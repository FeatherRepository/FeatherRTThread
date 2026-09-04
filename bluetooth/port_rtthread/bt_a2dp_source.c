/* bt_a2dp_source.c - M5 A2DP Source (M33, btstack 开源自研栈)
 *
 * 数据通路: M55 SBC 编码 -> 反向 ring (FT_ALINK2) -> 本模块按帧重组媒体包 ->
 * a2dp_source_stream_send_media_payload_rtp 上行到蓝牙耳机/音箱。
 * M55 侧编码产物已是 SBC 帧, 本文件不重编码, 只做帧汇聚 + RTP 时间戳。
 *
 * 连接管理 (msh 驱动, v1):
 *   bt_src scan           —— GAP inquiry 8s, 打印发现的设备 (音箱类 CoD 标记)
 *   bt_src connect <n>    —— 向扫描结果第 n 条发起 establish+start
 *   bt_src disconnect     —— 断流并断开
 *   bt_src                —— 状态/统计
 *
 * 编码格式约定 (v1): M55 固定 48k/JS/bitpool53 编码; 协商结果若不是 48k
 * 立体声则只打日志不启泵 (对端耳机几乎都支持 48k JS, btstack 选高优先)。
 *
 * 所有 handler 运行在 btloop 线程上下文 (非 ISR)。
 */
#include <rtthread.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "bluetooth.h"
#include "btstack_event.h"
#include "btstack_util.h"
#include "hci.h"
#include "gap.h"

#include "classic/a2dp_source.h"
#include "classic/avdtp.h"
#include "classic/avdtp_util.h"
#include "classic/sdp_server.h"

#include <feathertalk/audio_link.h>

/* Source 端点能力: 全采样率全声道模式 (对端取交集, btstack 优先选高) */
static uint8_t s_src_sbc_codec_capabilities[] = { 0xFF, 0xFF, 2, 53 };

static uint8_t s_sdp_a2dp_source_buffer[150];

static struct {
    uint8_t  local_seid;
    uint8_t  media_sbc_codec_configuration[4];
} s_src_sep;

/* ---- 扫描结果表 ---- */
#define FT_SRC_SCAN_MAX  8
static struct {
    bd_addr_t addr;
    char      name[32];
    uint32_t  cod;
    rt_bool_t is_audio;   /* CoD 含音频渲染类 */
} s_scan[FT_SRC_SCAN_MAX];
static uint8_t  s_scan_count;
static rt_bool_t s_scan_active;

/* ---- 流/发送状态 ---- */
#define FT_SRC_STATE_CLOSED   0
#define FT_SRC_STATE_OPEN     1
#define FT_SRC_STATE_PLAYING  2

static volatile int s_src_state;
static uint16_t     s_src_a2dp_cid;
static uint8_t      s_src_local_seid;
static bd_addr_t    s_src_peer;
static uint32_t     s_neg_rate;
static uint8_t      s_neg_channels;
static uint8_t      s_neg_bitpool_max;

/* 媒体泵 (btloop 定时器驱动) */
static btstack_timer_source_t s_pump_timer;
static rt_bool_t   s_pump_active;
static uint32_t    s_rtp_timestamp;
/* 帧化解析状态 (ring2 字节流 -> SBC 帧) */
static uint8_t     s_fbuf[2 + 512];
static uint32_t    s_fhave;
static uint32_t    s_fwant;
/* 媒体包暂存: [1B SBC hdr][N 帧] */
static uint8_t     s_mpkt[2 + 1024];
static uint32_t    s_mpkt_len;      /* 已装 SBC 帧字节数 (不含头) */
static uint32_t    s_mpkt_frames;
static uint16_t    s_max_payload;

/* 统计 */
static rt_uint32_t s_stat_sent_pkts;
static rt_uint32_t s_stat_sent_bytes;
static rt_uint32_t s_stat_tx_fail;
static rt_uint32_t s_stat_ring_drain;   /* 因无数据空转的泵次数 */
static rt_uint32_t s_stat_frames_tx;

/* ---- ring2 消费: 抽字节流并解帧 ----
 * 返回 1 = 拿到一个完整 SBC 帧 (拷入 out, 长度 *olen); 0 = 暂无 */
static int bt_src_next_sbc_frame(uint8_t *out, uint32_t *olen)
{
    while (1)
    {
        if (s_fwant == 0U)
        {
            uint8_t hdr[2];
            if (ft_alink_used(FT_ALINK2) < 2U) return 0;
            (void)ft_alink_read(FT_ALINK2, hdr, 2);
            uint32_t flen = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8);
            if (flen < 4U || flen > 512U)
            {
                s_fhave = 0U;
                s_fwant = 0U;
                continue;   /* 失步: 丢 1 字节重扫 (self-heal) */
            }
            s_fwant = flen;
            s_fhave = 0U;
        }
        if (ft_alink_used(FT_ALINK2) < s_fwant) return 0;
        rt_uint32_t got = ft_alink_read(FT_ALINK2, s_fbuf, s_fwant);
        if (got != s_fwant) return 0;
        if (s_fbuf[0] != 0x9CU)   /* SBC 同步字守卫 */
        {
            s_fwant = 0U;
            continue;
        }
        memcpy(out, s_fbuf, s_fwant);
        *olen = s_fwant;
        s_fwant = 0U;
        return 1;
    }
}

/* 媒体泵状态 */
static rt_bool_t   s_can_send_pending;   /* 已请求 can_send, 等事件 */
static rt_tick_t   s_src_last_can_send_tick;  /* 看门狗: 上次 CAN_SEND 时刻 */

/* 链路静默死亡看门狗: 流开着但 3s 无 CAN_SEND 信用且暂存有货 ->
 * 对端已死 (耳机失联但 ACL 未断/监管超时未到), 主动断开触发正常清理链
 * (STREAM_RELEASED -> ring2 发布 0 -> M55 编码器停 -> UAC 本地播放恢复)。
 * 有货才判: UAC 空闲 (无数据) 不误伤。 */
#define FT_SRC_CAN_SEND_TIMEOUT_TICK  rt_tick_from_millisecond(3000)

/* 从 ring2 攒帧到媒体包暂存 (载荷上限内尽量多装) */
static void bt_src_stage_frames(void)
{
    while (1)
    {
        uint32_t flen = 0;
        if (s_mpkt_len + 1U + 128U > (uint32_t)s_max_payload) break;
        if (!bt_src_next_sbc_frame(&s_mpkt[1 + s_mpkt_len], &flen)) break;
        s_mpkt_len += (uint16_t)flen;
        s_mpkt_frames++;
    }
}

/* 发送驱动核心: 有货就请求 CAN_SEND; 真正的"发完立刻续上"在事件里 */
static void bt_src_kick(void)
{
    if (s_src_state != FT_SRC_STATE_PLAYING) return;
    if (s_mpkt_frames == 0U)
    {
        bt_src_stage_frames();          /* ring2 -> 暂存 */
        if (s_mpkt_frames == 0U)
        {
            s_stat_ring_drain++;
            return;                     /* ring2 空: 等下个 tick */
        }
    }
    if (!s_can_send_pending)
    {
        s_can_send_pending = RT_TRUE;
        a2dp_source_stream_endpoint_request_can_send_now(s_src_a2dp_cid,
                                                         s_src_local_seid);
    }
}

/* 前向声明: 看门狗在泵 tick 里调用 (定义在本文件后面) */
static void bt_src_pump_stop(void);
static void bt_src_ring2_publish(rt_uint32_t rate);

/* ---- 媒体泵: 10ms 兜底节拍 (防空转后链路停摆), 主驱动在 CAN_SEND 事件 ---- */
static void bt_src_pump_handler(btstack_timer_source_t *ts)
{
    btstack_run_loop_set_timer(ts, 10);
    btstack_run_loop_add_timer(ts);
    bt_src_kick();
    if (s_src_state == FT_SRC_STATE_PLAYING && s_mpkt_frames > 0U &&
        (rt_tick_get() - s_src_last_can_send_tick) > FT_SRC_CAN_SEND_TIMEOUT_TICK)
    {
        rt_kprintf("[SRC] watchdog: 3s no CAN_SEND with data pending, force disconnect\n");
        a2dp_source_disconnect(s_src_a2dp_cid);
        s_src_a2dp_cid = 0;
        s_src_state = FT_SRC_STATE_CLOSED;
        bt_src_ring2_publish(0U);   /* M55 编码器停 -> UAC 本地播放恢复 */
        bt_src_pump_stop();
    }
}

static void bt_src_pump_start(void)
{
    s_max_payload = a2dp_max_media_payload_size(s_src_a2dp_cid, s_src_local_seid);
    if (s_max_payload > sizeof(s_mpkt) - 1U) s_max_payload = sizeof(s_mpkt) - 1U;
    s_rtp_timestamp = 0;
    s_mpkt_len = 0;
    s_mpkt_frames = 0;
    s_fhave = 0;
    s_fwant = 0;
    s_can_send_pending = RT_FALSE;
    s_src_last_can_send_tick = rt_tick_get();   /* 看门狗基线 */
    s_stat_sent_pkts = 0;      /* 每次开流清零, 窗口测量不受旧会话污染 */
    s_stat_sent_bytes = 0;
    s_stat_frames_tx = 0;
    s_stat_tx_fail = 0;
    s_stat_ring_drain = 0;
    s_pump_active = RT_TRUE;
    btstack_run_loop_remove_timer(&s_pump_timer);
    btstack_run_loop_set_timer_handler(&s_pump_timer, bt_src_pump_handler);
    btstack_run_loop_set_timer(&s_pump_timer, 10);
    btstack_run_loop_add_timer(&s_pump_timer);
    rt_kprintf("[SRC] pump start (max_payload %u)\n", s_max_payload);
}

static void bt_src_pump_stop(void)
{
    s_pump_active = RT_FALSE;
    s_can_send_pending = RT_FALSE;
    btstack_run_loop_remove_timer(&s_pump_timer);
}

/* 流状态 -> ring2 控制块换代 (M55 编码器线程据此启停, 与 Sink 方向同款约定) */
static void bt_src_ring2_publish(rt_uint32_t rate)
{
    FT_ALINK2->fmt_rate = rate;
    FT_ALINK2->fmt_bits = 16U;
    FT_ALINK2->fmt_ch   = 2U;
    __DMB();
    FT_ALINK2->fmt_gen++;
    FT_ALINK_DCACHE_CLEAN(FT_ALINK2_BASE, 32);
}

/* ---- A2DP source 事件 ---- */
static void bt_src_packet_handler(uint8_t packet_type, uint16_t channel,
                                  uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;

    switch (packet[2])
    {
    case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
        s_neg_rate = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
        s_neg_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(packet);
        s_neg_bitpool_max = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);
        rt_kprintf("[SRC] SBC config: %lu Hz, %u ch, bitpool max %u\n",
                   (unsigned long)s_neg_rate, s_neg_channels, s_neg_bitpool_max);
        break;

    case A2DP_SUBEVENT_STREAM_ESTABLISHED:
        if (a2dp_subevent_stream_established_get_status(packet) != ERROR_CODE_SUCCESS)
        {
            rt_kprintf("[SRC] establish failed, status 0x%02x\n",
                       a2dp_subevent_stream_established_get_status(packet));
            s_src_a2dp_cid = 0;
            break;
        }
        s_src_a2dp_cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
        s_src_local_seid = a2dp_subevent_stream_established_get_local_seid(packet);
        s_src_state = FT_SRC_STATE_OPEN;
        rt_kprintf("[SRC] established cid 0x%02x seid %u, starting...\n",
                   s_src_a2dp_cid, s_src_local_seid);
        a2dp_source_start_stream(s_src_a2dp_cid, s_src_local_seid);
        break;

    case A2DP_SUBEVENT_STREAM_STARTED:
        /* v1 编码恒 48k: 协商结果非 48k 不起泵 (对端罕见; 打日志明示) */
        if (s_neg_rate != 48000U)
        {
            rt_kprintf("[SRC] negotiated %lu Hz != 48k, 编码器暂不支持, 不出声\n",
                       (unsigned long)s_neg_rate);
            break;
        }
        s_src_state = FT_SRC_STATE_PLAYING;
        rt_kprintf("[SRC] stream started -> ring2 gen %lu\n",
                   (unsigned long)(FT_ALINK2->fmt_gen + 1));
        bt_src_ring2_publish(48000U);   /* M55 编码器启动信号 */
        bt_src_pump_start();
        break;

    case A2DP_SUBEVENT_STREAM_SUSPENDED:
    case A2DP_SUBEVENT_STREAM_RELEASED:
        s_src_state = (packet[2] == A2DP_SUBEVENT_STREAM_SUSPENDED)
                      ? FT_SRC_STATE_OPEN : FT_SRC_STATE_CLOSED;
        bt_src_ring2_publish(0U);       /* M55 编码器停止信号 */
        bt_src_pump_stop();
        rt_kprintf("[SRC] stream %s (tx %lu pkts/%lu B, fail %lu)\n",
                   (packet[2] == A2DP_SUBEVENT_STREAM_SUSPENDED) ? "suspended" : "released",
                   (unsigned long)s_stat_sent_pkts, (unsigned long)s_stat_sent_bytes,
                   (unsigned long)s_stat_tx_fail);
        break;

    case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        s_src_state = FT_SRC_STATE_CLOSED;
        s_src_a2dp_cid = 0;
        bt_src_ring2_publish(0U);
        bt_src_pump_stop();
        rt_kprintf("[SRC] signaling released\n");
        break;

    case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
        s_can_send_pending = RT_FALSE;
        s_src_last_can_send_tick = rt_tick_get();
        if (s_mpkt_frames > 0U)
        {
            /* [0] = SBC 媒体头: 帧数 (不分片) */
            s_mpkt[0] = (uint8_t)s_mpkt_frames;
            uint8_t rc = a2dp_source_stream_send_media_payload_rtp(
                s_src_a2dp_cid, s_src_local_seid, 0, s_rtp_timestamp,
                s_mpkt, (uint16_t)(s_mpkt_len + 1U));
            if (rc == 0U)
            {
                s_stat_sent_pkts++;
                s_stat_sent_bytes += s_mpkt_len;
                s_stat_frames_tx += s_mpkt_frames;
                s_rtp_timestamp += s_mpkt_frames * 128U;   /* 48k: 128 样点/帧 */
            }
            else
            {
                s_stat_tx_fail++;
            }
            s_mpkt_len = 0;
            s_mpkt_frames = 0;
        }
        bt_src_kick();   /* 发完立刻续攒续发: 速率跟随链路信用而非定时器 */
        break;

    default:
        break;
    }
}

/* ---- GAP inquiry: 发现 Classic 音频渲染设备 ---- */
#define FT_COD_AUDIO_RENDER  0x040414U   /* Rendering bit + Audio/Video 主类 */
static void bt_src_gap_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    bd_addr_t addr;

    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet))
    {
    case GAP_EVENT_INQUIRY_RESULT:
    {
        if (s_scan_count >= FT_SRC_SCAN_MAX) break;
        gap_event_inquiry_result_get_bd_addr(packet, addr);
        uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
        uint8_t idx = s_scan_count++;
        memcpy(s_scan[idx].addr, addr, 6);
        s_scan[idx].cod = cod;
        s_scan[idx].name[0] = 0;
        s_scan[idx].is_audio = ((cod & 0x001F00U) == 0x000400U) ? RT_TRUE : RT_FALSE; /* A/V 主类 */
        if (gap_event_inquiry_result_get_name_available(packet))
        {
            uint8_t nl = gap_event_inquiry_result_get_name_len(packet);
            if (nl > sizeof(s_scan[idx].name) - 1U) nl = sizeof(s_scan[idx].name) - 1U;
            memcpy(s_scan[idx].name, gap_event_inquiry_result_get_name(packet), nl);
            s_scan[idx].name[nl] = 0;
        }
        rt_kprintf("[SRC] [%u] %s cod=%06lx%s\n", idx, bd_addr_to_str(addr),
                   (unsigned long)cod, s_scan[idx].is_audio ? " (audio)" : "");
        break;
    }
    case GAP_EVENT_INQUIRY_COMPLETE:
        s_scan_active = RT_FALSE;
        rt_kprintf("[SRC] scan done, %u found\n", s_scan_count);
        break;
    default:
        break;
    }
}

/* ---- setup: bt_main.c 在 sdp_init 之后调 ---- */
static btstack_packet_callback_registration_t s_gap_reg;
int bt_a2dp_source_setup(void)
{
    a2dp_source_init();
    a2dp_source_register_packet_handler(&bt_src_packet_handler);

    /* inquiry 结果/完成事件走 GAP 事件包, 注册到 HCI 事件链 */
    s_gap_reg.callback = &bt_src_gap_handler;
    hci_add_event_handler(&s_gap_reg);

    avdtp_stream_endpoint_t *sep = a2dp_source_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC,
        s_src_sbc_codec_capabilities, sizeof(s_src_sbc_codec_capabilities),
        s_src_sep.media_sbc_codec_configuration,
        sizeof(s_src_sep.media_sbc_codec_configuration));
    if (sep == RT_NULL)
    {
        rt_kprintf("[SRC] create stream endpoint failed (pool)\n");
        return -1;
    }
    s_src_sep.local_seid = avdtp_local_seid(sep);
    s_src_local_seid = s_src_sep.local_seid;
    /* 编码恒 48k: 告诉协商层我们的偏好 */
    avdtp_set_preferred_sampling_frequency(sep, 48000);

    memset(s_sdp_a2dp_source_buffer, 0, sizeof(s_sdp_a2dp_source_buffer));
    a2dp_source_create_sdp_record(s_sdp_a2dp_source_buffer,
                                  sdp_create_service_record_handle(),
                                  AVDTP_SOURCE_FEATURE_MASK_PLAYER, NULL, NULL);
    uint8_t rc = sdp_register_service(s_sdp_a2dp_source_buffer);
    if (rc != 0U) rt_kprintf("[SRC] SDP a2dp_source register FAILED rc=%u\n", rc);

    rt_kprintf("[SRC] source ready (seid %u, SBC 48k preferred)\n",
               s_src_sep.local_seid);
    return 0;
}

/* ---- msh ---- */
static int bt_src(int argc, char **argv)
{
    static const char *const names[] = { "closed", "open", "playing" };

    if (argc > 1 && strcmp(argv[1], "scan") == 0)
    {
        if (s_scan_active)
        {
            rt_kprintf("[SRC] scan already active\n");
            return 0;
        }
        s_scan_count = 0;
        s_scan_active = RT_TRUE;
        rt_kprintf("[SRC] scanning 8s ...\n");
        gap_inquiry_start(7);   /* 7 * 1.28s ≈ 9s */
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "connect") == 0)
    {
        if (s_src_state != FT_SRC_STATE_CLOSED || s_src_a2dp_cid != 0)
        {
            rt_kprintf("[SRC] busy, state=%s cid=0x%x\n",
                       names[s_src_state & 3], s_src_a2dp_cid);
            return 0;
        }
        if (argc < 3)
        {
            rt_kprintf("usage: bt_src connect <scan-index|xx:xx:xx:xx:xx:xx>\n");
            return 0;
        }
        if (strlen(argv[2]) >= 17U && argv[2][2] == ':')
        {
            sscanf_bd_addr(argv[2], s_src_peer);   /* 直接按 MAC 连 (如对端已挂着 ACL) */
        }
        else
        {
            uint32_t idx = strtoul(argv[2], RT_NULL, 0);
            if (idx >= s_scan_count)
            {
                rt_kprintf("[SRC] bad index (have %u)\n", s_scan_count);
                return 0;
            }
            memcpy(s_src_peer, s_scan[idx].addr, 6);
        }
        uint8_t rc = a2dp_source_establish_stream(s_src_peer, &s_src_a2dp_cid);
        rt_kprintf("[SRC] establish %s -> rc=0x%02x cid=0x%x\n",
                   bd_addr_to_str(s_src_peer), rc, s_src_a2dp_cid);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "disconnect") == 0)
    {
        if (s_src_a2dp_cid != 0)
        {
            a2dp_source_disconnect(s_src_a2dp_cid);
        }
        return 0;
    }
    rt_kprintf("[SRC] state=%s cid=0x%x peer=%s\n",
               names[s_src_state & 3], s_src_a2dp_cid, bd_addr_to_str(s_src_peer));
    rt_kprintf("[SRC] negotiated: %lu Hz %u ch bitpool_max=%u\n",
               (unsigned long)s_neg_rate, s_neg_channels, s_neg_bitpool_max);
    rt_kprintf("[SRC] tx: pkts=%lu bytes=%lu frames=%lu fail=%lu, pump_idle=%lu, ring2 used=%lu\n",
               (unsigned long)s_stat_sent_pkts, (unsigned long)s_stat_sent_bytes,
               (unsigned long)s_stat_frames_tx, (unsigned long)s_stat_tx_fail,
               (unsigned long)s_stat_ring_drain,
               (unsigned long)ft_alink_used(FT_ALINK2));
    return 0;
}
MSH_CMD_EXPORT_ALIAS(bt_src, bt_src, M5: A2DP Source scan/connect/status);
