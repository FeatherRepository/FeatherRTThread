/* bt_a2dp_sink.c - M4b A2DP Sink + AVRCP (M33, btstack 开源自研栈)
 *
 * 参照 bluetooth/reference/btstack/example/a2dp_sink_demo.c 的 setup_demo()
 * 模式: a2dp_sink/avrcp target+controller 初始化、SBC stream endpoint
 * (44.1k+48k 双 mandatory, bitpool 2-53)、SDP 记录四件套。
 *
 * 媒体通路: a2dp_sink_register_media_handler 收原始 L2CAP 媒体包
 *   -> 剥 12B RTP 头 (+CSRC 扩展) 和 1B SBC 帧数字段
 *   -> 按 A0-2 帧化协议 [2B LE len + payload] 写跨核 ring + 门铃
 *      (payload 可含多个 SBC 帧, M55 端 btstack_sbc_decoder_process_data
 *       自己做流式分帧)
 * 流状态机: STREAM_STARTED -> ring 控制块登记协商格式 + fmt_gen++
 *           (M55 据此 claim sound0 开流); SUSPENDED/RELEASED/信令断开
 *           -> fmt_rate=0 + fmt_gen++ (M55 据此关流释放)。
 * AVRCP: 绝对音量 VOLUME_CHANGED -> ring 控制块 volume_percent (M55 应用)。
 *
 * 仅在 bt_main.c 的 btstack init 段 (sdp_init 之后) 调 bt_a2dp_sink_setup()。
 * 所有 handler 运行在 btloop 线程上下文 (非 ISR)。
 */
#include <rtthread.h>
#include <stdint.h>
#include <string.h>

#include "bluetooth.h"
#include "bluetooth_company_id.h"
#include "btstack_event.h"
#include "btstack_util.h"
#include "hci.h"

#include "classic/a2dp_sink.h"
#include "classic/avdtp.h"
#include "classic/avdtp_util.h"
#include "classic/avrcp.h"
#include "classic/avrcp_controller.h"
#include "classic/avrcp_target.h"
#include "classic/device_id_server.h"
#include "classic/sdp_server.h"
#include "classic/sdp_util.h"

#include <feathertalk/audio_link.h>

/* audio_link_m33.c 的 producer 写入口 (写 ring + 门铃) */
extern rt_uint32_t ft_audio_produce(const rt_uint8_t *data, rt_uint32_t len);

/* 采样率自适应决策 (实测修正): 恢复声明全部采样率+声道模式, bitpool 2-53。
 * 曾试 48k-only (0x1F) 想省掉 44.1k->48k 重采样, 实测手机 (Android) 看到
 * 48k-only 能力后会发畸形的 SET_CONFIGURATION (Media Codec capability 长度
 * 为 0), 被我们按 BAD_CODEC_FORMAT 拒绝 -> 手机直接放弃连接 (两次重试均同),
 * 见 worklog/pulls/m4b_phone_conn_*.log。结论: 不能靠收窄能力集选采样率。
 * 自适应路径: 对端任选 44.1k/48k -> M33 把协商采样率写 ring 控制块 fmt_rate
 * -> M55 按 fmt_rate 自动直通(48k)或线性重采样(44.1k->48k), sound0 恒 48k。 */
static uint8_t s_media_sbc_codec_capabilities[] = { 0xFF, 0xFF, 2, 53 };

static uint8_t s_sdp_a2dp_sink_buffer[150];
static uint8_t s_sdp_avrcp_target_buffer[150];
static uint8_t s_sdp_avrcp_controller_buffer[200];
static uint8_t s_sdp_device_id_buffer[100];

/* stream endpoint: seid + 回写协商配置 (demo 同款) */
static struct {
    uint8_t a2dp_local_seid;
    uint8_t media_sbc_codec_configuration[4];
} s_stream_endpoint;

/* 流/连接状态 (msh bt_a2dp 可读) */
#define FT_A2DP_STATE_CLOSED    0
#define FT_A2DP_STATE_OPEN      1
#define FT_A2DP_STATE_PLAYING   2
#define FT_A2DP_STATE_PAUSED    3

static volatile int s_stream_state;
static uint16_t s_a2dp_cid;
static uint16_t s_avrcp_cid;
static uint16_t s_negotiated_rate;      /* 协商采样率 (SBC 配置事件) */
static uint8_t  s_negotiated_channels;
static uint8_t  s_negotiated_bitpool_max;
static bd_addr_t s_peer_addr;

/* 媒体通路统计 */
static rt_uint32_t s_media_packets;
static rt_uint32_t s_media_bytes;
static rt_uint32_t s_media_dropped;     /* ring 满整包丢弃计数 */
static rt_uint32_t s_media_hdr_err;     /* RTP/SBC 头解析失败计数 */
static rt_uint32_t s_stream_starts;
static rt_uint8_t  s_last_volume_pct = 0xFFU;

/* ring 控制块登记流格式并换代 (M55 consumer 监测 fmt_gen) */
static void bt_a2dp_ring_publish_format(rt_uint32_t rate)
{
    FT_ALINK->fmt_rate = rate;
    FT_ALINK->fmt_bits = 16U;
    FT_ALINK->fmt_ch   = 2U;
    __DMB();
    FT_ALINK->fmt_gen++;
    FT_ALINK_DCACHE_CLEAN(FT_ALINK_BASE, 32);
}

/* ---- HCI 事件: 记录 Classic 对端地址 (供板侧主动 connect) ---- */
static btstack_packet_callback_registration_t s_hci_event_reg;
static void bt_a2dp_hci_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size)
{
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) == HCI_EVENT_CONNECTION_COMPLETE &&
        (size > 11U) && (packet[2] == 0U) && (packet[11] == 1U))
    {
        /* Classic ACL 连接建立: [5..10] = bd_addr */
        memcpy(s_peer_addr, &packet[5], 6);
    }
}

/* ---- 媒体包 handler (btloop 上下文): RTP 头剥离 + 帧化写 ring ---- */
static void bt_a2dp_media_handler(uint8_t seid, uint8_t *packet, uint16_t size)
{
    /* 单包暂存: RTP 载荷上限按 L2CAP MTU 1024 足够 (实测高位 SBC 约 600B) */
    static rt_uint8_t s_stage[2 + 1024];
    rt_uint32_t pos, payload_len, total, written;

    (void)seid;
    if (s_stream_state != FT_A2DP_STATE_PLAYING)
    {
        return;   /* 仅在 stream started 状态产出 */
    }
    /* RTP 头 12B + 4B/CSRC; SBC 载荷前还有 1B 帧数等字段。
     * CSRC 数取首字节低 4 位 (RFC3550: V|P|X|CC) —— 曾误取高 4 位,
     * 0x80 -> CC 算成 8, 每包多跳 32B 载荷, 解码器拿到的全是错位帧
     * (卡顿/断音根因之一, 与 HCI 带宽问题叠加) */
    if (size < 13U)
    {
        s_media_hdr_err++;
        return;
    }
    pos = 12U + 4U * (packet[0] & 0x0FU);   /* CSRC 扩展 */
    if ((rt_uint32_t)size <= pos)
    {
        s_media_hdr_err++;
        return;
    }
    pos += 1U;   /* SBC codec 头 (fragmentation/starting/last/num_frames) */
    payload_len = (rt_uint32_t)size - pos;
    if (payload_len > 1024U)
    {
        s_media_hdr_err++;
        return;
    }
    /* 载荷必须以 SBC 同步字 0x9C 开头, 否则说明头解析错位 —— 丢包计数,
     * 不往 ring 塞错位数据 (错位帧会让解码器陷入长期失同步) */
    if (packet[pos] != 0x9CU)
    {
        s_media_hdr_err++;
        return;
    }

    total = payload_len + 2U;
    if (ft_alink_space(FT_ALINK) < total)
    {
        /* ring 满 (consumer 落后): 整包丢弃, 保字节流帧对齐 */
        s_media_dropped++;
        return;
    }
    s_stage[0] = (rt_uint8_t)(payload_len & 0xFFU);
    s_stage[1] = (rt_uint8_t)(payload_len >> 8);
    memcpy(s_stage + 2, packet + pos, payload_len);
    written = ft_audio_produce(s_stage, total);
    if (written == total)
    {
        s_media_packets++;
        s_media_bytes += payload_len;
    }
    else
    {
        s_media_dropped++;
    }
}

/* ---- A2DP 事件 (HCI_EVENT_A2DP_META) ---- */
static void bt_a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel,
                                        uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;

    switch (packet[2])
    {
    case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
        s_negotiated_rate = (uint16_t)a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
        s_negotiated_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(packet);
        s_negotiated_bitpool_max = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);
        rt_kprintf("[A2DP] SBC config: %u Hz, %u ch, chmode %u, blk %u, sub %u, bitpool %u-%u\n",
                   (unsigned)s_negotiated_rate, (unsigned)s_negotiated_channels,
                   (unsigned)a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet),
                   (unsigned)a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet),
                   (unsigned)a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet),
                   (unsigned)a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet),
                   (unsigned)s_negotiated_bitpool_max);
        break;

    case A2DP_SUBEVENT_STREAM_ESTABLISHED:
        if (a2dp_subevent_stream_established_get_status(packet) != ERROR_CODE_SUCCESS)
        {
            /* 失败必须清 cid —— establish 时 btstack 已预分配 cid 写入
             * s_a2dp_cid, 页面超时/对端拒收等失败路径若不清零,
             * bt_a2dp connect 会永远报 busy (实测卡 20 分钟无超时事件) */
            rt_kprintf("[A2DP] stream establish failed, status 0x%02x\n",
                       a2dp_subevent_stream_established_get_status(packet));
            s_a2dp_cid = 0;
            break;
        }
        a2dp_subevent_stream_established_get_bd_addr(packet, s_peer_addr);
        s_a2dp_cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
        s_stream_state = FT_A2DP_STATE_OPEN;
        rt_kprintf("[A2DP] stream established: %s cid 0x%02x seid %u\n",
                   bd_addr_to_str(s_peer_addr), s_a2dp_cid,
                   a2dp_subevent_stream_established_get_local_seid(packet));
        break;

    case A2DP_SUBEVENT_STREAM_STARTED:
        s_stream_state = FT_A2DP_STATE_PLAYING;
        s_media_packets = 0;
        s_media_bytes = 0;
        s_media_dropped = 0;
        s_media_hdr_err = 0;
        s_stream_starts++;
        /* 登记协商格式 + 换代 -> M55 claim sound0 开流 */
        bt_a2dp_ring_publish_format(s_negotiated_rate ? s_negotiated_rate : 48000U);
        rt_kprintf("[A2DP] stream started: %u Hz -> ring gen %lu\n",
                   (unsigned)s_negotiated_rate, (unsigned long)FT_ALINK->fmt_gen);
        break;

    case A2DP_SUBEVENT_STREAM_SUSPENDED:
        s_stream_state = FT_A2DP_STATE_PAUSED;
        bt_a2dp_ring_publish_format(0U);   /* fmt_rate=0: 流停, M55 关 sound0 */
        rt_kprintf("[A2DP] stream suspended (packets %lu, dropped %lu)\n",
                   (unsigned long)s_media_packets, (unsigned long)s_media_dropped);
        break;

    case A2DP_SUBEVENT_STREAM_RELEASED:
        s_stream_state = FT_A2DP_STATE_CLOSED;
        bt_a2dp_ring_publish_format(0U);
        rt_kprintf("[A2DP] stream released (packets %lu, bytes %lu, dropped %lu)\n",
                   (unsigned long)s_media_packets, (unsigned long)s_media_bytes,
                   (unsigned long)s_media_dropped);
        break;

    case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        s_a2dp_cid = 0;
        if (s_stream_state != FT_A2DP_STATE_CLOSED)
        {
            s_stream_state = FT_A2DP_STATE_CLOSED;
            bt_a2dp_ring_publish_format(0U);
        }
        rt_kprintf("[A2DP] signaling connection released\n");
        break;

    default:
        break;
    }
}

/* ---- AVRCP ---- */
static void bt_avrcp_packet_handler(uint8_t packet_type, uint16_t channel,
                                    uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;

    switch (packet[2])
    {
    case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED:
        if (avrcp_subevent_connection_established_get_status(packet) != ERROR_CODE_SUCCESS)
        {
            rt_kprintf("[AVRCP] connection failed, status 0x%02x\n",
                       avrcp_subevent_connection_established_get_status(packet));
            return;
        }
        s_avrcp_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
        rt_kprintf("[AVRCP] connected, cid 0x%02x\n", s_avrcp_cid);
        /* 声明支持绝对音量通知 -> 对端 SetAbsoluteVolume 才有回报 */
        avrcp_target_support_event(s_avrcp_cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
        avrcp_controller_get_supported_events(s_avrcp_cid);
        break;
    case AVRCP_SUBEVENT_CONNECTION_RELEASED:
        rt_kprintf("[AVRCP] released, cid 0x%02x\n",
                   avrcp_subevent_connection_released_get_avrcp_cid(packet));
        s_avrcp_cid = 0;
        break;
    default:
        break;
    }
}

static void bt_avrcp_target_packet_handler(uint8_t packet_type, uint16_t channel,
                                           uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;

    switch (packet[2])
    {
    case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
    {
        /* 对端 (手机/PC) 绝对音量 0-127 -> 百分比写 ring 控制块, M55 应用 */
        uint8_t vol = avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
        rt_uint32_t pct = ((rt_uint32_t)vol * 100U + 63U) / 127U;
        s_last_volume_pct = (rt_uint8_t)pct;
        FT_ALINK->volume_percent = pct;
        FT_ALINK_DCACHE_CLEAN(FT_ALINK_BASE, 32);
        rt_kprintf("[AVRCP] volume %u (%u%%)\n", (unsigned)vol, (unsigned)pct);
        break;
    }
    case AVRCP_SUBEVENT_OPERATION:
        /* 播放/暂停等 passthrough: 先记录 */
        rt_kprintf("[AVRCP] operation id 0x%02x %s\n",
                   avrcp_subevent_operation_get_operation_id(packet),
                   avrcp_subevent_operation_get_button_pressed(packet) ? "press" : "release");
        break;
    default:
        break;
    }
}

static void bt_avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel,
                                               uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    if (s_avrcp_cid == 0) return;

    /* 只记录对端播放状态变化, 验证 controller 通路 */
    if (packet[2] == AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED)
    {
        rt_kprintf("[AVRCP] playback status %u\n",
                   avrcp_subevent_notification_playback_status_changed_get_play_status(packet));
    }
}

/* ---- setup: bt_main.c 在 sdp_init 之后调用 ---- */
int bt_a2dp_sink_setup(void)
{
    avdtp_stream_endpoint_t *local_stream_endpoint;

    a2dp_sink_init();
    avrcp_init();
    avrcp_controller_init();
    avrcp_target_init();

    a2dp_sink_register_packet_handler(&bt_a2dp_sink_packet_handler);
    a2dp_sink_register_media_handler(&bt_a2dp_media_handler);

    local_stream_endpoint = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC,
        s_media_sbc_codec_capabilities, sizeof(s_media_sbc_codec_capabilities),
        s_stream_endpoint.media_sbc_codec_configuration,
        sizeof(s_stream_endpoint.media_sbc_codec_configuration));
    if (local_stream_endpoint == NULL)
    {
        rt_kprintf("[A2DP] create stream endpoint failed (pool)\n");
        return -1;
    }
    s_stream_endpoint.a2dp_local_seid = avdtp_local_seid(local_stream_endpoint);

    avrcp_register_packet_handler(&bt_avrcp_packet_handler);
    avrcp_controller_register_packet_handler(&bt_avrcp_controller_packet_handler);
    avrcp_target_register_packet_handler(&bt_avrcp_target_packet_handler);

    /* 记录 Classic 对端地址 (bt_a2dp connect 用) */
    s_hci_event_reg.callback = &bt_a2dp_hci_handler;
    hci_add_event_handler(&s_hci_event_reg);

    /* SDP 记录: A2DP Sink (扬声器) + AVRCP target + controller + PnP */
    uint8_t rc;
    memset(s_sdp_a2dp_sink_buffer, 0, sizeof(s_sdp_a2dp_sink_buffer));
    a2dp_sink_create_sdp_record(s_sdp_a2dp_sink_buffer, sdp_create_service_record_handle(),
                                AVDTP_SINK_FEATURE_MASK_SPEAKER, NULL, NULL);
    rc = sdp_register_service(s_sdp_a2dp_sink_buffer);
    if (rc != 0U) rt_kprintf("[A2DP] SDP a2dp_sink register FAILED rc=%u\n", rc);

    memset(s_sdp_avrcp_target_buffer, 0, sizeof(s_sdp_avrcp_target_buffer));
    avrcp_target_create_sdp_record(s_sdp_avrcp_target_buffer, sdp_create_service_record_handle(),
                                   1U << AVRCP_TARGET_SUPPORTED_FEATURE_CATEGORY_MONITOR_OR_AMPLIFIER,
                                   NULL, NULL);
    rc = sdp_register_service(s_sdp_avrcp_target_buffer);
    if (rc != 0U) rt_kprintf("[A2DP] SDP avrcp_target register FAILED rc=%u\n", rc);

    memset(s_sdp_avrcp_controller_buffer, 0, sizeof(s_sdp_avrcp_controller_buffer));
    avrcp_controller_create_sdp_record(s_sdp_avrcp_controller_buffer, sdp_create_service_record_handle(),
                                       1U << AVRCP_CONTROLLER_SUPPORTED_FEATURE_CATEGORY_PLAYER_OR_RECORDER,
                                       NULL, NULL);
    rc = sdp_register_service(s_sdp_avrcp_controller_buffer);
    if (rc != 0U) rt_kprintf("[A2DP] SDP avrcp_controller register FAILED rc=%u\n", rc);

    memset(s_sdp_device_id_buffer, 0, sizeof(s_sdp_device_id_buffer));
    /* PnP 记录沿用 demo 的 BlueKitchen 厂商号 (自有 VID/PID 是后续打磨项) */
    device_id_create_sdp_record(s_sdp_device_id_buffer, sdp_create_service_record_handle(),
                                DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
                                BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    rc = sdp_register_service(s_sdp_device_id_buffer);
    if (rc != 0U) rt_kprintf("[A2DP] SDP device_id register FAILED rc=%u\n", rc);

    rt_kprintf("[A2DP] sink ready (seid %u, SBC 44.1k/48k bitpool 2-53, auto-rate)\n",
               s_stream_endpoint.a2dp_local_seid);
    return 0;
}

/* 诊断: 打印 4 条已注册 SDP 记录内容 (验证 SDP 服务是否正确上桌) */
static void bt_a2dp_dump_sdp(void)
{
    rt_kprintf("[A2DP] SDP a2dp_sink record (%lu B):\n",
               (unsigned long)de_get_len(s_sdp_a2dp_sink_buffer));
    de_dump_data_element(s_sdp_a2dp_sink_buffer);
    rt_kprintf("[A2DP] SDP avrcp_target record (%lu B):\n",
               (unsigned long)de_get_len(s_sdp_avrcp_target_buffer));
    de_dump_data_element(s_sdp_avrcp_target_buffer);
    rt_kprintf("[A2DP] SDP avrcp_controller record (%lu B):\n",
               (unsigned long)de_get_len(s_sdp_avrcp_controller_buffer));
    de_dump_data_element(s_sdp_avrcp_controller_buffer);
    rt_kprintf("[A2DP] SDP device_id record (%lu B):\n",
               (unsigned long)de_get_len(s_sdp_device_id_buffer));
    de_dump_data_element(s_sdp_device_id_buffer);
}

static int bt_a2dp(int argc, char **argv)
{
    static const char *const state_names[] = { "closed", "open", "playing", "paused" };
    int state = s_stream_state;

    if (argc > 1 && strcmp(argv[1], "sdp") == 0)
    {
        bt_a2dp_dump_sdp();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "connect") == 0)
    {
        /* 板侧主动向最近连接过的 Classic 对端发起 A2DP (sink establish):
         * Windows 收到入向 AVDTP 信令会绑定音频端点并驱动 SEP 配置 */
        if (s_stream_state != FT_A2DP_STATE_CLOSED || s_a2dp_cid != 0)
        {
            rt_kprintf("[A2DP] busy, state=%s cid=0x%x\n", state_names[state & 3], s_a2dp_cid);
            return 0;
        }
        uint8_t rc = a2dp_sink_establish_stream(s_peer_addr, &s_a2dp_cid);
        rt_kprintf("[A2DP] establish -> %s rc=0x%02x cid=0x%x\n",
                   bd_addr_to_str(s_peer_addr), rc, s_a2dp_cid);
        return 0;
    }
    rt_kprintf("[A2DP] state=%s a2dp_cid=0x%x avrcp_cid=0x%x peer=%s\n",
               state_names[state & 3], s_a2dp_cid, s_avrcp_cid,
               bd_addr_to_str(s_peer_addr));
    rt_kprintf("[A2DP] negotiated: %u Hz %u ch bitpool_max=%u, starts=%lu\n",
               (unsigned)s_negotiated_rate, (unsigned)s_negotiated_channels,
               (unsigned)s_negotiated_bitpool_max, (unsigned long)s_stream_starts);
    rt_kprintf("[A2DP] media: packets=%lu bytes=%lu dropped=%lu hdr_err=%lu\n",
               (unsigned long)s_media_packets, (unsigned long)s_media_bytes,
               (unsigned long)s_media_dropped, (unsigned long)s_media_hdr_err);
    rt_kprintf("[A2DP] volume=%u%% ring: gen=%lu rate=%lu used=%lu\n",
               (unsigned)s_last_volume_pct, (unsigned long)FT_ALINK->fmt_gen,
               (unsigned long)FT_ALINK->fmt_rate,
               (unsigned long)ft_alink_used(FT_ALINK));
    return 0;
}
MSH_CMD_EXPORT_ALIAS(bt_a2dp, bt_a2dp, M4b: show A2DP sink / AVRCP / media stats);
