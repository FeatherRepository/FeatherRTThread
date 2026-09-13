/* bt_le_audio_broadcast.c - M8.2 Auracast 广播源 (M33 侧)
 *
 * 板子作为 Broadcast Source: 扩展广播 (Broadcast Audio Announcement +
 * Broadcast ID) + 周期广播 (BASE: LC3 48k/10ms 立体声, 双 BIS FL/FR)
 * + BIG (未加密) + BIS ISO 发送。
 *
 * 音频源 (首期): 板载正弦发生器 (440Hz 左 / 660Hz 右), 供接收端直接
 * 辨识声道; 后续可切 ring2 (A2DP Source 环回) 或 USB。
 * 编码: 本机 liblc3 (M33 实时编码 48k/10ms 单声道 ~1.4ms/帧 @100MHz,
 * M33 主频 400MHz, 余量充足; btloop 线程内完成)。
 *
 * 发送流程 (对齐 btstack broadcast_source_lite):
 *   BIG Created 事件 → 取 BIS handles → hci_request_bis_can_send_now_events
 *   → HCI_EVENT_BIS_CAN_SEND_NOW → 编码一帧 → hci_send_iso_packet_buffer
 *
 * msh: bt_bcast_start / bt_bcast_stop / bt_bcast_stats
 */
#include <rtthread.h>
#include <string.h>
#include <math.h>

#include "btstack_config.h"
#include "btstack_event.h"
#include "bluetooth.h"
#include "hci.h"
#include "gap.h"
#include "btstack_util.h"
#include "ble/att_server.h"
#include "ft_gatt.h"

#include "bt_le_audio.h"
#include "lc3.h"
#include "le_audio_base_builder.h"

/* ---- 广播配置: LC3 48k/10ms 立体声, 120 octets/帧, 双 BIS ---- */
#define FT_BCST_FRAME_US        10000U
#define FT_BCST_RATE_HZ         48000U
#define FT_BCST_SAMPLES         480U                    /* 每帧每声道采样数 */
#define FT_BCST_OCTETS          120U                    /* LC3 帧字节数 */
#define FT_BCST_NUM_BIS         2U                      /* FL + FR */
#define FT_BCST_SDU_INTERVAL_US 10000U
#define FT_BCST_PD_US           40000U                  /* Presentation Delay, 同官方 demo */
#define FT_BCST_MAX_LATENCY_MS  31U
#define FT_BCST_RTN             2U
#define FT_BCST_PHY             2U                      /* 2M */

/* Broadcast ID: 随机固定值 (展示用; 加密广播时再引入随机生成) */
#define FT_BCST_ID              0x123456U

/* AD 类型常量 (bluetooth_data_types.h 未入 CPPPATH 时就地定义) */
#ifndef BLUETOOTH_DATA_TYPE_SERVICE_DATA_16_BIT_UUID
#define BLUETOOTH_DATA_TYPE_SERVICE_DATA_16_BIT_UUID 0x16U
#endif
#ifndef BLUETOOTH_DATA_TYPE_BROADCAST_AUDIO_ANNOUNCEMENT
#define BLUETOOTH_DATA_TYPE_BROADCAST_AUDIO_ANNOUNCEMENT 0x1852U
#endif

/* ---- LC3 编码器 (双声道, 静态复用) ---- */
static lc3_encoder_t    s_enc[2];
static rt_uint8_t      *s_enc_mem;
static int16_t          s_pcm[FT_BCST_SAMPLES * 2U];    /* 交织 L/R */
static rt_uint32_t      s_phase;                         /* 正弦相位 (采样计数) */
static rt_bool_t        s_enc_ready;

/* ---- BIG / 广播状态 ---- */
static le_audio_big_t           s_big_storage;
static le_audio_big_params_t    s_big_params;
static le_advertising_set_t     s_adv_set;
static le_extended_advertising_parameters_t s_ext_params;
static uint8_t                  s_adv_handle;
static hci_con_handle_t         s_bis_handle[FT_BCST_NUM_BIS];
static rt_bool_t                s_active;
static rt_bool_t                s_big_created;
static btstack_packet_callback_registration_t s_hci_event_reg;

/* BASE 构造缓冲 (放 periodic advertising data) */
static le_audio_base_builder_t  s_base_builder;
static uint8_t                  s_base_buffer[64];

/* 编码输出帧缓冲 */
static rt_uint8_t s_frame_l[FT_BCST_OCTETS];
static rt_uint8_t s_frame_r[FT_BCST_OCTETS];

static btstack_timer_source_t s_request_timer;

/* 统计 */
static rt_uint32_t s_stat_iso_sent;
static rt_uint32_t s_stat_enc_frames;
static rt_uint32_t s_stat_send_busy;

/* ---- LC3 编码一帧 (左右声道 -> 各 120B) ----
 * 每帧独立: 正弦发生器相位连续, 编码器状态跨帧连续 (liblc3 要求) */
static void ft_bcst_encode_frame(void)
{
    /* 生成 10ms 交织正弦: 左 440Hz, 右 660Hz */
    for (rt_uint32_t i = 0U; i < FT_BCST_SAMPLES; i++)
    {
        double t = (double)(s_phase + i) / (double)FT_BCST_RATE_HZ;
        s_pcm[i * 2U]      = (int16_t)(9000.0 * sin(2.0 * 3.14159265358979 * 440.0 * t));
        s_pcm[i * 2U + 1U] = (int16_t)(9000.0 * sin(2.0 * 3.14159265358979 * 660.0 * t));
    }
    s_phase += FT_BCST_SAMPLES;
    lc3_encode(s_enc[0], LC3_PCM_FORMAT_S16, s_pcm, 2,
               FT_BCST_OCTETS, s_frame_l);
    lc3_encode(s_enc[1], LC3_PCM_FORMAT_S16, s_pcm + 1, 2,
               FT_BCST_OCTETS, s_frame_r);
    s_stat_enc_frames++;
}

/* ---- 发送一帧到指定 BIS (complete SDU, 无时间戳) ---- */
static void ft_bcst_send_frame(rt_uint8_t bis_index)
{
    hci_con_handle_t handle = s_bis_handle[bis_index];
    const rt_uint8_t *frame = (bis_index == 0U) ? s_frame_l : s_frame_r;

    hci_reserve_packet_buffer();
    uint8_t *buffer = hci_get_outgoing_packet_buffer();
    /* complete SDU, no TS: [handle|PB=2][len][seq][sdu_len][payload] */
    little_endian_store_16(buffer, 0, ((uint16_t)handle) | (2U << 12));
    little_endian_store_16(buffer, 2, 4U + FT_BCST_OCTETS);
    static rt_uint16_t seq[FT_BCST_NUM_BIS];
    little_endian_store_16(buffer, 4, seq[bis_index]);
    little_endian_store_16(buffer, 6, FT_BCST_OCTETS);
    memcpy(&buffer[8], frame, FT_BCST_OCTETS);
    seq[bis_index]++;
    hci_send_iso_packet_buffer(8U + FT_BCST_OCTETS);
    s_stat_iso_sent++;
}

/* ---- BASE 构造: 1 subgroup (LC3), 2 BIS (FL/FR) ----
 * 对齐官方 demo: subgroup 级 = freq/duration/octets (BAP Table 3.2 必选),
 * 位置信息只在 BIS 级; metadata = Streaming Audio Contexts (tag 0x02) = MEDIA */
static void ft_bcst_build_base(void)
{
    static const uint8_t subgroup_cfg[] = {
        0x03, 0x01, 0x80, 0x02,                 /* Sampling freq: 48000 */
        0x02, 0x02, 0x02,                       /* Frame duration: 10ms */
        0x03, 0x04, 0x78, 0x00,                 /* Octets per codec frame: 120 */
    };
    static const uint8_t metadata[] = {
        0x03, 0x02, 0x04, 0x00,                 /* Streaming contexts: MEDIA */
    };
    /* BIS 级 codec configuration (LTV): 单声道各一侧 */
    static const uint8_t bis_l_cfg[] = {
        0x05, 0x04, 0x01, 0x00, 0x00, 0x00,     /* Channel allocation: FL */
    };
    static const uint8_t bis_r_cfg[] = {
        0x05, 0x04, 0x02, 0x00, 0x00, 0x00,     /* Channel allocation: FR */
    };
    static const uint8_t codec_id[5] = { 0x06, 0x00, 0x00, 0x00, 0x00 }; /* LC3 */

    le_audio_base_builder_init(&s_base_builder, s_base_buffer,
                               sizeof(s_base_buffer), FT_BCST_PD_US);
    le_audio_base_builder_add_subgroup(&s_base_builder, codec_id,
                                       sizeof(subgroup_cfg), subgroup_cfg,
                                       sizeof(metadata), metadata);
    le_audio_base_builder_add_bis(&s_base_builder, 1,
                                  sizeof(bis_l_cfg), bis_l_cfg);
    le_audio_base_builder_add_bis(&s_base_builder, 2,
                                  sizeof(bis_r_cfg), bis_r_cfg);
}

/* ---- 扩展广播数据 (对齐官方 PBP_Source demo):
 * AD1 Broadcast Audio Announcement (0x1852) + Broadcast ID
 * AD2 Public Broadcast Announcement (0x1856): features + metadata
 * AD3 Appearance (0x0A00 Generic Audio Source)
 * AD4 Broadcast Name (0x30) -- Auracast UI 显示名
 * AD5 Complete Local Name */
static uint8_t s_ext_adv_data[] = {
    6, 0x16, 0x52, 0x18,
    (FT_BCST_ID >> 16) & 0xFF, (FT_BCST_ID >> 8) & 0xFF, FT_BCST_ID & 0xFF,
    6, 0x16, 0x56, 0x18, 0x00, 0x00,        /* features: 未加密/无质量位; metadata 空 */
    4, 0x19, 0x00, 0x0A,                    /* Appearance 0x0A00 */
    13, 0x30, 'F', 'T', '-', 'B', 'c', 'a', 's', 't', '-', '0', '1',
    12, 0x09, 'F', 'e', 'a', 't', 'h', 'e', 'r', 'T', 'a', 'l', 'k',
};

static const le_periodic_advertising_parameters_t s_periodic_params = {
    .periodic_advertising_interval_min = 64,   /* 80ms, 同官方 demo */
    .periodic_advertising_interval_max = 64,
    .periodic_advertising_properties   = 0,
};

/* ---- 启动广播 (WORKING 后由外部调, 或 msh bt_bcast_start) ---- */
static void ft_bcst_setup_advertising(void)
{
    bd_addr_t local_addr;
    bd_addr_type_t local_type;
    gap_le_get_own_address(&local_type, local_addr);

    memset(&s_ext_params, 0, sizeof(s_ext_params));
    s_ext_params.own_address_type = local_type;
    /* event properties: connectable=0 scannable=0 legacy=0 anonymous=0
     * (non-connectable non-scannable extended advertising) */
    s_ext_params.advertising_event_properties = 0;
    s_ext_params.primary_advertising_interval_min = 0xA0;  /* 62.5ms, 同官方 demo */
    s_ext_params.primary_advertising_interval_max = 0xA0;
    s_ext_params.primary_advertising_channel_map = 7;
    s_ext_params.peer_address_type = 0;
    memset(&s_ext_params.peer_address, 0, 6);
    s_ext_params.advertising_filter_policy = 0;
    s_ext_params.advertising_tx_power = 0x7F;   /* host default */
    s_ext_params.primary_advertising_phy = 1;
    s_ext_params.secondary_advertising_max_skip = 0;
    s_ext_params.secondary_advertising_phy = 1;
    s_ext_params.advertising_sid = 2;   /* Broadcast SID */
    gap_extended_advertising_setup(&s_adv_set, &s_ext_params, &s_adv_handle);
    if (local_type == BD_ADDR_TYPE_LE_RANDOM)
    {
        gap_extended_advertising_set_random_address(s_adv_handle, local_addr);
    }
    gap_extended_advertising_set_adv_data(s_adv_handle,
                                          sizeof(s_ext_adv_data), s_ext_adv_data);

    ft_bcst_build_base();
    gap_periodic_advertising_set_params(s_adv_handle, &s_periodic_params);
    /* periodic data = BAA AD (0x1851): LEVEL1 PD + LEVEL2 BASE,
     * builder 已输出完整 AD 结构 ([len][0x16][uuid][pd][base]), 直接使用 */
    {
        uint16_t base_len = le_audio_base_builder_get_ad_data_size(&s_base_builder);
        gap_periodic_advertising_set_data(s_adv_handle, base_len, s_base_buffer);
    }
    gap_periodic_advertising_start(s_adv_handle, 0);
    gap_extended_advertising_start(s_adv_handle, 0, 0);
}

static void ft_bcst_setup_big(void)
{
    memset(&s_big_params, 0, sizeof(s_big_params));
    s_big_params.big_handle = 0;
    s_big_params.advertising_handle = s_adv_handle;
    s_big_params.num_bis = FT_BCST_NUM_BIS;
    s_big_params.max_sdu = FT_BCST_OCTETS;
    s_big_params.max_transport_latency_ms = FT_BCST_MAX_LATENCY_MS;
    s_big_params.rtn = FT_BCST_RTN;
    s_big_params.phy = FT_BCST_PHY;
    s_big_params.packing = 0;
    s_big_params.sdu_interval_us = FT_BCST_SDU_INTERVAL_US;
    s_big_params.framing = 0;
    s_big_params.encryption = 0;
    memset(s_big_params.broadcast_code, 0, 16);
    (void)gap_big_create(&s_big_storage, &s_big_params);
    s_big_created = RT_TRUE;
}

int ft_le_audio_broadcast_start(void)
{
    if (s_active) return -1;

    /* LC3 编码器一次分配复用 */
    if (s_enc_mem == RT_NULL)
    {
        unsigned mem_sz = (lc3_encoder_size(FT_BCST_FRAME_US, FT_BCST_RATE_HZ) + 3U) & ~3U;
        s_enc_mem = (rt_uint8_t *)rt_malloc(mem_sz * 2U);
        if (s_enc_mem == RT_NULL)
        {
            rt_kprintf("[BCST] enc mem alloc failed\\n");
            return -2;
        }
        s_enc[0] = lc3_setup_encoder(FT_BCST_FRAME_US, FT_BCST_RATE_HZ,
                                     FT_BCST_RATE_HZ, &s_enc_mem[0]);
        s_enc[1] = lc3_setup_encoder(FT_BCST_FRAME_US, FT_BCST_RATE_HZ,
                                     FT_BCST_RATE_HZ, &s_enc_mem[mem_sz]);
        if ((s_enc[0] == RT_NULL) || (s_enc[1] == RT_NULL))
        {
            rt_free(s_enc_mem);
            s_enc_mem = RT_NULL;
            return -3;
        }
    }
    s_enc_ready = RT_TRUE;
    s_phase = 0U;

    ft_bcst_setup_advertising();
    ft_bcst_setup_big();
    s_active = RT_TRUE;
    rt_kprintf("[BCST] start: BIG handle %u, LC3 48k/10ms stereo, ID 0x%06x\\n",
               s_big_params.big_handle, FT_BCST_ID);
    feathertalk_ipc_send_event(84U);
    return 0;
}

int ft_le_audio_broadcast_stop(void)
{
    if (!s_active) return -1;
    (void)gap_big_terminate(s_big_params.big_handle);
    gap_periodic_advertising_stop(s_adv_handle);
    gap_extended_advertising_stop(s_adv_handle);
    s_active = RT_FALSE;
    rt_kprintf("[BCST] stop: sent=%lu enc=%lu busy=%lu\\n",
               (unsigned long)s_stat_iso_sent, (unsigned long)s_stat_enc_frames,
               (unsigned long)s_stat_send_busy);
    feathertalk_ipc_send_event(85U);
    return 0;
}

/* ---- HCI 事件 ---- */
extern volatile uint16_t g_bt_evt_log[12][2];
extern volatile uint8_t  g_bt_evt_log_pos;
static void ft_bcst_evt_log(uint16_t a, uint16_t b)
{
    uint8_t i = g_bt_evt_log_pos % 12U;
    g_bt_evt_log[i][0] = a;
    g_bt_evt_log[i][1] = b;
    g_bt_evt_log_pos++;
}

/* BIG_CREATED 回调里 packet buffer 被事件占用, 直接 request 会在
 * hci_iso_notify 的 buffer 检查处 return, 第一包永远发不出 (实测)。
 * 延迟到下一拍再 request */
extern volatile uint8_t  g_ft_big_evt_raw[24];
extern volatile uint8_t  g_ft_big_evt_raw_len;
static void ft_bcst_request_send(btstack_timer_source_t *ts)
{
    (void)ts;
    /* 一次性 dump LE Create BIG Complete 原始字节: 用于确定本控制器
     * 事件布局中 Sync_Delay/Transport_Latency/PHY/NSE/BN/PTO/IRC/
     * Max_PDU/ISO_Interval 的实际偏移 (构建 LEVEL 3 BIGInfo 用) */
    rt_kprintf("[BCST] big evt raw (%u):", g_ft_big_evt_raw_len);
    for (uint8_t i = 3U; i < g_ft_big_evt_raw_len; i++)
    {
        rt_kprintf(" %02x", g_ft_big_evt_raw[i]);
    }
    rt_kprintf("\n");
    hci_request_bis_can_send_now_events(s_big_params.big_handle);
    rt_kprintf("[BCST] request bis can-send (deferred)\n");
}

static void ft_bcst_hci_handler(rt_uint8_t packet_type, rt_uint16_t channel,
                                rt_uint8_t *packet, rt_uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (packet[0])
    {
    case HCI_EVENT_META_GAP:
        ft_bcst_evt_log(0xB1, hci_event_gap_meta_get_subevent_code(packet));
        if (hci_event_gap_meta_get_subevent_code(packet) == GAP_SUBEVENT_BIG_CREATED)
        {
            for (rt_uint32_t i = 0U; i < FT_BCST_NUM_BIS; i++)
            {
                s_bis_handle[i] =
                    gap_subevent_big_created_get_bis_con_handles(packet, i);
            }
            rt_kprintf("[BCST] BIG created: bis=0x%04x/0x%04x\\n",
                       s_bis_handle[0], s_bis_handle[1]);
            /* 延迟 request: 回调上下文里 packet buffer 被事件占用 */
            s_request_timer.process = &ft_bcst_request_send;
            btstack_run_loop_set_timer(&s_request_timer, 10);
            btstack_run_loop_add_timer(&s_request_timer);
        }
        else if (hci_event_gap_meta_get_subevent_code(packet) == GAP_SUBEVENT_BIG_TERMINATED)
        {
            rt_kprintf("[BCST] BIG terminated\\n");
        }
        break;

    case HCI_EVENT_BIS_CAN_SEND_NOW:
    {
        ft_bcst_evt_log(0xB2, 0);
        rt_uint8_t bis_index = hci_event_bis_can_send_now_get_bis_index(packet);
        if (bis_index < FT_BCST_NUM_BIS && s_enc_ready)
        {
            /* 每帧只在 BIS0 事件时编码一次, BIS0/BIS1 共用同一批采样 */
            if (bis_index == 0U)
            {
                ft_bcst_encode_frame();
            }
            ft_bcst_send_frame(bis_index);
        }
        hci_request_bis_can_send_now_events(s_big_params.big_handle);
        break;
    }
    default:
        break;
    }
}

/* ---- 初始化 (bt_main 建栈后调用) ---- */
void ft_le_audio_broadcast_init(void)
{
    s_hci_event_reg.callback = &ft_bcst_hci_handler;
    hci_add_event_handler(&s_hci_event_reg);
    /* 关键: hci_register_iso_packet_handler 是单值注册, BIS_CAN_SEND_NOW
     * 事件经此派发; 不注册则事件落入 unicast 的 handler 被静默丢弃
     * (实测 iso_sent=0 根因)。unicast/broadcast 不同时活跃, 覆盖安全 */
    hci_register_iso_packet_handler(&ft_bcst_hci_handler);
    rt_kprintf("[BCST] broadcast source ready (msh bt_bcast_start)\n");
}

/* ---- msh ---- */
static int bt_bcast_start(int argc, char **argv)
{
    (void)argc; (void)argv;
    return ft_le_audio_broadcast_start();
}
MSH_CMD_EXPORT_ALIAS(bt_bcast_start, bt_bcast_start,
                     M8.2: start Auracast broadcast source);

static int bt_bcast_stop(int argc, char **argv)
{
    (void)argc; (void)argv;
    return ft_le_audio_broadcast_stop();
}
MSH_CMD_EXPORT_ALIAS(bt_bcast_stop, bt_bcast_stop,
                     M8.2: stop Auracast broadcast source);

static int bt_bcast_stats(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern volatile uint16_t g_bt_evt_log[12][2];
    extern volatile uint8_t  g_bt_evt_log_pos;
    rt_kprintf("[BCST] evt log (a b): ");
    for (int i = 0; i < 12; i++)
    {
        uint8_t idx = (uint8_t)(g_bt_evt_log_pos - 1U - i) % 12U;
        rt_kprintf("%04x/%04x ", g_bt_evt_log[idx][0], g_bt_evt_log[idx][1]);
    }
    rt_kprintf("\n");
    rt_kprintf("[BCST] active=%d big_created=%d bis=0x%04x/0x%04x\\n",
               s_active, s_big_created, s_bis_handle[0], s_bis_handle[1]);
    rt_kprintf("[BCST] iso_sent=%lu enc_frames=%lu send_busy=%lu\\n",
               (unsigned long)s_stat_iso_sent, (unsigned long)s_stat_enc_frames,
               (unsigned long)s_stat_send_busy);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(bt_bcast_stats, bt_bcast_stats,
                     M8.2: broadcast source stats);
