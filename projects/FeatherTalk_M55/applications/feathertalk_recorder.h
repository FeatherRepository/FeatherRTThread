#ifndef FEATHERTALK_RECORDER_H
#define FEATHERTALK_RECORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT_RECORDER_DEVICE_COUNT 2U
#define FT_RECORDER_PATH_MAX     256U

typedef enum
{
    FT_RECORDER_IDLE = 0,
    FT_RECORDER_STARTING,
    FT_RECORDER_RECORDING,
    FT_RECORDER_STOPPING,
    FT_RECORDER_SAVED,
    FT_RECORDER_ERROR
} ft_recorder_state_t;

typedef struct
{
    const char *device_name;
    bool registered;
    bool ready;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t sample_bits;
} ft_recorder_device_info_t;

typedef struct
{
    ft_recorder_state_t state;
    size_t selected_device;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t sample_bits;
    uint32_t duration_ms;
    uint32_t peak_per_mille;
    uint32_t peak_max_per_mille;
    uint64_t data_bytes;
    int last_error;
    char storage_mount[16];
    char file_path[FT_RECORDER_PATH_MAX];
} ft_recorder_status_t;

int ft_recorder_get_devices(ft_recorder_device_info_t *devices,
                            size_t capacity, size_t *count);
int ft_recorder_select_device(size_t index);
int ft_recorder_start(void);
int ft_recorder_stop(void);
int ft_recorder_get_status(ft_recorder_status_t *status);
bool ft_recorder_can_start(void);

#endif /* FEATHERTALK_RECORDER_H */
