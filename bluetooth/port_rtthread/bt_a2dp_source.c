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
 * 编码格式约定: M55 固定 48k/16 blocks/8 subbands/loudness/JS 编码。
 * 能力集也只宣告这一组合, 避免对端选到 M55 无法生成的配置; bitpool 取
 * 对端协商的 max (常见 39/53), 通过 ring2 flags 高字节传给 M55。
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
#include <feathertalk/bt_source_shared.h>

/* Source 端点能力: 48 kHz / Joint Stereo / 16 blocks / 8 subbands / Loudness。
 * 与 M55 固定编码器一致; max bitpool 53 只是上限, 对端可协商更低值。 */
static uint8_t s_src_sbc_codec_capabilities[] = {
    (AVDTP_SBC_48000 << 4) | AVDTP_SBC_JOINT_STEREO,
    (AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) |
        AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    2, 53
};

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
static uint8_t  s_name_req_next;
static rt_bool_t s_name_req_active;
static bd_addr_t s_name_req_addr;

/* ---- 流/发送状态 ---- */
#define FT_SRC_STATE_CLOSED     FT_BT_SOURCE_STATE_CLOSED
#define FT_SRC_STATE_CONNECTING FT_BT_SOURCE_STATE_CONNECTING
#define FT_SRC_STATE_OPEN       FT_BT_SOURCE_STATE_OPEN
#define FT_SRC_STATE_PLAYING    FT_BT_SOURCE_STATE_PLAYING
#define FT_SRC_STATE_ERROR      FT_BT_SOURCE_STATE_ERROR

static volatile int s_src_state;
static uint16_t     s_src_a2dp_cid;
static uint8_t      s_src_local_seid;
static bd_addr_t    s_src_peer;
static uint32_t     s_neg_rate;
static uint8_t      s_neg_channels;
static uint8_t      s_neg_bitpool_max;

/* ---- M6: 断链自动重连 (指数退避, 最大 5 次) ---- */
#define FT_SRC_RECONNECT_MAX_RETRY  5
#define FT_SRC_RECONNECT_BASE_MS    2000
static bd_addr_t                s_reconnect_peer;
static rt_bool_t                s_reconnect_valid;
static rt_uint8_t               s_reconnect_retry;
static btstack_timer_source_t   s_reconnect_timer;

static void bt_src_reconnect_timer_cb(btstack_timer_source_t *ts);

static uint8_t bt_src_scan_find(const uint8_t *addr)
{
    for (uint8_t i = 0U; i < s_scan_count; i++)
    {
        if (memcmp(s_scan[i].addr, addr, 6U) == 0) return i;
    }
    return 0xFFU;
}

static uint8_t bt_src_scan_reserve(const uint8_t *addr, uint32_t cod)
{
    uint8_t index = bt_src_scan_find(addr);
    rt_bool_t audio = ((cod & 0x001F00U) == 0x000400U) ? RT_TRUE : RT_FALSE;

    if (index != 0xFFU)
    {
        s_scan[index].cod = cod;
        s_scan[index].is_audio = audio;
        return index;
    }
    if (s_scan_count < FT_SRC_SCAN_MAX)
    {
        index = s_scan_count++;
    }
    else if (audio)
    {
        for (uint8_t i = 0U; i < s_scan_count; i++)
        {
            if (!s_scan[i].is_audio)
            {
                index = i;
                break;
            }
        }
    }
    if (index == 0xFFU) return index;

    memset(&s_scan[index], 0, sizeof(s_scan[index]));
    memcpy(s_scan[index].addr, addr, 6U);
    s_scan[index].cod = cod;
    s_scan[index].is_audio = audio;
    return index;
}

static void bt_src_publish(void)
{
    volatile ft_bt_source_shared_t *shared = FT_BT_SOURCE;
    uint32_t peer_flags =
        (s_src_state == FT_SRC_STATE_OPEN || s_src_state == FT_SRC_STATE_PLAYING) ?
        FT_BT_SOURCE_FLAG_CONNECTED : 0U;

    if (shared->magic != FT_BT_SOURCE_MAGIC ||
        shared->version != FT_BT_SOURCE_VERSION)
        shared->sequence = 0U;
    shared->sequence++;
    __DMB();
    shared->magic = FT_BT_SOURCE_MAGIC;
    shared->version = FT_BT_SOURCE_VERSION;
    shared->scan_active = s_scan_active ? 1U : 0U;
    shared->state = (uint32_t)s_src_state;
    shared->count = s_scan_count;
    memcpy((void *)shared->peer, s_src_peer, 6U);
    shared->peer_flags = (uint8_t)peer_flags;
    shared->reserved = 0U;
    for (uint8_t i = 0U; i < s_scan_count; i++)
    {
        volatile ft_bt_source_device_t *dst = &shared->devices[i];
        size_t name_len = strlen(s_scan[i].name);
        if (name_len >= FT_BT_SOURCE_NAME_LEN) name_len = FT_BT_SOURCE_NAME_LEN - 1U;
        memcpy((void *)dst->addr, s_scan[i].addr, 6U);
        dst->flags = (uint8_t)((s_scan[i].is_audio ? FT_BT_SOURCE_FLAG_AUDIO : 0U) |
            ((peer_flags != 0U && memcmp(s_scan[i].addr, s_src_peer, 6U) == 0) ?
             FT_BT_SOURCE_FLAG_CONNECTED : 0U));
        dst->name_len = (uint8_t)name_len;
        memset((void *)dst->name, 0, FT_BT_SOURCE_NAME_LEN);
        memcpy((void *)dst->name, s_scan[i].name, name_len);
    }
    __DMB();
    shared->sequence++;
    __DMB();
}

/* Classic inquiry often returns an audio device without its remote name.
 * Resolve names one at a time after the inquiry so the UI does not have to
 * show "(unnamed)" for every headphone that omits EIR name data. */
static void bt_src_request_next_name(void)
{
    while (!s_name_req_active && s_name_req_next < s_scan_count)
    {
        uint8_t index = s_name_req_next++;
        if (s_scan[index].name[0] != '\0') continue;
        memcpy(s_name_req_addr, s_scan[index].addr, 6U);
        if (gap_remote_name_request(s_name_req_addr, 0, 0) != 0)
        {
            rt_kprintf("[SRC] name request rejected for %s\n",
                       bd_addr_to_str(s_scan[index].addr));
            continue;
        }
        s_name_req_active = RT_TRUE;
    }
}

static void bt_src_cancel_name_request(void)
{
    if (!s_name_req_active) return;
    (void)hci_send_cmd(&hci_remote_name_request_cancel, s_name_req_addr);
    s_name_req_active = RT_FALSE;
}

/* 调度下一次重连 (指数退避; 超过最大次数放弃) */
static void bt_src_schedule_reconnect(void)
{
    if (!s_reconnect_valid || s_reconnect_retry >= FT_SRC_RECONNECT_MAX_RETRY)
    {
        if (s_reconnect_valid)
            rt_kprintf("[SRC] reconnect: max retries, giving up\n");
        s_reconnect_valid = RT_FALSE;
        return;
    }
    uint32_t delay_ms = FT_SRC_RECONNECT_BASE_MS << (s_reconnect_retry > 3 ? 3 : s_reconnect_retry);
    btstack_run_loop_set_timer(&s_reconnect_timer, delay_ms);
    btstack_run_loop_set_timer_handler(&s_reconnect_timer, bt_src_reconnect_timer_cb);
    btstack_run_loop_add_timer(&s_reconnect_timer);
    s_reconnect_retry++;
    rt_kprintf("[SRC] reconnect #%u to %s in %lu ms\n",
               s_reconnect_retry, bd_addr_to_str(s_reconnect_peer),
               (unsigned long)delay_ms);
}

/* 重连定时器回调: 尝试对上次的 peer 重新 establish */
static void bt_src_reconnect_timer_cb(btstack_timer_source_t *ts)
{
    if (s_src_state != FT_SRC_STATE_CLOSED || !s_reconnect_valid) return;
    rt_kprintf("[SRC] reconnect attempt #%u to %s\n",
               s_reconnect_retry, bd_addr_to_str(s_reconnect_peer));
    a2dp_source_establish_stream(s_reconnect_peer, &s_src_a2dp_cid);
}

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

/* M6-BT: 角色切回 SINK 时由 bt_main 调用 —— 断开 Source 链路并停泵 */
void bt_a2dp_source_drop(void)
{
    btstack_run_loop_remove_timer(&s_reconnect_timer);
    s_reconnect_valid = RT_FALSE;
    s_reconnect_retry = 0U;
    if (s_src_a2dp_cid != 0U)
    {
        a2dp_source_disconnect(s_src_a2dp_cid);
        s_src_a2dp_cid = 0U;
    }
    s_src_state = FT_SRC_STATE_CLOSED;
    s_scan_active = RT_FALSE;
    s_scan_count = 0U;
    memset(s_src_peer, 0, sizeof(s_src_peer));
    bt_src_pump_stop();
    bt_src_ring2_publish(0U);   /* M55 编码器停 -> UAC 本地播放恢复 */
    bt_src_publish();
}

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
        bt_src_publish();
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
    rt_uint32_t bitpool = 0U;
    if (rate != 0U)
    {
        bitpool = s_neg_bitpool_max;
        if (bitpool < 2U || bitpool > 53U) bitpool = 53U;
    }
    FT_ALINK2->flags = (bitpool << FT_ALINK_FLAGS_BITPOOL_SHIFT) &
                       FT_ALINK_FLAGS_BITPOOL_MASK;
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
            s_src_state = FT_SRC_STATE_ERROR;
            bt_src_publish();
            break;
        }
        s_src_a2dp_cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
        s_src_local_seid = a2dp_subevent_stream_established_get_local_seid(packet);
        s_src_state = FT_SRC_STATE_OPEN;
        /* 保存对端地址供断链重连 */
        memcpy(s_reconnect_peer, s_src_peer, 6);
        s_reconnect_valid = RT_TRUE;
        s_reconnect_retry = 0;
        bt_src_publish();
        rt_kprintf("[SRC] established cid 0x%02x seid %u, starting...\n",
                   s_src_a2dp_cid, s_src_local_seid);
        /* The peer was usually discovered without its name; try once now so
         * the connected-device row can show a real name. */
        bt_src_cancel_name_request();
        {
            uint8_t name_index = bt_src_scan_find(s_src_peer);
            if (name_index != 0xFFU && s_scan[name_index].name[0] == '\0')
            {
                memcpy(s_name_req_addr, s_src_peer, 6U);
                if (gap_remote_name_request(s_name_req_addr, 0, 0) == 0)
                    s_name_req_active = RT_TRUE;
            }
        }
        a2dp_source_start_stream(s_src_a2dp_cid, s_src_local_seid);
        break;

    case A2DP_SUBEVENT_STREAM_STARTED:
        /* v1 编码恒 48k: 协商结果非 48k 不起泵 (对端罕见; 打日志明示) */
        if (s_neg_rate != 48000U)
        {
            s_src_state = FT_SRC_STATE_ERROR;
            bt_src_publish();
            rt_kprintf("[SRC] negotiated %lu Hz != 48k, 编码器暂不支持, 不出声\n",
                       (unsigned long)s_neg_rate);
            break;
        }
        s_src_state = FT_SRC_STATE_PLAYING;
        bt_src_publish();
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
        bt_src_publish();
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
        bt_src_publish();
        rt_kprintf("[SRC] signaling released\n");
        bt_src_schedule_reconnect();
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
        gap_event_inquiry_result_get_bd_addr(packet, addr);
        uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
        uint8_t idx = bt_src_scan_reserve(addr, cod);
        if (idx == 0xFFU) break;
        if (gap_event_inquiry_result_get_name_available(packet))
        {
            uint8_t nl = gap_event_inquiry_result_get_name_len(packet);
            if (nl > sizeof(s_scan[idx].name) - 1U) nl = sizeof(s_scan[idx].name) - 1U;
            memcpy(s_scan[idx].name, gap_event_inquiry_result_get_name(packet), nl);
            s_scan[idx].name[nl] = 0;
        }
        bt_src_publish();
        rt_kprintf("[SRC] [%u] %s cod=%06lx%s\n", idx, bd_addr_to_str(addr),
                   (unsigned long)cod, s_scan[idx].is_audio ? " (audio)" : "");
        break;
    }
    case GAP_EVENT_INQUIRY_COMPLETE:
        s_scan_active = RT_FALSE;
        s_name_req_next = 0U;
        s_name_req_active = RT_FALSE;
        bt_src_publish();
        rt_kprintf("[SRC] scan done, %u found\n", s_scan_count);
        bt_src_request_next_name();
        break;
    case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
    {
        bd_addr_t addr;
        const char *name;
        uint8_t idx;
        s_name_req_active = RT_FALSE;
        if (hci_event_remote_name_request_complete_get_status(packet) == 0)
        {
            hci_event_remote_name_request_complete_get_bd_addr(packet, addr);
            name = hci_event_remote_name_request_complete_get_remote_name(packet);
            idx = bt_src_scan_find(addr);
            if (idx != 0xFFU && name != RT_NULL && name[0] != '\0')
            {
                size_t len = strlen(name);
                if (len >= sizeof(s_scan[idx].name)) len = sizeof(s_scan[idx].name) - 1U;
                memcpy(s_scan[idx].name, name, len);
                s_scan[idx].name[len] = 0;
                rt_kprintf("[SRC] name %s -> %s\n", bd_addr_to_str(addr),
                           s_scan[idx].name);
                bt_src_publish();
            }
        }
        bt_src_request_next_name();
        break;
    }
    default:
        break;
    }
}

int bt_a2dp_source_scan(void)
{
    if (s_scan_active) return RT_EOK;
    if (s_src_state != FT_SRC_STATE_CLOSED && s_src_state != FT_SRC_STATE_ERROR)
        return -RT_EBUSY;
    bt_src_cancel_name_request();
    s_name_req_next = 0U;
    s_scan_count = 0U;
    s_scan_active = RT_TRUE;
    bt_src_publish();
    rt_kprintf("[SRC] scanning 8s ...\n");
    gap_inquiry_start(7);
    return RT_EOK;
}

int bt_a2dp_source_connect_index(uint8_t index)
{
    if (index >= s_scan_count) return -RT_EINVAL;
    if (s_src_state != FT_SRC_STATE_CLOSED && s_src_state != FT_SRC_STATE_ERROR)
        return -RT_EBUSY;
    if (s_src_a2dp_cid != 0U) return -RT_EBUSY;
    if (s_scan_active)
    {
        gap_inquiry_stop();
        s_scan_active = RT_FALSE;
    }
    bt_src_cancel_name_request();
    memcpy(s_src_peer, s_scan[index].addr, 6U);
    s_src_state = FT_SRC_STATE_CONNECTING;
    bt_src_publish();
    uint8_t rc = a2dp_source_establish_stream(s_src_peer, &s_src_a2dp_cid);
    rt_kprintf("[SRC] establish %s -> rc=0x%02x cid=0x%x\n",
               bd_addr_to_str(s_src_peer), rc, s_src_a2dp_cid);
    if (rc != 0U)
    {
        s_src_a2dp_cid = 0U;
        s_src_state = FT_SRC_STATE_ERROR;
        bt_src_publish();
        return -RT_ERROR;
    }
    return RT_EOK;
}

int bt_a2dp_source_disconnect(void)
{
    btstack_run_loop_remove_timer(&s_reconnect_timer);
    s_reconnect_valid = RT_FALSE;
    s_reconnect_retry = 0U;
    if (s_src_a2dp_cid != 0U)
    {
        a2dp_source_disconnect(s_src_a2dp_cid);
        s_src_a2dp_cid = 0U;
    }
    s_src_state = FT_SRC_STATE_CLOSED;
    bt_src_pump_stop();
    bt_src_ring2_publish(0U);
    bt_src_publish();
    return RT_EOK;
}

/* M6-BT: 角色切到 SOURCE 时由 bt_main.c 调用——自动扫描耳机 */
void bt_a2dp_source_start_scan(void)
{
    rt_kprintf("[SRC] auto-scan for headphones\n");
    if (bt_a2dp_source_scan() != RT_EOK)
        rt_kprintf("[SRC] auto-scan skipped (source busy)\n");
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
    FT_BT_SOURCE->magic = 0U;
    FT_BT_SOURCE->sequence = 0U;
    __DMB();
    bt_src_publish();
    return 0;
}

/* ---- msh ---- */
static int bt_src(int argc, char **argv)
{
    static const char *const names[] = {
        "closed", "connecting", "open", "playing", "error"
    };

    if (argc > 1 && strcmp(argv[1], "scan") == 0)
    {
        int rc = bt_a2dp_source_scan();
        if (rc != RT_EOK) rt_kprintf("[SRC] scan rejected rc=%d\n", rc);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "connect") == 0)
    {
        if (argc < 3)
        {
            rt_kprintf("usage: bt_src connect <scan-index|xx:xx:xx:xx:xx:xx>\n");
            return 0;
        }
        if (strlen(argv[2]) >= 17U && argv[2][2] == ':')
        {
            if (s_src_state != FT_SRC_STATE_CLOSED &&
                s_src_state != FT_SRC_STATE_ERROR)
            {
                rt_kprintf("[SRC] busy, state=%s cid=0x%x\n",
                           names[s_src_state], s_src_a2dp_cid);
                return 0;
            }
            if (s_scan_active)
            {
                gap_inquiry_stop();
                s_scan_active = RT_FALSE;
            }
            bt_src_cancel_name_request();
            sscanf_bd_addr(argv[2], s_src_peer);
            s_src_state = FT_SRC_STATE_CONNECTING;
            bt_src_publish();
            uint8_t rc = a2dp_source_establish_stream(s_src_peer, &s_src_a2dp_cid);
            rt_kprintf("[SRC] establish %s -> rc=0x%02x cid=0x%x\n",
                       bd_addr_to_str(s_src_peer), rc, s_src_a2dp_cid);
            if (rc != 0U)
            {
                s_src_a2dp_cid = 0U;
                s_src_state = FT_SRC_STATE_ERROR;
                bt_src_publish();
            }
        }
        else
        {
            uint32_t idx = strtoul(argv[2], RT_NULL, 0);
            if (idx > 0xFFU)
            {
                rt_kprintf("[SRC] bad index (have %u)\n", s_scan_count);
                return 0;
            }
            int rc = bt_a2dp_source_connect_index((uint8_t)idx);
            if (rc != RT_EOK) rt_kprintf("[SRC] connect rejected rc=%d\n", rc);
        }
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "disconnect") == 0)
    {
        (void)bt_a2dp_source_disconnect();
        return 0;
    }
    rt_kprintf("[SRC] state=%s cid=0x%x peer=%s\n",
               names[s_src_state], s_src_a2dp_cid, bd_addr_to_str(s_src_peer));
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
