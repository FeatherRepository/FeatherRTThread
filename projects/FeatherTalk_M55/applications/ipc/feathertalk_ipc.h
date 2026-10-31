#ifndef FEATHERTALK_M55_IPC_H
#define FEATHERTALK_M55_IPC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t sequence;
    uint32_t received_ms;
    uint32_t m33_uptime_ms;
    uint32_t unix_time;
    uint8_t battery_percent;
    uint8_t network_state;
    uint8_t signal_percent;
    uint8_t flags;
} feathertalk_system_status_t;

typedef struct
{
    uint32_t sequence;
    uint32_t received_ms;
    uint8_t capabilities;
    uint8_t enabled;
    uint8_t connected;
    uint8_t wifi_signal_percent;
    uint8_t brightness_percent;
    uint8_t rotation;
    uint8_t last_control;
    uint8_t result;
} feathertalk_quick_status_t;

int feathertalk_ipc_start(void);
void feathertalk_ipc_set_lvgl_ready(void);
int feathertalk_ipc_get_system_status(feathertalk_system_status_t *status);
int feathertalk_ipc_get_quick_status(feathertalk_quick_status_t *status);
int feathertalk_ipc_set_quick_control(uint8_t control, uint8_t value);
void feathertalk_ipc_set_periodic_report_enabled(bool enabled);

/* KWS 唤醒事件（M55 -> M33）。
 * 线程/中断安全：只投递待发槽，实际帧由 IPC 线程统一写出
 * （rt_device_write 非并发安全，与 quick_command 同一模式）。
 * label_id: EI 类别索引（1 = xiaomai）；confidence_pct: 0~100。 */
void feathertalk_ipc_post_kws_event(uint8_t label_id, uint8_t confidence_pct);
uint32_t feathertalk_ipc_get_kws_event_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_M55_IPC_H */
