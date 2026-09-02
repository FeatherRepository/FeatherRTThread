#include "lv_gpu_batch.h"

#include "lv_draw_runtime_stats.h"
#include "lv_draw_vg_lite_type.h"
#include "lv_vg_lite_utils.h"
#include "cy_pdl.h"
#include "drv_lcd.h"
#include <string.h>

#ifdef FEATHERTALK_USING_LVGL_GPU_BATCH

#ifndef FEATHERTALK_LVGL_GPU_TRANSIENT_BYTES
#define FEATHERTALK_LVGL_GPU_TRANSIENT_BYTES (384U * 1024U)
#endif

#ifndef FEATHERTALK_LVGL_GPU_GLYPH_CACHE_BYTES
#define FEATHERTALK_LVGL_GPU_GLYPH_CACHE_BYTES (192U * 1024U)
#endif
#define LV_GPU_GLYPH_CACHE_BYTES FEATHERTALK_LVGL_GPU_GLYPH_CACHE_BYTES
#define LV_GPU_GLYPH_CACHE_SLOTS 512U
#define LV_GPU_PIPELINE_SLOTS 2U

CY_SECTION(".cy_gpu_buf") CY_ALIGN(__SCB_DCACHE_LINE_SIZE)
static uint8_t s_transient_arena[LV_GPU_PIPELINE_SLOTS][FEATHERTALK_LVGL_GPU_TRANSIENT_BYTES];
static size_t s_transient_offset[LV_GPU_PIPELINE_SLOTS];
static size_t s_transient_cleaned_offset[LV_GPU_PIPELINE_SLOTS];

typedef struct
{
    const void *font;
    uint32_t glyph_id;
    uint32_t data_offset;
    uint16_t width;
    uint16_t height;
    uint8_t format;
    bool valid;
    lv_draw_buf_t draw_buf;
} lv_gpu_glyph_cache_entry_t;

CY_SECTION(".cy_gpu_buf") CY_ALIGN(__SCB_DCACHE_LINE_SIZE)
static uint8_t s_glyph_cache_arena[LV_GPU_GLYPH_CACHE_BYTES];
static lv_gpu_glyph_cache_entry_t s_glyph_cache[LV_GPU_GLYPH_CACHE_SLOTS];
static size_t s_glyph_cache_offset;
static lv_gpu_glyph_cache_stats_t s_glyph_cache_stats;

typedef struct
{
    struct lv_draw_vg_lite_unit_t *unit;
    lv_gpu_batch_boundary_t requested_boundary;
    bool active;
    bool gpu_commands_pending;
    bool sync_in_progress;
    bool inflight;
    uint8_t active_slot;
    uint8_t inflight_slot;
} lv_gpu_batch_state_t;

static lv_gpu_batch_state_t s_batch;
static volatile uint32_t s_debug_stage;

static uint32_t elapsed_us(uint32_t start_cycles)
{
    uint32_t cycles_per_us = SystemCoreClock / 1000000U;
    if (cycles_per_us == 0U)
    {
        cycles_per_us = 1U;
    }
    return (DWT->CYCCNT - start_cycles) / cycles_per_us;
}

static void retire_inflight_frame(void)
{
    uint32_t start_cycles;
    uint32_t gpu_wait_us;
    uint32_t scanout_wait_us;

    if (!s_batch.inflight || s_batch.unit == NULL)
    {
        return;
    }

    /* The CPU may already have encoded the next frame in VG-Lite's alternate
     * command buffer. Wait only for the submitted buffer, then release exactly
     * the decoder/gradient resources and scanout surface owned by that frame. */
    s_debug_stage = 10U;
    start_cycles = DWT->CYCCNT;
    lv_vg_lite_wait_frame(s_batch.unit, s_batch.inflight_slot);
    s_debug_stage = 11U;
    gpu_wait_us = elapsed_us(start_cycles);
    start_cycles = DWT->CYCCNT;
    s_debug_stage = 12U;
    (void)lcd_gpu_surface_wait_present(50U);
    s_debug_stage = 13U;
    scanout_wait_us = elapsed_us(start_cycles);
    lv_draw_runtime_stats_note_pipeline_wait(gpu_wait_us, scanout_wait_us);
    s_batch.inflight = false;
}

static uint32_t glyph_cache_hash(const void *font, uint32_t glyph_id)
{
    uintptr_t key = (uintptr_t)font;
    return (uint32_t)(((key >> 4U) ^ (key >> 13U) ^
                       ((uintptr_t)glyph_id * 2654435761UL)) &
                      (LV_GPU_GLYPH_CACHE_SLOTS - 1U));
}

static bool glyph_cache_key_matches(const lv_gpu_glyph_cache_entry_t *entry,
                                    const void *font,
                                    uint32_t glyph_id,
                                    uint16_t width,
                                    uint16_t height,
                                    uint8_t format)
{
    return entry->valid && entry->font == font && entry->glyph_id == glyph_id &&
           entry->width == width && entry->height == height && entry->format == format;
}

static void glyph_cache_clean(const void *data, size_t size)
{
    uintptr_t start = (uintptr_t)data & ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1U);
    uintptr_t end = ((uintptr_t)data + size + __SCB_DCACHE_LINE_SIZE - 1U) &
                    ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1U);
    SCB_CleanDCache_by_Addr((void *)start, (int32_t)(end - start));
}

/*
 * GC355 executes uploaded VG-Lite paths directly from the cacheable GPU heap.
 * The vendor driver cannot include this SoC's CMSIS cache definitions, so its
 * weak hook is overridden here at the platform boundary.
 */
void vg_lite_uploaded_path_cache_clean(const void *memory, vg_lite_uint32_t bytes)
{
    if (memory == NULL || bytes == 0U)
    {
        return;
    }

    glyph_cache_clean(memory, (size_t)bytes);
}

void lv_gpu_batch_register_unit(struct lv_draw_vg_lite_unit_t *unit)
{
    s_batch.unit = unit;
}

void lv_gpu_batch_frame_begin(void)
{
    s_debug_stage = 1U;
    if (s_batch.active)
    {
        lv_gpu_batch_frame_end();
    }

    s_batch.active = true;
    s_batch.requested_boundary = LV_GPU_BATCH_BOUNDARY_RESOURCE;
    s_transient_offset[s_batch.active_slot] = 0U;
    s_transient_cleaned_offset[s_batch.active_slot] = 0U;
    if (s_batch.unit != NULL)
        lv_vg_lite_pending_select(s_batch.unit, s_batch.active_slot);
    lv_draw_runtime_stats_note_batch_begin();
    s_debug_stage = 2U;
}

void lv_gpu_batch_frame_end(void)
{
    if (!s_batch.active)
    {
        return;
    }

    s_debug_stage = 3U;
    /* Collect and encode frame N+1 while frame N is executing. Only here, at
     * the next submission boundary, must the preceding GPU job and display
     * commit be retired before their command/surface slots are reused. */
    retire_inflight_frame();

    if (s_batch.gpu_commands_pending && s_batch.unit != NULL)
    {
        s_debug_stage = 20U;
        lv_vg_lite_submit_frame(s_batch.unit);
        s_debug_stage = 21U;
        s_batch.inflight = true;
        s_batch.inflight_slot = s_batch.active_slot;
        s_batch.gpu_commands_pending = false;
        s_batch.active_slot ^= 1U;
        lv_vg_lite_pending_select(s_batch.unit, s_batch.active_slot);
    }
    s_batch.active = false;
    lv_draw_runtime_stats_note_batch_end();
    s_debug_stage = 0U;
}

void lv_gpu_batch_wait_idle(void)
{
    s_debug_stage = 30U;
    retire_inflight_frame();
    s_debug_stage = 0U;
}

void lv_gpu_batch_debug_get(lv_gpu_batch_debug_t *debug)
{
    if (debug == NULL) return;
    debug->stage = s_debug_stage;
    debug->active = s_batch.active;
    debug->commands_pending = s_batch.gpu_commands_pending;
    debug->inflight = s_batch.inflight;
    debug->active_slot = s_batch.active_slot;
    debug->inflight_slot = s_batch.inflight_slot;
}

void lv_gpu_batch_before_software(uint32_t task_type)
{
    (void)task_type;
    if (s_batch.active && s_batch.gpu_commands_pending)
    {
        lv_gpu_batch_force_sync(LV_GPU_BATCH_BOUNDARY_SOFTWARE);
    }
}

void lv_gpu_batch_note_gpu_command(void)
{
    s_batch.gpu_commands_pending = true;
}

void lv_gpu_batch_note_finish_complete(void)
{
    if (!s_batch.gpu_commands_pending)
    {
        return;
    }

    if (s_batch.active && s_batch.requested_boundary != LV_GPU_BATCH_BOUNDARY_FRAME)
    {
        lv_draw_runtime_stats_note_batch_boundary((uint32_t)s_batch.requested_boundary);
    }

    s_batch.gpu_commands_pending = false;
    s_batch.requested_boundary = LV_GPU_BATCH_BOUNDARY_RESOURCE;
}

void lv_gpu_batch_force_sync(lv_gpu_batch_boundary_t reason)
{
    if ((!s_batch.gpu_commands_pending && !s_batch.inflight) ||
        s_batch.unit == NULL || s_batch.sync_in_progress)
    {
        return;
    }

    s_batch.requested_boundary = reason;
    s_batch.sync_in_progress = true;
    retire_inflight_frame();
    if (s_batch.gpu_commands_pending)
        lv_vg_lite_finish(s_batch.unit);
    s_transient_offset[s_batch.active_slot] = 0U;
    s_transient_cleaned_offset[s_batch.active_slot] = 0U;
    s_batch.sync_in_progress = false;
}

void *lv_gpu_batch_transient_copy(const void *source, size_t size, size_t alignment)
{
    size_t aligned_offset;
    uint8_t slot = s_batch.active_slot;

    if (!s_batch.active || source == NULL || size == 0U ||
        size > sizeof(s_transient_arena[slot]))
    {
        return NULL;
    }

    if (alignment == 0U)
    {
        alignment = 1U;
    }
    aligned_offset = (s_transient_offset[slot] + alignment - 1U) & ~(alignment - 1U);
    if (aligned_offset + size > sizeof(s_transient_arena[slot]))
    {
        lv_gpu_batch_force_sync(LV_GPU_BATCH_BOUNDARY_EXPLICIT);
        s_transient_offset[slot] = 0U;
        s_transient_cleaned_offset[slot] = 0U;
        aligned_offset = 0U;
        lv_draw_runtime_stats_note_batch_transient((uint32_t)size, true);
    }

    memcpy(&s_transient_arena[slot][aligned_offset], source, size);
    s_transient_offset[slot] = aligned_offset + size;
    lv_draw_runtime_stats_note_batch_transient((uint32_t)s_transient_offset[slot], false);
    return &s_transient_arena[slot][aligned_offset];
}

void lv_gpu_batch_prepare_submit(void)
{
    uintptr_t start;
    uintptr_t end;
    uint8_t slot = s_batch.active_slot;

    if (s_transient_offset[slot] == 0U ||
        s_transient_cleaned_offset[slot] == s_transient_offset[slot])
    {
        return;
    }

    start = (uintptr_t)s_transient_arena[slot] & ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1U);
    end = ((uintptr_t)s_transient_arena[slot] + s_transient_offset[slot] +
           __SCB_DCACHE_LINE_SIZE - 1U) &
          ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1U);
    SCB_CleanDCache_by_Addr((void *)start, (int32_t)(end - start));
    s_transient_cleaned_offset[slot] = s_transient_offset[slot];
}

lv_draw_buf_t *lv_gpu_batch_glyph_cache_lookup(const void *font,
                                                uint32_t glyph_id,
                                                uint16_t width,
                                                uint16_t height,
                                                uint8_t format)
{
    uint32_t slot;
    uint32_t probe;

    if (font == NULL || glyph_id == 0U) return NULL;
    slot = glyph_cache_hash(font, glyph_id);
    for (probe = 0U; probe < LV_GPU_GLYPH_CACHE_SLOTS; probe++)
    {
        lv_gpu_glyph_cache_entry_t *entry =
            &s_glyph_cache[(slot + probe) & (LV_GPU_GLYPH_CACHE_SLOTS - 1U)];
        if (!entry->valid)
        {
            s_glyph_cache_stats.misses++;
            return NULL;
        }
        if (glyph_cache_key_matches(entry, font, glyph_id, width, height, format))
        {
            s_glyph_cache_stats.hits++;
            return &entry->draw_buf;
        }
    }
    s_glyph_cache_stats.misses++;
    return NULL;
}

lv_draw_buf_t *lv_gpu_batch_glyph_cache_store(const void *font,
                                               uint32_t glyph_id,
                                               uint16_t width,
                                               uint16_t height,
                                               uint8_t format,
                                               const lv_draw_buf_t *source)
{
    uint32_t slot;
    uint32_t probe;
    size_t size;
    size_t offset;

    if (font == NULL || glyph_id == 0U || source == NULL || source->data == NULL)
        return NULL;
    size = source->header.stride * source->header.h;
    offset = (s_glyph_cache_offset + LV_DRAW_BUF_ALIGN - 1U) & ~(LV_DRAW_BUF_ALIGN - 1U);
    if (size == 0U || offset + size > sizeof(s_glyph_cache_arena))
    {
        s_glyph_cache_stats.overflows++;
        return NULL;
    }

    slot = glyph_cache_hash(font, glyph_id);
    for (probe = 0U; probe < LV_GPU_GLYPH_CACHE_SLOTS; probe++)
    {
        lv_gpu_glyph_cache_entry_t *entry =
            &s_glyph_cache[(slot + probe) & (LV_GPU_GLYPH_CACHE_SLOTS - 1U)];
        if (!entry->valid)
        {
            uint8_t *data = &s_glyph_cache_arena[offset];
            memcpy(data, source->data, size);
            glyph_cache_clean(data, size);
            entry->font = font;
            entry->glyph_id = glyph_id;
            entry->data_offset = (uint32_t)offset;
            entry->width = width;
            entry->height = height;
            entry->format = format;
            entry->draw_buf = *source;
            entry->draw_buf.data = data;
            entry->draw_buf.unaligned_data = data;
            entry->draw_buf.data_size = (uint32_t)size;
            entry->valid = true;
            s_glyph_cache_offset = offset + size;
            s_glyph_cache_stats.stores++;
            s_glyph_cache_stats.bytes_used = (uint32_t)s_glyph_cache_offset;
            return &entry->draw_buf;
        }
    }

    s_glyph_cache_stats.overflows++;
    return NULL;
}

bool lv_gpu_batch_glyph_buffer_is_persistent(const lv_draw_buf_t *draw_buf)
{
    uintptr_t data;
    uintptr_t start = (uintptr_t)s_glyph_cache_arena;
    uintptr_t end = start + s_glyph_cache_offset;
    if (draw_buf == NULL || draw_buf->data == NULL) return false;
    data = (uintptr_t)draw_buf->data;
    return data >= start && data < end;
}

void lv_gpu_batch_glyph_cache_stats_get(lv_gpu_glyph_cache_stats_t *stats)
{
    if (stats != NULL) *stats = s_glyph_cache_stats;
}

void lv_gpu_batch_note_font_descriptor_cache(bool hit)
{
    if (hit) s_glyph_cache_stats.descriptor_hits++;
    else s_glyph_cache_stats.descriptor_misses++;
}

void vg_lite_before_submit_hook(void)
{
    lv_gpu_batch_prepare_submit();
}

bool lv_gpu_batch_is_active(void)
{
    return s_batch.active;
}

bool lv_gpu_batch_defer_dispatch(void)
{
    return s_batch.active;
}

bool lv_draw_batch_defer_dispatch(void)
{
    return lv_gpu_batch_defer_dispatch();
}

bool lv_gpu_batch_suppress_flush(void)
{
    return s_batch.active;
}

#else

void lv_gpu_batch_register_unit(struct lv_draw_vg_lite_unit_t *unit)
{
    (void)unit;
}

void lv_gpu_batch_frame_begin(void) {}
void lv_gpu_batch_frame_end(void) {}
void lv_gpu_batch_wait_idle(void) {}
void lv_gpu_batch_debug_get(lv_gpu_batch_debug_t *debug)
{
    if (debug != NULL) memset(debug, 0, sizeof(*debug));
}
void lv_gpu_batch_before_software(uint32_t task_type) { (void)task_type; }
void lv_gpu_batch_note_gpu_command(void) {}
void lv_gpu_batch_note_finish_complete(void) {}
void lv_gpu_batch_force_sync(lv_gpu_batch_boundary_t reason) { (void)reason; }
void *lv_gpu_batch_transient_copy(const void *source, size_t size, size_t alignment)
{
    (void)source;
    (void)size;
    (void)alignment;
    return NULL;
}
void lv_gpu_batch_prepare_submit(void) {}
struct lv_draw_buf_t *lv_gpu_batch_glyph_cache_lookup(const void *font, uint32_t glyph_id,
                                                       uint16_t width, uint16_t height, uint8_t format)
{
    (void)font; (void)glyph_id; (void)width; (void)height; (void)format;
    return NULL;
}
struct lv_draw_buf_t *lv_gpu_batch_glyph_cache_store(const void *font, uint32_t glyph_id,
                                                      uint16_t width, uint16_t height, uint8_t format,
                                                      const struct lv_draw_buf_t *source)
{
    (void)font; (void)glyph_id; (void)width; (void)height; (void)format; (void)source;
    return NULL;
}
bool lv_gpu_batch_glyph_buffer_is_persistent(const struct lv_draw_buf_t *draw_buf)
{
    (void)draw_buf;
    return false;
}
void lv_gpu_batch_glyph_cache_stats_get(lv_gpu_glyph_cache_stats_t *stats)
{
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
}
void lv_gpu_batch_note_font_descriptor_cache(bool hit) { (void)hit; }
void vg_lite_before_submit_hook(void) {}
bool lv_gpu_batch_is_active(void) { return false; }
bool lv_gpu_batch_defer_dispatch(void) { return false; }
bool lv_draw_batch_defer_dispatch(void) { return false; }
bool lv_gpu_batch_suppress_flush(void) { return false; }

#endif
