#ifndef LV_DRAW_RUNTIME_STATS_H
#define LV_DRAW_RUNTIME_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
} lv_draw_runtime_stats_t;

void lv_draw_runtime_stats_get(lv_draw_runtime_stats_t *stats);
void lv_draw_runtime_stats_reset(void);

void lv_draw_runtime_stats_note_gpu_task(uint32_t task_type);
void lv_draw_runtime_stats_note_gpu_flush(void);
void lv_draw_runtime_stats_note_gpu_finish(uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* LV_DRAW_RUNTIME_STATS_H */
