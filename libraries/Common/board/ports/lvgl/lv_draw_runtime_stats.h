#ifndef LV_DRAW_RUNTIME_STATS_H
#define LV_DRAW_RUNTIME_STATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LV_DRAW_RUNTIME_TASK_TYPE_COUNT 13U

typedef struct
{
    uint32_t routed_gpu_tasks;
    uint32_t routed_sw_tasks;
    uint32_t route_unit_switches;

    uint32_t executed_gpu_tasks;
    uint32_t executed_sw_tasks;
    uint32_t sw_label_tasks;
    uint32_t sw_image_tasks;
    uint32_t sw_layer_tasks;
    uint32_t sw_other_tasks;

    uint32_t gpu_flush_calls;
    uint32_t gpu_finish_calls;
    uint32_t gpu_finish_wait_ms_total;
    uint32_t gpu_finish_wait_ms_max;

    uint32_t gpu_submit_count;
    uint32_t gpu_submit_bytes;

    uint32_t gpu_completed_jobs;
    uint32_t gpu_busy_us_total;
    uint32_t gpu_busy_us_max;

    uint32_t gpu_batch_frames;
    uint32_t gpu_batch_frame_submits_total;
    uint32_t gpu_batch_frame_submits_max;
    uint32_t gpu_batch_frame_tasks_max;
    uint32_t gpu_batch_software_boundaries;
    uint32_t gpu_batch_resource_boundaries;
    uint32_t gpu_batch_explicit_boundaries;
    uint32_t gpu_batch_transient_bytes_max;
    uint32_t gpu_batch_transient_overflows;
    uint32_t gpu_batch_collect_us_total;
    uint32_t gpu_batch_collect_us_max;
    uint32_t gpu_batch_encode_us_total;
    uint32_t gpu_batch_encode_us_max;
    uint32_t gpu_batch_finish_us_total;
    uint32_t gpu_batch_finish_us_max;
    uint32_t gpu_pipeline_wait_count;
    uint32_t gpu_pipeline_wait_us_total;
    uint32_t gpu_pipeline_wait_us_max;
    uint32_t scanout_pipeline_wait_count;
    uint32_t scanout_pipeline_wait_us_total;
    uint32_t scanout_pipeline_wait_us_max;
    uint32_t gpu_task_encode_count[LV_DRAW_RUNTIME_TASK_TYPE_COUNT];
    uint32_t gpu_task_encode_us_total[LV_DRAW_RUNTIME_TASK_TYPE_COUNT];
    uint32_t gpu_task_encode_us_max[LV_DRAW_RUNTIME_TASK_TYPE_COUNT];
    uint32_t gpu_glyph_draw_count;
    uint32_t gpu_glyph_draw_us_total;
    uint32_t gpu_glyph_draw_us_max;
} lv_draw_runtime_stats_t;

void lv_draw_runtime_stats_get(lv_draw_runtime_stats_t *stats);
void lv_draw_runtime_stats_reset(void);

void lv_draw_runtime_stats_note_gpu_task(uint32_t task_type);
void lv_draw_runtime_stats_note_gpu_flush(void);
void lv_draw_runtime_stats_note_gpu_finish(uint32_t elapsed_ms);
void lv_draw_runtime_stats_note_batch_begin(void);
void lv_draw_runtime_stats_note_batch_collection_end(void);
void lv_draw_runtime_stats_note_batch_dispatch_end(void);
void lv_draw_runtime_stats_note_batch_end(void);
void lv_draw_runtime_stats_note_pipeline_wait(uint32_t gpu_wait_us,
                                              uint32_t scanout_wait_us);
void lv_draw_runtime_stats_note_batch_boundary(uint32_t reason);
void lv_draw_runtime_stats_note_batch_transient(uint32_t bytes, bool overflow);
uint32_t lv_draw_runtime_stats_gpu_task_begin(void);
void lv_draw_runtime_stats_gpu_task_end(uint32_t task_type, uint32_t start_cycles);
uint32_t lv_draw_runtime_stats_gpu_glyph_begin(void);
void lv_draw_runtime_stats_gpu_glyph_end(uint32_t start_cycles);

#ifdef __cplusplus
}
#endif

#endif /* LV_DRAW_RUNTIME_STATS_H */
