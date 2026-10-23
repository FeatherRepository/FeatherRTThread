/* bt_internal.h - bt_main.c 拆分后五模块的共享内部接口
 * (bt_diag / bt_gatt_ft / bt_adv / bt_bringup / bt_service_core)
 * 对外 API 仍在 bt_service.h / bt_le_audio.h；本头仅供
 * bluetooth/port_rtthread 内部使用。2026-10-24 机械拆分。 */
#ifndef FEATHERTALK_BT_INTERNAL_H
#define FEATHERTALK_BT_INTERNAL_H

#include <rtthread.h>
#include <stdint.h>
#include "btstack_defines.h"
#include "btstack_run_loop.h"   /* btstack_timer_source_t */
#include "hci_dump.h"          /* hci_dump_t (s_dump_stdout) */

/* ---- 服务生命周期状态 (定义于 bt_service_core.c) ---- */
enum { BT_OFF, BT_STARTING, BT_READY, BT_STOPPING, BT_ERROR };
extern volatile int       s_bt_state;
extern volatile int       s_bt_err;         /* 定义于 bt_service_core.c */
extern volatile rt_bool_t s_bt_starting;
extern volatile rt_bool_t s_desired_on;
extern rt_bool_t          s_stack_initialized;
extern rt_bool_t          s_stack_setup_failed;
extern void               (*s_owner_callback)(void);
extern btstack_packet_callback_registration_t s_hci_event_handler;
int  bt_bringup(void);
int  bt_stop_owned(void);

/* ---- UART 设备配置 (bt_info 打印与 bringup 共用) ---- */
#ifndef FEATHERTALK_BT_UART_DEV
#define FEATHERTALK_BT_UART_DEV     "uart4"
#endif
#ifndef FEATHERTALK_BT_UART_BAUD
#define FEATHERTALK_BT_UART_BAUD    115200
#endif

/* ---- 崩溃检查点 (定义于 bt_diag.c) ---- */
extern volatile uint32_t g_bt_checkpoint;
#define BT_CP(code) do { g_bt_checkpoint = (uint32_t)(code); } while (0)

/* ---- 共存/事件诊断 (定义于 bt_diag.c) ---- */
typedef struct {
    uint32_t starts;
    uint32_t command_completes;
    uint32_t advertising_acks;
    uint32_t hardware_errors;
    uint32_t last_complete_ms;
    uint32_t stops;
    uint32_t stack_inits;
} ft_bt_coex_diag_t;
extern volatile ft_bt_coex_diag_t g_bt_coex_diag;

extern volatile uint8_t g_adv_cc_status[3];
#define ADV_CC_PARAMS  0
#define ADV_CC_DATA    1
#define ADV_CC_ENABLE  2

extern volatile uint16_t g_bt_evt_log[12][2];
extern volatile uint8_t  g_bt_evt_log_pos;
void bt_evt_log(uint16_t a, uint16_t b);

extern volatile uint16_t g_leaud_log[16][2];
extern volatile uint8_t  g_leaud_log_pos;
void leaud_log(uint16_t a, uint16_t b);

extern volatile uint32_t g_le_connect_diag[8];

/* ---- GATT/FT Data 服务 (定义于 bt_gatt_ft.c) ---- */
extern hci_con_handle_t s_gatt_con_handle;
extern volatile int     s_gatt_connected;
extern hci_con_handle_t s_classic_handle;
extern volatile int     s_classic_connected;
extern volatile int     s_notify_enabled;
extern uint16_t         s_notify_counter;
extern uint8_t          s_ft_cmd;
extern btstack_timer_source_t s_notify_timer;
extern btstack_packet_callback_registration_t s_sm_event_reg;
uint16_t ft_ntf_build_payload(uint8_t *out);
uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                           uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
int      att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                            uint16_t transaction_mode, uint16_t offset,
                            uint8_t *buffer, uint16_t buffer_size);
void     ft_notify_timer_handler(struct btstack_timer_source *ts);
void     ft_sm_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size);
void     ft_ir_fail_cancel(void);

/* ---- 广播/通道管理 (定义于 bt_adv.c) ---- */
/* M8.1: LE-only 验证开关 (1 = 关闭 Classic 可发现/可连, 广播 flags 声明
 * 无 BR/EDR, 强制手机走 LE Audio/LC3; 0 = 双模常规形态)。
 * M8.x 双模阶段: A2DP Sink/Source + AVRCP 本就无条件初始化, 置 0 即恢复
 * Classic 可见 (TODO: 迁移到 rtconfig/Kconfig 正式配置项) */
#ifndef FEATHERTALK_BT_LE_AUDIO_ONLY
#define FEATHERTALK_BT_LE_AUDIO_ONLY 0
#endif

/* M8.x 音频通道模式 (用户可在 UI 直接选择发 BLE 还是经典):
 * 0=双模 (两套发现机制都开, 默认)  1=仅 LE (只发 LE 广播, 经典扫描关)
 * 2=仅经典 (LE 可连接广播停, 经典扫描开; 广播源 handle3 不受影响,
 *   转发场景 —— A2DP 收流 + Auracast 出局 —— 在经典模式下照常可用)。
 * 原 LE 优先配对模式 = 仅 LE 模式的复用 (HyperOS 配对判型需要)。 */
#define FT_TRANSPORT_DUAL         0
#define FT_TRANSPORT_LE_ONLY      1
#define FT_TRANSPORT_CLASSIC_ONLY 2
extern int s_audio_transport;

extern btstack_timer_source_t s_adv_keepalive_timer;
extern uint8_t s_le_adv_handle;         /* packet_handler 连接建立时停广播 */
extern uint8_t s_le_discovery_handle;
extern uint8_t s_le_discovery_registered;
extern const hci_dump_t *s_dump_stdout; /* bt_bringup 赋值 hci_dump 实例 */
extern const hci_dump_t s_dump_filtered;   /* 过滤包装 (ACL/ISO/NOP 减负) */
void bt_adv_start(void);
void bt_adv_set_data(void);
void bt_adv_enable(void);
void bt_classic_eir_setup(void);
void ft_adv_keepalive_handler(struct btstack_timer_source *ts);

/* ---- HCI 包处理器 (定义于 bt_service_core.c, bringup 注册) ---- */
void packet_handler(uint8_t packet_type, uint16_t channel,
                    uint8_t *packet, uint16_t size);

#endif /* FEATHERTALK_BT_INTERNAL_H */
