#ifndef LV_GPU_BATCH_H
#define LV_GPU_BATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lv_draw_vg_lite_unit_t;
struct lv_draw_buf_t;

typedef struct
{
    uint32_t hits;
    uint32_t misses;
    uint32_t stores;
    uint32_t bytes_used;
    uint32_t overflows;
    uint32_t descriptor_hits;
    uint32_t descriptor_misses;
} lv_gpu_glyph_cache_stats_t;

typedef enum
{
    LV_GPU_BATCH_BOUNDARY_FRAME = 0,
    LV_GPU_BATCH_BOUNDARY_SOFTWARE,
    LV_GPU_BATCH_BOUNDARY_RESOURCE,
    LV_GPU_BATCH_BOUNDARY_EXPLICIT,
} lv_gpu_batch_boundary_t;

typedef struct
{
    uint32_t stage;
    bool active;
    bool commands_pending;
    bool inflight;
    uint8_t active_slot;
    uint8_t inflight_slot;
} lv_gpu_batch_debug_t;

void lv_gpu_batch_register_unit(struct lv_draw_vg_lite_unit_t *unit);
void lv_gpu_batch_frame_begin(void);
void lv_gpu_batch_frame_end(void);
void lv_gpu_batch_wait_idle(void);
void lv_gpu_batch_debug_get(lv_gpu_batch_debug_t *debug);
void lv_gpu_batch_before_software(uint32_t task_type);
void lv_gpu_batch_note_gpu_command(void);
void lv_gpu_batch_note_finish_complete(void);
void lv_gpu_batch_force_sync(lv_gpu_batch_boundary_t reason);
void *lv_gpu_batch_transient_copy(const void *source, size_t size, size_t alignment);
void lv_gpu_batch_prepare_submit(void);
struct lv_draw_buf_t *lv_gpu_batch_glyph_cache_lookup(const void *font,
                                                       uint32_t glyph_id,
                                                       uint16_t width,
                                                       uint16_t height,
                                                       uint8_t format);
struct lv_draw_buf_t *lv_gpu_batch_glyph_cache_store(const void *font,
                                                      uint32_t glyph_id,
                                                      uint16_t width,
                                                      uint16_t height,
                                                      uint8_t format,
                                                      const struct lv_draw_buf_t *source);
bool lv_gpu_batch_glyph_buffer_is_persistent(const struct lv_draw_buf_t *draw_buf);
void lv_gpu_batch_glyph_cache_stats_get(lv_gpu_glyph_cache_stats_t *stats);
void lv_gpu_batch_note_font_descriptor_cache(bool hit);

bool lv_gpu_batch_is_active(void);
bool lv_gpu_batch_defer_dispatch(void);
bool lv_gpu_batch_suppress_flush(void);

/* Strong override for LVGL's weak task-creation hook. */
bool lv_draw_batch_defer_dispatch(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_GPU_BATCH_H */
