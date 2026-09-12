/* bt_le_audio_unicast.c - M8.1 LE Audio BAP Unicast Server (M33 侧)
 *
 * 在自研 btstack 上实现与 LE Audio 手机互通的最小 Unicast Server:
 *   PACS (静态能力, GATT DB 内置值) + ASCS (2 个 Sink ASE 状态机 + 控制点)
 *   + CIS acceptor (LE CIS Request -> gap_cis_accept) + ISO SDU 收流
 *   -> A0 跨核 ring (帧化: 2B LE len + 1B 声道 + LC3 帧)。
 *
 * btstack 在树版本没有 ASCS server 组件 (仅 lite demo + LTV 工具),
 * Infineon 官方参考 (bap_unicast.c) 是 WICED 栈实现, 只作架构参考;
 * 本文件的状态机/字节序/错误码按 ASCS 1.0.1 + BAP 1.0.1 实现并通过
 * PC 端 bleak GATT 验收 (无 LE Audio 手机亦可验到 QoS Configure 为止)。
 *
 * 配置面: 仅 Sink (收流放音), 双 ASE 各承载单声道 LC3 (手机约定
 * ASE1=FL/ASE2=FR, 实际以 Codec Config 的 Channel Allocation LTV 为准),
 * 48 kHz / 10 ms / 30..120 octets (PACS 静态声明)。
 * 数据面: ISO -> ring 恒 48k/16/2 消费 (A0 契约不变), LC3 解码在 M55。
 */
#include <rtthread.h>
#include <string.h>

#include "btstack_config.h"
#include "btstack_event.h"
#include "bluetooth.h"
#include "hci.h"
#include "gap.h"
#include "ble/att_server.h"
#include "ft_gatt.h"

#include <feathertalk/audio_link.h>
#include "bt_le_audio.h"

/* ---- 常量 (ASCS 1.0.1 / BAP 1.0.1) ---- */
#define FT_LE_OP_CONFIG_CODEC       0x01U
#define FT_LE_OP_CONFIG_QOS         0x02U
#define FT_LE_OP_ENABLE             0x03U
#define FT_LE_OP_RECV_START_READY   0x04U
#define FT_LE_OP_DISABLE            0x05U
#define FT_LE_OP_RECV_STOP_READY    0x06U
#define FT_LE_OP_UPDATE_METADATA    0x07U
#define FT_LE_OP_RELEASE            0x08U

/* ASE Control Point 应答错误码 (随 ASE State Changed 通知携带) */
#define FT_LE_ERR_SUCCESS           0x00U
#define FT_LE_ERR_UNSUPPORTED_OP    0x01U
#define FT_LE_ERR_INVALID_LEN       0x02U
#define FT_LE_ERR_INVALID_ASE_ID    0x03U
#define FT_LE_ERR_INVALID_ASE_STATE 0x04U
#define FT_LE_ERR_INVALID_DIRECTION 0x05U

/* LC3 codec id (BT Audio: LC3 = 0x06) */
#define FT_LE_CODEC_LC3             0x06U

#define FT_LE_ASE_NUM       2U
#define FT_LE_CODEC_CFG_MAX 32U
#define FT_LE_META_MAX      16U

/* LC3 帧参数 LTV tag (BAP Codec Specific Configuration) */
#define FT_LE_LTV_FREQ          0x01U
#define FT_LE_LTV_FRAME_DUR     0x02U
#define FT_LE_LTV_CHAN_ALLOC    0x04U
#define FT_LE_LTV_OCTETS        0x04U   /* QoS LTV 空间无冲突, 仅 Codec 用下表 */

/* ---- Sink ASE 状态机 ---- */
typedef enum
{
    FT_ASE_IDLE = 0U,
    FT_ASE_CODEC_CONFIGURED = 1U,
    FT_ASE_QOS_CONFIGURED = 2U,
    FT_ASE_ENABLING = 3U,
    FT_ASE_STREAMING = 4U,
    FT_ASE_DISABLING = 5U,
    FT_ASE_RELEASING = 6U,
} ft_ase_state_t;

typedef struct
{
    rt_uint8_t       ase_id;
    ft_ase_state_t   state;
    rt_uint8_t       codec_cfg[FT_LE_CODEC_CFG_MAX];  /* Codec Config LTVs */
    rt_uint8_t       codec_cfg_len;
    rt_uint8_t       metadata[FT_LE_META_MAX];
    rt_uint8_t       metadata_len;
    rt_uint8_t       cig_id;
    rt_uint8_t       cis_id;
    rt_uint32_t      sdu_interval_us;
    rt_uint16_t      max_sdu;
    rt_uint16_t      max_latency;
    rt_uint32_t      pres_delay_us;
    rt_uint8_t       chan_alloc;    /* bit0=FL bit1=FR (LTV tag 0x04) */
    hci_con_handle_t cis_handle;    /* HCI_CON_HANDLE_INVALID = 未建 */
    rt_uint8_t       notify_on;     /* CCCD 已订阅 */
    rt_uint8_t       established;
} ft_ase_t;

static ft_ase_t        s_ase[FT_LE_ASE_NUM];
static hci_con_handle_t s_acl_handle = HCI_CON_HANDLE_INVALID;
static rt_uint8_t      s_streaming;        /* ring 数据面已开 (任一 ASE Streaming) */
static btstack_packet_callback_registration_t s_hci_event_reg;

/* 统计 (msh bt_le_stats) */
static rt_uint32_t s_stat_iso_sdus;
static rt_uint32_t s_stat_iso_dropped;   /* ring 满丢弃 */
static rt_uint32_t s_stat_frames;        /* 已 produce 的 LC3 帧 */
static rt_uint32_t s_stat_op_err;        /* 控制点拒单计数 */

/* ---- ring 数据面 (对齐 bt_a2dp_ring_publish_format 的换代语义) ----
 * flags bit0 = LE Audio LC3 帧模式 (M55 consumer 据此分派解码插件) */
static void ft_le_ring_start(void)
{
    FT_ALINK->fmt_rate = 48000U;
    FT_ALINK->fmt_bits = 16U;
    FT_ALINK->fmt_ch   = 2U;
    FT_ALINK->flags    = FT_ALINK_FLAGS_MODE_LE_AUDIO;
    __DMB();
    FT_ALINK->fmt_gen++;
    FT_ALINK_DCACHE_CLEAN(FT_ALINK_BASE, 32);
    s_streaming = 1U;
    rt_kprintf("[LEA] ring start (LC3 48k/10ms, 2xASE)\n");
    feathertalk_ipc_send_event(80U);
}

static void ft_le_ring_stop(void)
{
    FT_ALINK->fmt_rate = 0U;
    FT_ALINK->flags    = 0U;
    __DMB();
    FT_ALINK->fmt_gen++;
    FT_ALINK_DCACHE_CLEAN(FT_ALINK_BASE, 32);
    s_streaming = 0U;
    rt_kprintf("[LEA] ring stop: frames=%lu drop=%lu\n",
               (unsigned long)s_stat_frames, (unsigned long)s_stat_iso_dropped);
    feathertalk_ipc_send_event(81U);
}

static void ft_le_streaming_refresh(void)
{
    rt_uint8_t any = 0U;
    for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
    {
        if (s_ase[i].state == FT_ASE_STREAMING) any = 1U;
    }
    if (any && !s_streaming)
    {
        ft_le_ring_start();
    }
    else if (!any && s_streaming)
    {
        ft_le_ring_stop();
    }
}

/* ---- ASE State 值编码 (ASCS 读取/通知同构) ---- */
static const uint16_t s_ase_value_handle[FT_LE_ASE_NUM] = {
    ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_SINK_ASE_01_VALUE_HANDLE,
    ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_SINK_ASE_02_VALUE_HANDLE,
};

static uint16_t ft_le_ase_encode(const ft_ase_t *ase, uint8_t *out)
{
    uint16_t pos = 0U;
    out[pos++] = ase->ase_id;
    out[pos++] = (uint8_t)ase->state;
    switch (ase->state)
    {
    case FT_ASE_CODEC_CONFIGURED:
        /* Codec ID (5B: format/company/vendor) + Codec Specific Config LTVs */
        out[pos++] = FT_LE_CODEC_LC3;
        out[pos++] = 0x00U; out[pos++] = 0x00U;   /* company id */
        out[pos++] = 0x00U; out[pos++] = 0x00U;   /* vendor codec id */
        out[pos++] = ase->codec_cfg_len;
        memcpy(&out[pos], ase->codec_cfg, ase->codec_cfg_len);
        pos += ase->codec_cfg_len;
        break;
    case FT_ASE_QOS_CONFIGURED:
        out[pos++] = (uint8_t)(ase->sdu_interval_us & 0xFF);
        out[pos++] = (uint8_t)((ase->sdu_interval_us >> 8) & 0xFF);
        out[pos++] = (uint8_t)((ase->sdu_interval_us >> 16) & 0xFF);
        out[pos++] = 0x00U;   /* framing: unframed */
        out[pos++] = 0x02U;   /* PHY: 2M */
        out[pos++] = (uint8_t)(ase->max_sdu & 0xFFU);
        out[pos++] = (uint8_t)(ase->max_sdu >> 8);
        out[pos++] = 0x02U;   /* retransmission number (RTN) */
        out[pos++] = (uint8_t)(ase->max_latency & 0xFFU);
        out[pos++] = (uint8_t)(ase->max_latency >> 8);
        out[pos++] = (uint8_t)(ase->pres_delay_us & 0xFFU);
        out[pos++] = (uint8_t)((ase->pres_delay_us >> 8) & 0xFFU);
        out[pos++] = (uint8_t)((ase->pres_delay_us >> 16) & 0xFFU);
        break;
    case FT_ASE_ENABLING:
    case FT_ASE_STREAMING:
    case FT_ASE_DISABLING:
        out[pos++] = ase->metadata_len;
        memcpy(&out[pos], ase->metadata, ase->metadata_len);
        pos += ase->metadata_len;
        break;
    default:
        break;   /* IDLE / RELEASING: 无附加参数 */
    }
    return pos;
}

static void ft_le_ase_notify(ft_ase_t *ase, uint8_t err_code)
{
    uint8_t buf[8U + FT_LE_CODEC_CFG_MAX];

    if ((s_acl_handle == HCI_CON_HANDLE_INVALID) || (ase->notify_on == 0U))
    {
        return;
    }
    uint16_t len = ft_le_ase_encode(ase, buf);
    if (err_code != FT_LE_ERR_SUCCESS)
    {
        /* 错误通知: [ASE_ID, 当前 state, Error Code] (无状态参数) */
        buf[2] = err_code;
        len = 3U;
    }
    (void)att_server_notify(s_acl_handle, s_ase_value_handle[ase->ase_id - 1U],
                            buf, len);
}

static void ft_le_ase_reset(ft_ase_t *ase)
{
    /* 关键: 保留 ase_id —— memset 会清零 ID, 清零后的 ASE 永远无法再被
     * 手机匹配 (INVALID_ASE_ID), 一次断链后整台设备即不可用 (实测) */
    rt_uint8_t keep_id = ase->ase_id;
    memset(ase, 0, sizeof(*ase));
    ase->ase_id = keep_id;
    ase->cis_handle = HCI_CON_HANDLE_INVALID;
    ase->state = FT_ASE_IDLE;
}

static ft_ase_t *ft_le_ase_by_id(rt_uint8_t ase_id)
{
    for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
    {
        if (s_ase[i].ase_id == ase_id) return &s_ase[i];
    }
    return RT_NULL;
}

/* ---- Codec Config LTV 解析: 提取声道分配 (ASE->L/R 映射) ---- */
static void ft_le_parse_codec_cfg(ft_ase_t *ase)
{
    const rt_uint8_t *p = ase->codec_cfg;
    rt_uint32_t len = ase->codec_cfg_len;
    ase->chan_alloc = (ase->ase_id == 1U) ? 0x01U : 0x02U;   /* 默认 FL/FR */
    while (len >= 2U)
    {
        rt_uint8_t ltv_len = p[0];
        rt_uint8_t tag = p[1];
        if ((ltv_len < 1U) || (ltv_len > len - 1U)) break;
        if ((tag == FT_LE_LTV_CHAN_ALLOC) && (ltv_len >= 5U))
        {
            rt_uint32_t mask = (rt_uint32_t)p[2] | ((rt_uint32_t)p[3] << 8) |
                               ((rt_uint32_t)p[4] << 16) | ((rt_uint32_t)p[5] << 24);
            if (mask & 0x01U)      ase->chan_alloc = 0x01U;  /* Front Left */
            else if (mask & 0x02U) ase->chan_alloc = 0x02U;  /* Front Right */
        }
        p += ltv_len + 1U;
        len -= ltv_len + 1U;
    }
}

/* ---- ASE Control Point 操作分发 ----
 * 每个操作按 BAP 校验 ASE 状态后迁移并通知; 错误经通知携带错误码。
 * 一次写只取第一个 ASE 处理 (手机实际逐 ASE 发), 简化但合规。 */
static void ft_le_handle_codec_config(const rt_uint8_t *p, rt_uint32_t len)
{
    if (len < 9U) { s_stat_op_err++; return; }
    ft_ase_t *ase = ft_le_ase_by_id(p[0]);
    if (ase == RT_NULL)
    {
        s_stat_op_err++;
        return;
    }
    rt_uint8_t cfg_len = p[8];
    if ((rt_uint32_t)(9U + cfg_len) > len || cfg_len > FT_LE_CODEC_CFG_MAX)
    {
        ft_le_ase_notify(ase, FT_LE_ERR_INVALID_LEN);
        s_stat_op_err++;
        return;
    }
    if ((ase->state != FT_ASE_IDLE) && (ase->state != FT_ASE_CODEC_CONFIGURED))
    {
        ft_le_ase_notify(ase, FT_LE_ERR_INVALID_ASE_STATE);
        s_stat_op_err++;
        return;
    }
    if (p[3] != FT_LE_CODEC_LC3)   /* codec id[0] = coding format (p[3..7]) */
    {
        ft_le_ase_notify(ase, FT_LE_ERR_INVALID_ASE_STATE);
        s_stat_op_err++;
        return;
    }
    memcpy(ase->codec_cfg, &p[9], cfg_len);
    ase->codec_cfg_len = cfg_len;
    ft_le_parse_codec_cfg(ase);
    ase->state = FT_ASE_CODEC_CONFIGURED;
    ft_le_ase_notify(ase, FT_LE_ERR_SUCCESS);
    rt_kprintf("[LEA] ASE%u codec cfg (%u B, chan=0x%02x)\n",
               ase->ase_id, cfg_len, ase->chan_alloc);
}

static void ft_le_handle_qos_config(const rt_uint8_t *p, rt_uint32_t len)
{
    /* num_ases + N * 19B: ASE_ID,CIG_ID,CIS_ID,SDUint(3),framing,phy,
     * max_sdu(2),rtn,latency(2),pd_min(3),pd_max(3) */
    if (len < 1U) { s_stat_op_err++; return; }
    rt_uint8_t n = p[0];
    if ((rt_uint32_t)(1U + n * 19U) > len)
    {
        s_stat_op_err++;
        return;
    }
    const rt_uint8_t *q = &p[1];
    for (rt_uint8_t i = 0U; i < n; i++)
    {
        ft_ase_t *ase = ft_le_ase_by_id(q[0]);
        if (ase == RT_NULL ||
            (ase->state != FT_ASE_CODEC_CONFIGURED && ase->state != FT_ASE_QOS_CONFIGURED))
        {
            if (ase != RT_NULL) ft_le_ase_notify(ase, FT_LE_ERR_INVALID_ASE_STATE);
            s_stat_op_err++;
            return;
        }
        ase->cig_id = q[1];
        ase->cis_id = q[2];
        ase->sdu_interval_us = (rt_uint32_t)q[3] | ((rt_uint32_t)q[4] << 8) |
                               ((rt_uint32_t)q[5] << 16);
        ase->max_sdu = (rt_uint16_t)q[8] | ((rt_uint16_t)q[9] << 8);
        ase->max_latency = (rt_uint16_t)q[11] | ((rt_uint16_t)q[12] << 8);
        ase->pres_delay_us = (rt_uint32_t)q[16] | ((rt_uint32_t)q[17] << 8) |
                             ((rt_uint32_t)q[18] << 16);
        ase->state = FT_ASE_QOS_CONFIGURED;
        ft_le_ase_notify(ase, FT_LE_ERR_SUCCESS);
        rt_kprintf("[LEA] ASE%u qos: cig=%u cis=%u sdu_int=%lu us max_sdu=%u\n",
                   ase->ase_id, ase->cig_id, ase->cis_id,
                   (unsigned long)ase->sdu_interval_us, ase->max_sdu);
        q += 19U;
    }
}

static void ft_le_handle_enable(const rt_uint8_t *p, rt_uint32_t len)
{
    if (len < 2U) { s_stat_op_err++; return; }
    rt_uint8_t n = p[0];
    const rt_uint8_t *q = &p[1];
    for (rt_uint8_t i = 0U; i < n; i++)
    {
        if ((rt_uint32_t)(q - p) + 2U > len) { s_stat_op_err++; return; }
        ft_ase_t *ase = ft_le_ase_by_id(q[0]);
        rt_uint8_t meta_len = q[1];
        if (ase == RT_NULL || ase->state != FT_ASE_QOS_CONFIGURED)
        {
            if (ase != RT_NULL) ft_le_ase_notify(ase, FT_LE_ERR_INVALID_ASE_STATE);
            s_stat_op_err++;
            return;
        }
        if ((rt_uint32_t)(q - p) + 2U + meta_len > len ||
            meta_len > FT_LE_META_MAX)
        {
            ft_le_ase_notify(ase, FT_LE_ERR_INVALID_LEN);
            s_stat_op_err++;
            return;
        }
        memcpy(ase->metadata, &q[2], meta_len);
        ase->metadata_len = meta_len;
        ase->state = FT_ASE_ENABLING;
        ft_le_ase_notify(ase, FT_LE_ERR_SUCCESS);
        /* Sink ASE: 服务端就绪即转 Streaming (Receiver Start Ready 仅用于
         * Source ASE; 本配置无 Source), 手机随后开始投 ISO SDU */
        ase->state = FT_ASE_STREAMING;
        ft_le_ase_notify(ase, FT_LE_ERR_SUCCESS);
        q += 2U + meta_len;
    }
    ft_le_streaming_refresh();
}

static void ft_le_handle_metadata_update(const rt_uint8_t *p, rt_uint32_t len)
{
    if (len < 2U) { s_stat_op_err++; return; }
    rt_uint8_t n = p[0];
    const rt_uint8_t *q = &p[1];
    for (rt_uint8_t i = 0U; i < n; i++)
    {
        if ((rt_uint32_t)(q - p) + 2U > len) { s_stat_op_err++; return; }
        ft_ase_t *ase = ft_le_ase_by_id(q[0]);
        rt_uint8_t meta_len = q[1];
        if (ase == RT_NULL ||
            (ase->state != FT_ASE_ENABLING && ase->state != FT_ASE_STREAMING))
        {
            if (ase != RT_NULL) ft_le_ase_notify(ase, FT_LE_ERR_INVALID_ASE_STATE);
            s_stat_op_err++;
            return;
        }
        if ((rt_uint32_t)(q - p) + 2U + meta_len > len ||
            meta_len > FT_LE_META_MAX)
        {
            ft_le_ase_notify(ase, FT_LE_ERR_INVALID_LEN);
            s_stat_op_err++;
            return;
        }
        memcpy(ase->metadata, &q[2], meta_len);
        ase->metadata_len = meta_len;
        ft_le_ase_notify(ase, FT_LE_ERR_SUCCESS);
        q += 2U + meta_len;
    }
}

static void ft_le_handle_release(const rt_uint8_t *p, rt_uint32_t len)
{
    if (len < 1U) { s_stat_op_err++; return; }
    rt_uint8_t n = p[0];
    if ((rt_uint32_t)(1U + n) > len) { s_stat_op_err++; return; }
    for (rt_uint8_t i = 0U; i < n; i++)
    {
        ft_ase_t *ase = ft_le_ase_by_id(p[1U + i]);
        if (ase == RT_NULL || ase->state == FT_ASE_IDLE ||
            ase->state == FT_ASE_RELEASING)
        {
            if (ase != RT_NULL) ft_le_ase_notify(ase, FT_LE_ERR_INVALID_ASE_STATE);
            s_stat_op_err++;
            continue;
        }
        ase->state = FT_ASE_RELEASING;
        ft_le_ase_notify(ase, FT_LE_ERR_SUCCESS);
        if (ase->cis_handle != HCI_CON_HANDLE_INVALID)
        {
            /* CIS 由外设侧主动断 (HCI Disconnect on CIS handle) */
            (void)gap_disconnect(ase->cis_handle);
        }
        ft_le_ase_reset(ase);
        ft_le_ase_notify(ase, FT_LE_ERR_SUCCESS);
    }
    ft_le_streaming_refresh();
}

static void ft_le_control_point(const rt_uint8_t *buffer, rt_uint16_t size)
{
    if (size < 1U)
    {
        s_stat_op_err++;
        return;
    }
    switch (buffer[0])
    {
    case FT_LE_OP_CONFIG_CODEC:     ft_le_handle_codec_config(&buffer[1], size - 1U); break;
    case FT_LE_OP_CONFIG_QOS:       ft_le_handle_qos_config(&buffer[1], size - 1U); break;
    case FT_LE_OP_ENABLE:           ft_le_handle_enable(&buffer[1], size - 1U); break;
    case FT_LE_OP_UPDATE_METADATA:  ft_le_handle_metadata_update(&buffer[1], size - 1U); break;
    case FT_LE_OP_RELEASE:          ft_le_handle_release(&buffer[1], size - 1U); break;
    case FT_LE_OP_RECV_START_READY:
    case FT_LE_OP_DISABLE:
    case FT_LE_OP_RECV_STOP_READY:
        /* 仅 Source ASE 适用; Sink-only 配置直接回 Invalid Direction */
        if (size >= 2U)
        {
            ft_ase_t *ase = ft_le_ase_by_id(buffer[1]);
            if (ase != RT_NULL)
            {
                ft_le_ase_notify(ase, FT_LE_ERR_INVALID_DIRECTION);
            }
        }
        s_stat_op_err++;
        break;
    default:
        s_stat_op_err++;
        break;
    }
}

/* ---- ATT 路由 (bt_main.c 的 att 回调转发进来) ----
 * read 返回 0xFFFF = 非本模块句柄; write 返回 -2 = 非本模块句柄 */
#define FT_LE_READ_UNHANDLED   0xFFFFU
#define FT_LE_WRITE_UNHANDLED  (-2)

uint16_t ft_le_audio_att_read(rt_uint16_t att_handle, rt_uint16_t offset,
                              rt_uint8_t *buffer, rt_uint16_t buffer_size)
{
    for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
    {
        if (att_handle == s_ase_value_handle[i])
        {
            rt_uint8_t val[8U + FT_LE_CODEC_CFG_MAX];
            rt_uint16_t len = ft_le_ase_encode(&s_ase[i], val);
            /* btstack 两段式: 首调 buffer=NULL 查总长, 再调拷贝 */
            if (buffer == RT_NULL)
            {
                return len;
            }
            if (offset < len && buffer_size >= (rt_uint16_t)(len - offset))
            {
                memcpy(buffer, &val[offset], len - offset);
                return (uint16_t)(len - offset);
            }
            return 0U;
        }
    }
    return FT_LE_READ_UNHANDLED;
}

int ft_le_audio_att_write(hci_con_handle_t con_handle, rt_uint16_t att_handle,
                          const rt_uint8_t *buffer, rt_uint16_t buffer_size)
{
    if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_ASE_CONTROL_POINT_01_VALUE_HANDLE)
    {
        s_acl_handle = con_handle;   /* 控制面来自哪个 ACL, 通知就回哪条 */
        ft_le_control_point(buffer, buffer_size);
        return 0;
    }
    for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
    {
        uint16_t cccd = (i == 0U)
            ? ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_SINK_ASE_01_CLIENT_CONFIGURATION_HANDLE
            : ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_SINK_ASE_02_CLIENT_CONFIGURATION_HANDLE;
        if (att_handle == cccd)
        {
            s_ase[i].notify_on =
                (buffer_size >= 2U && little_endian_read_16(buffer, 0) == 0x0001U) ? 1U : 0U;
            s_acl_handle = con_handle;
            return 0;
        }
    }
    return FT_LE_WRITE_UNHANDLED;
}

/* ---- ISO SDU -> A0 ring ----
 * ring 帧: [len16 LE][ch u8][LC3 帧], len = 1 + 帧字节数。
 * 一次 ISO SDU = 一帧 (PACS 声明 max 1 frame/SDU)。 */
static void ft_le_iso_handler(rt_uint8_t packet_type, rt_uint16_t channel,
                              rt_uint8_t *packet, rt_uint16_t size)
{
    (void)channel;
    if (packet_type != HCI_ISO_DATA_PACKET) return;

    rt_uint16_t hdr = little_endian_read_16(packet, 0);
    hci_con_handle_t con_handle = hdr & 0x0FFFU;
    rt_uint8_t ts_flag = (hdr >> 14) & 1U;
    rt_uint16_t offset = 4U;
    if (ts_flag) offset += 4U;
    offset += 2U;   /* packet sequence number */
    if (size < offset + 2U) return;
    rt_uint16_t hdr2 = little_endian_read_16(packet, offset);
    rt_uint16_t sdu_len = hdr2 & 0x3FFFU;
    rt_uint8_t pkt_status = hdr2 >> 14;
    offset += 2U;
    if (pkt_status != 0U || sdu_len == 0U || (rt_uint16_t)(offset + sdu_len) > size)
    {
        return;   /* 丢失/无效 SDU: M55 端以声道超时 PLC 补, 不投垃圾 */
    }

    ft_ase_t *ase = RT_NULL;
    for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
    {
        if (s_ase[i].cis_handle == con_handle) ase = &s_ase[i];
    }
    if (ase == RT_NULL)
    {
        return;
    }
    /* 自愈: 部分 stack 在 Enable 完成前即开始投流 —— 首包数据到达即开 ring */
    if (s_streaming == 0U)
    {
        ft_le_ring_start();
    }

    rt_uint16_t total = (rt_uint16_t)(1U + sdu_len);
    if (ft_alink_space(FT_ALINK) < (rt_uint32_t)total)
    {
        s_stat_iso_dropped++;
        return;
    }
    rt_uint8_t stage[2U + 1U + 128U];   /* LC3 48k/10ms 帧长 <= 120B + 声道 1B */
    if (sdu_len > 128U)
    {
        s_stat_iso_dropped++;
        return;
    }
    stage[0] = (rt_uint8_t)(total & 0xFFU);
    stage[1] = (rt_uint8_t)(total >> 8);
    stage[2] = ase->chan_alloc;   /* 0x01=L 0x02=R, M55 据此分路解码 */
    memcpy(&stage[3], &packet[offset], sdu_len);
    if (ft_audio_produce(stage, total) == total)
    {
        s_stat_iso_sdus++;
        s_stat_frames++;
    }
    else
    {
        s_stat_iso_dropped++;
    }
}

/* ---- HCI 事件: ACL 连接跟踪 / CIS 请求 / CIS 建立 / 断链清理 ---- */
static void ft_le_hci_handler(rt_uint8_t packet_type, rt_uint16_t channel,
                              rt_uint8_t *packet, rt_uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (packet[0])
    {
    case HCI_EVENT_LE_META:
        switch (packet[2])
        {
        case HCI_SUBEVENT_LE_CIS_REQUEST:
        {
            hci_con_handle_t cis_handle =
                hci_subevent_le_cis_request_get_cis_connection_handle(packet);
            rt_uint8_t cig_id = hci_subevent_le_cis_request_get_cig_id(packet);
            rt_uint8_t cis_id = hci_subevent_le_cis_request_get_cis_id(packet);
            ft_ase_t *match = RT_NULL;
            for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
            {
                ft_ase_t *ase = &s_ase[i];
                if (ase->state == FT_ASE_QOS_CONFIGURED &&
                    ase->cig_id == cig_id && ase->cis_id == cis_id)
                {
                    match = ase;
                }
            }
            if (match != RT_NULL)
            {
                match->cis_handle = cis_handle;
                (void)gap_cis_accept(cis_handle);
                rt_kprintf("[LEA] CIS accept: ase=%u cis=0x%04x (cig=%u cis_id=%u)\n",
                           match->ase_id, cis_handle, cig_id, cis_id);
            }
            else
            {
                (void)gap_cis_reject(cis_handle);
                rt_kprintf("[LEA] CIS reject: cig=%u cis_id=%u (no QoS match)\n",
                           cig_id, cis_id);
            }
            break;
        }
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
            if (packet[3] == 0U)   /* status ok */
            {
                s_acl_handle = little_endian_read_16(packet, 4);
            }
            break;
        default:
            break;
        }
        break;

    case HCI_EVENT_META_GAP:
        if (packet[2] == GAP_SUBEVENT_CIS_CREATED)
        {
            rt_uint8_t status = gap_subevent_cis_created_get_status(packet);
            hci_con_handle_t cis_handle =
                gap_subevent_cis_created_get_cis_con_handle(packet);
            for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
            {
                if (s_ase[i].cis_handle == cis_handle)
                {
                    if (status == 0U)
                    {
                        s_ase[i].established = 1U;
                        rt_kprintf("[LEA] ASE%u CIS established (0x%04x)\n",
                                   s_ase[i].ase_id, cis_handle);
                    }
                    else
                    {
                        rt_kprintf("[LEA] ASE%u CIS failed status=0x%02x\n",
                                   s_ase[i].ase_id, status);
                        ft_le_ase_reset(&s_ase[i]);
                    }
                }
            }
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
    {
        hci_con_handle_t h = hci_event_disconnection_complete_get_connection_handle(packet);
        rt_uint8_t was_acl = (h == s_acl_handle);
        rt_uint8_t touched = 0U;
        for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
        {
            if (s_ase[i].cis_handle == h || (was_acl && s_ase[i].state != FT_ASE_IDLE))
            {
                ft_le_ase_reset(&s_ase[i]);
                touched = 1U;
            }
        }
        if (was_acl)
        {
            s_acl_handle = HCI_CON_HANDLE_INVALID;
        }
        if (touched)
        {
            ft_le_streaming_refresh();
            rt_kprintf("[LEA] link down 0x%04x, ASE reset\n", h);
        }
        break;
    }
    default:
        break;
    }
}

/* ---- 初始化 / 诊断 ---- */
void ft_le_audio_init(void)
{
    for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
    {
        ft_le_ase_reset(&s_ase[i]);
        s_ase[i].ase_id = (rt_uint8_t)(i + 1U);
    }
    s_hci_event_reg.callback = &ft_le_hci_handler;
    hci_add_event_handler(&s_hci_event_reg);
    hci_register_iso_packet_handler(&ft_le_iso_handler);
    rt_kprintf("[LEA] unicast server ready (2 sink ASE, LC3 48k/10ms)\n");
}

static int ft_le_stats(int argc, char **argv)
{
    (void)argc; (void)argv;
    rt_kprintf("[LEA] acl=0x%04x streaming=%u\n",
               s_acl_handle, s_streaming);
    for (rt_uint32_t i = 0U; i < FT_LE_ASE_NUM; i++)
    {
        ft_ase_t *ase = &s_ase[i];
        rt_kprintf("[LEA] ASE%u: state=%u cis=0x%04x max_sdu=%u chan=0x%02x notify=%u\n",
                   ase->ase_id, ase->state, ase->cis_handle,
                   ase->max_sdu, ase->chan_alloc, ase->notify_on);
    }
    rt_kprintf("[LEA] iso sdus=%lu dropped=%lu frames=%lu op_err=%lu\n",
               (unsigned long)s_stat_iso_sdus, (unsigned long)s_stat_iso_dropped,
               (unsigned long)s_stat_frames, (unsigned long)s_stat_op_err);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_le_stats, bt_le_stats,
                     M8.1: show LE Audio unicast server state);
