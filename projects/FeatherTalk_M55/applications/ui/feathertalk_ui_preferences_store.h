#ifndef FEATHERTALK_UI_PREFERENCES_STORE_H
#define FEATHERTALK_UI_PREFERENCES_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT_PREFERENCES_STORE_WALLPAPER_PATH_MAX 256U
#define FT_PREFERENCES_STORE_TILE_OPA_MIN        32U
#define FT_PREFERENCES_STORE_TILE_OPA_MAX        255U
#define FT_PREFERENCES_STORE_TIMEZONE_MINUTES_MIN (-720)
#define FT_PREFERENCES_STORE_TIMEZONE_MINUTES_MAX 840
#define FT_PREFERENCES_STORE_AUDIO_OUTPUT_VOLUME_MAX 100U
#define FT_PREFERENCES_STORE_AUDIO_INPUT_GAIN_MAX 75U

typedef struct
{
    uint32_t accent_rgb;
    uint8_t tile_opa;
    uint8_t background;
    bool use_24_hour;
    int16_t timezone_offset_minutes;
    uint8_t language;
    uint8_t audio_output_volume;
    uint8_t audio_input_gain;
    uint32_t audio_output_sample_rate;
    uint8_t audio_output_sample_bits;
    uint8_t audio_output_channels;
    char wallpaper_path[FT_PREFERENCES_STORE_WALLPAPER_PATH_MAX];
} ft_preferences_store_payload_t;

typedef struct
{
    bool initialized;
    bool worker_started;
    bool loaded_from_storage;
    bool dirty;
    bool write_in_progress;
    bool frozen;
    bool test_suspended;
    int8_t active_slot; /* -1: no committed slot, 0: A, 1: B. */
    uint8_t valid_slots; /* Bit 0: A, bit 1: B. */
    uint32_t generation;
    uint32_t update_serial;
    uint32_t successful_writes;
    uint32_t failed_writes;
    uint32_t ignored_test_updates;
    int last_error;
} ft_preferences_store_status_t;

/* Load the newest valid A/B record. If neither record is valid, copy defaults,
 * mark the store dirty, and let the worker create the first record after the
 * quiet period. `loaded` may be NULL. */
int ft_preferences_store_init(const ft_preferences_store_payload_t *defaults,
                              ft_preferences_store_payload_t *loaded);

/* Validate and copy a complete immutable snapshot into the store. During test
 * suspension this intentionally returns success without changing the snapshot. */
int ft_preferences_store_update(const ft_preferences_store_payload_t *payload);
int ft_preferences_store_snapshot(ft_preferences_store_payload_t *payload);

/* Synchronously persist all updates visible to the caller. A failed write keeps
 * the store dirty so the background worker can retry it. */
int ft_preferences_store_flush(void);

/* Freeze is used before exporting /flash over USB. It blocks new background
 * writes, waits for an existing write, and flushes the latest snapshot. */
int ft_preferences_store_freeze(void);
void ft_preferences_store_thaw(void);

/* Automated UI tests can suppress persistence and ignore their preference
 * mutations, preserving the per-device configuration. */
int ft_preferences_store_test_suspend(bool suspend);

int ft_preferences_store_get_status(ft_preferences_store_status_t *status);
bool ft_preferences_store_payload_valid(
    const ft_preferences_store_payload_t *payload);

#endif /* FEATHERTALK_UI_PREFERENCES_STORE_H */
