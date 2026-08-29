#ifndef FEATHERTALK_M55_IPC_H
#define FEATHERTALK_M55_IPC_H

#include <stdint.h>

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

#endif /* FEATHERTALK_M55_IPC_H */
