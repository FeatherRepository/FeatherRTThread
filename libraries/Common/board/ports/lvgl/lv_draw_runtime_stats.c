#include "lv_draw_runtime_stats.h"

#include <string.h>
#include "lvgl.h"
#include "cy_pdl.h"

#define LV_DRAW_UNIT_ID_SW       1U
#define LV_DRAW_UNIT_ID_VG_LITE  2U

/*
 * These counters are intentionally 32-bit. Each field has a single writer
 * (LVGL task, SW draw task, or VG-Lite submission path), so Cortex-M aligned
 * loads give low-overhead telemetry without locking the renderer. A snapshot
 * can contain a one-task skew between fields, which is acceptable here.
 */
static volatile lv_draw_runtime_stats_t s_stats;
static uint32_t s_last_routed_unit;
static volatile uint32_t s_gpu_busy_start_cycles;
static volatile uint32_t s_gpu_busy_active;
static uint32_t s_cpu_cycles_per_us;

static void LV_ATTRIBUTE_FAST_MEM gpu_cycle_counter_init(void)
{
    if (s_cpu_cycles_per_us != 0U)
    {
        return;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_cpu_cycles_per_us = SystemCoreClock / 1000000U;
    if (s_cpu_cycles_per_us == 0U)
    {
        s_cpu_cycles_per_us = 1U;
    }
}

static void note_sw_task_type(uint32_t task_type)
{
    switch ((lv_draw_task_type_t)task_type)
    {
        case LV_DRAW_TASK_TYPE_LABEL:
            s_stats.sw_label_tasks++;
            break;
        case LV_DRAW_TASK_TYPE_IMAGE:
            s_stats.sw_image_tasks++;
            break;
        case LV_DRAW_TASK_TYPE_LAYER:
            s_stats.sw_layer_tasks++;
            break;
        default:
            s_stats.sw_other_tasks++;
            break;
    }
}

void lv_draw_runtime_stats_get(lv_draw_runtime_stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->routed_gpu_tasks = s_stats.routed_gpu_tasks;
    stats->routed_sw_tasks = s_stats.routed_sw_tasks;
    stats->route_unit_switches = s_stats.route_unit_switches;
    stats->executed_gpu_tasks = s_stats.executed_gpu_tasks;
    stats->executed_sw_tasks = s_stats.executed_sw_tasks;
    stats->sw_label_tasks = s_stats.sw_label_tasks;
    stats->sw_image_tasks = s_stats.sw_image_tasks;
    stats->sw_layer_tasks = s_stats.sw_layer_tasks;
    stats->sw_other_tasks = s_stats.sw_other_tasks;
    stats->gpu_flush_calls = s_stats.gpu_flush_calls;
    stats->gpu_finish_calls = s_stats.gpu_finish_calls;
    stats->gpu_finish_wait_ms_total = s_stats.gpu_finish_wait_ms_total;
    stats->gpu_finish_wait_ms_max = s_stats.gpu_finish_wait_ms_max;
    stats->gpu_submit_count = s_stats.gpu_submit_count;
    stats->gpu_submit_bytes = s_stats.gpu_submit_bytes;
    stats->gpu_completed_jobs = s_stats.gpu_completed_jobs;
    stats->gpu_busy_us_total = s_stats.gpu_busy_us_total;
    stats->gpu_busy_us_max = s_stats.gpu_busy_us_max;
}

void lv_draw_runtime_stats_reset(void)
{
    memset((void *)&s_stats, 0, sizeof(s_stats));
    s_last_routed_unit = 0U;
    s_gpu_busy_active = 0U;
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_runtime_stats_note_gpu_task(uint32_t task_type)
{
    (void)task_type;
    s_stats.executed_gpu_tasks++;
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_runtime_stats_note_gpu_flush(void)
{
    s_stats.gpu_flush_calls++;
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_runtime_stats_note_gpu_finish(uint32_t elapsed_ms)
{
    s_stats.gpu_finish_calls++;
    s_stats.gpu_finish_wait_ms_total += elapsed_ms;
    if (elapsed_ms > s_stats.gpu_finish_wait_ms_max)
    {
        s_stats.gpu_finish_wait_ms_max = elapsed_ms;
    }
}

/* Strong implementations for the low-overhead weak hooks in LVGL/VG-Lite. */
void LV_ATTRIBUTE_FAST_MEM lv_draw_task_route_perf_hook(uint32_t unit_id, uint32_t task_type)
{
    (void)task_type;

    if (unit_id == LV_DRAW_UNIT_ID_VG_LITE)
    {
        s_stats.routed_gpu_tasks++;
    }
    else if (unit_id == LV_DRAW_UNIT_ID_SW)
    {
        s_stats.routed_sw_tasks++;
    }

    if ((unit_id == LV_DRAW_UNIT_ID_SW || unit_id == LV_DRAW_UNIT_ID_VG_LITE) &&
        s_last_routed_unit != 0U && unit_id != s_last_routed_unit)
    {
        s_stats.route_unit_switches++;
    }
    if (unit_id == LV_DRAW_UNIT_ID_SW || unit_id == LV_DRAW_UNIT_ID_VG_LITE)
    {
        s_last_routed_unit = unit_id;
    }
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_sw_task_perf_hook(uint32_t task_type)
{
    s_stats.executed_sw_tasks++;
    note_sw_task_type(task_type);
}

void LV_ATTRIBUTE_FAST_MEM vg_lite_submit_perf_hook(uint32_t command_bytes)
{
    s_stats.gpu_submit_count++;
    s_stats.gpu_submit_bytes += command_bytes;
}

void LV_ATTRIBUTE_FAST_MEM vg_lite_hardware_begin_perf_hook(void)
{
    gpu_cycle_counter_init();
    s_gpu_busy_start_cycles = DWT->CYCCNT;
    s_gpu_busy_active = 1U;
}

void LV_ATTRIBUTE_FAST_MEM vg_lite_hardware_complete_perf_hook(void)
{
    uint32_t elapsed_cycles;
    uint32_t elapsed_us;

    if (s_gpu_busy_active == 0U)
    {
        return;
    }

    elapsed_cycles = DWT->CYCCNT - s_gpu_busy_start_cycles;
    elapsed_us = elapsed_cycles / s_cpu_cycles_per_us;
    s_gpu_busy_active = 0U;
    s_stats.gpu_completed_jobs++;
    s_stats.gpu_busy_us_total += elapsed_us;
    if (elapsed_us > s_stats.gpu_busy_us_max)
    {
        s_stats.gpu_busy_us_max = elapsed_us;
    }
}
