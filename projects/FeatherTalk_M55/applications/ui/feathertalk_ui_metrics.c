#include <rtthread.h>
#include "feathertalk_ui_internal.h"
#include "lv_draw_runtime_stats.h"

static ft_ui_metrics_t s_metrics;
static lv_obj_t *s_metrics_root;
static uint32_t s_last_refresh_count;
static uint32_t s_last_render_count;
static uint32_t s_last_flush_count;
static uint32_t s_last_flushed_pixels;
static uint32_t s_last_sample_ms;
static uint32_t s_render_start_ms;
static uint32_t s_route_objects;
static uint32_t s_route_heap;
static lv_draw_runtime_stats_t s_last_draw_stats;
static uint32_t s_metrics_start_ms;
static lv_draw_runtime_stats_t s_metrics_start_draw_stats;

static uint32_t counter_rate(uint32_t delta, uint32_t elapsed_ms)
{
    if (elapsed_ms == 0U)
    {
        return 0U;
    }
    return (uint32_t)(((uint64_t)delta * 1000ULL) / elapsed_ms);
}

static uint32_t count_objects(lv_obj_t *obj)
{
    uint32_t count = 1U;
    uint32_t child_count;
    uint32_t i;

    if ((obj == RT_NULL) || !lv_obj_is_valid(obj))
    {
        return 0U;
    }

    child_count = lv_obj_get_child_count(obj);
    for (i = 0U; i < child_count; i++)
    {
        count += count_objects(lv_obj_get_child(obj, (int32_t)i));
    }
    return count;
}

static void display_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_REFR_READY)
    {
        s_metrics.refresh_count++;
    }
    else if (code == LV_EVENT_RENDER_START)
    {
        s_render_start_ms = rt_tick_get_millisecond();
    }
    else if (code == LV_EVENT_RENDER_READY)
    {
        uint32_t elapsed = rt_tick_get_millisecond() - s_render_start_ms;
        s_metrics.render_count++;
        s_metrics.render_time_last_ms = elapsed;
        if (elapsed > s_metrics.render_time_max_ms)
            s_metrics.render_time_max_ms = elapsed;
    }
    else if (code == LV_EVENT_FLUSH_FINISH)
    {
        const lv_area_t *area = (const lv_area_t *)lv_event_get_param(event);
        s_metrics.flush_count++;
        if (area != RT_NULL)
        {
            uint32_t width = (uint32_t)lv_area_get_width(area);
            uint32_t height = (uint32_t)lv_area_get_height(area);
            s_metrics.flushed_pixels += width * height;
        }
    }
}

static void metrics_timer_cb(lv_timer_t *timer)
{
    rt_size_t total;
    rt_size_t used;
    rt_size_t max_used;
    uint32_t now;
    uint32_t elapsed;
    uint32_t refreshes;
    uint32_t renders;
    uint32_t flushes;
    uint32_t pixels;
    uint32_t objects;
    uint32_t gpu_tasks;
    uint32_t sw_tasks;
    uint32_t draw_tasks;
    uint32_t gpu_busy_us;
    uint32_t gpu_jobs;
    uint32_t total_elapsed;
    uint32_t gpu_busy_us_total;
    uint32_t gpu_jobs_total;
    lv_draw_runtime_stats_t draw_stats;

    LV_UNUSED(timer);
    now = rt_tick_get_millisecond();
    elapsed = now - s_last_sample_ms;
    refreshes = s_metrics.refresh_count - s_last_refresh_count;
    renders = s_metrics.render_count - s_last_render_count;
    flushes = s_metrics.flush_count - s_last_flush_count;
    pixels = s_metrics.flushed_pixels - s_last_flushed_pixels;
    lv_draw_runtime_stats_get(&draw_stats);
    gpu_tasks = draw_stats.executed_gpu_tasks - s_last_draw_stats.executed_gpu_tasks;
    sw_tasks = draw_stats.executed_sw_tasks - s_last_draw_stats.executed_sw_tasks;
    draw_tasks = gpu_tasks + sw_tasks;
    gpu_busy_us = draw_stats.gpu_busy_us_total - s_last_draw_stats.gpu_busy_us_total;
    gpu_jobs = draw_stats.gpu_completed_jobs - s_last_draw_stats.gpu_completed_jobs;
    total_elapsed = now - s_metrics_start_ms;
    gpu_busy_us_total = draw_stats.gpu_busy_us_total -
                        s_metrics_start_draw_stats.gpu_busy_us_total;
    gpu_jobs_total = draw_stats.gpu_completed_jobs -
                     s_metrics_start_draw_stats.gpu_completed_jobs;
    if (elapsed > 0U)
    {
        s_metrics.fps = counter_rate(renders, elapsed);
        s_metrics.refresh_fps = counter_rate(refreshes, elapsed);
        s_metrics.flushes_per_second = counter_rate(flushes, elapsed);
        s_metrics.flushed_pixels_per_second = counter_rate(pixels, elapsed);
        s_metrics.gpu_tasks_per_second = counter_rate(gpu_tasks, elapsed);
        s_metrics.sw_tasks_per_second = counter_rate(sw_tasks, elapsed);
        s_metrics.gpu_task_percent = draw_tasks == 0U ? 0U :
                                     (uint32_t)(((uint64_t)gpu_tasks * 100ULL) / draw_tasks);
        s_metrics.sw_label_tasks_per_second = counter_rate(
            draw_stats.sw_label_tasks - s_last_draw_stats.sw_label_tasks, elapsed);
        s_metrics.route_unit_switches_per_second = counter_rate(
            draw_stats.route_unit_switches - s_last_draw_stats.route_unit_switches, elapsed);
        s_metrics.gpu_submits_per_second = counter_rate(
            draw_stats.gpu_submit_count - s_last_draw_stats.gpu_submit_count, elapsed);
        s_metrics.gpu_submit_bytes_per_second = counter_rate(
            draw_stats.gpu_submit_bytes - s_last_draw_stats.gpu_submit_bytes, elapsed);
        s_metrics.gpu_flushes_per_second = counter_rate(
            draw_stats.gpu_flush_calls - s_last_draw_stats.gpu_flush_calls, elapsed);
        s_metrics.gpu_finishes_per_second = counter_rate(
            draw_stats.gpu_finish_calls - s_last_draw_stats.gpu_finish_calls, elapsed);
        s_metrics.gpu_finish_wait_ms_per_second = counter_rate(
            draw_stats.gpu_finish_wait_ms_total - s_last_draw_stats.gpu_finish_wait_ms_total,
            elapsed);
        s_metrics.gpu_busy_us_per_second = counter_rate(gpu_busy_us, elapsed);
        s_metrics.gpu_busy_percent = (uint32_t)(((uint64_t)gpu_busy_us * 100ULL) /
                                                ((uint64_t)elapsed * 1000ULL));
        if (s_metrics.gpu_busy_percent > 100U)
        {
            s_metrics.gpu_busy_percent = 100U;
        }
        if (s_metrics.gpu_busy_percent > s_metrics.gpu_busy_peak_percent)
        {
            s_metrics.gpu_busy_peak_percent = s_metrics.gpu_busy_percent;
        }
        s_metrics.gpu_jobs_per_second = counter_rate(gpu_jobs, elapsed);
        s_metrics.gpu_job_average_us = gpu_jobs == 0U ? 0U : gpu_busy_us / gpu_jobs;
    }
    s_metrics.gpu_busy_us_total = gpu_busy_us_total;
    s_metrics.gpu_completed_job_count = gpu_jobs_total;
    s_metrics.gpu_busy_average_percent = total_elapsed == 0U ? 0U :
        (uint32_t)(((uint64_t)gpu_busy_us_total * 100ULL) /
                   ((uint64_t)total_elapsed * 1000ULL));
    if (s_metrics.gpu_busy_average_percent > 100U)
    {
        s_metrics.gpu_busy_average_percent = 100U;
    }
    s_metrics.gpu_job_average_total_us = gpu_jobs_total == 0U ? 0U :
                                         gpu_busy_us_total / gpu_jobs_total;
    s_metrics.gpu_task_count = draw_stats.executed_gpu_tasks;
    s_metrics.sw_task_count = draw_stats.executed_sw_tasks;
    s_metrics.routed_gpu_task_count = draw_stats.routed_gpu_tasks;
    s_metrics.routed_sw_task_count = draw_stats.routed_sw_tasks;
    s_metrics.route_unit_switch_count = draw_stats.route_unit_switches;
    s_metrics.gpu_submit_count = draw_stats.gpu_submit_count;
    s_metrics.gpu_finish_wait_max_ms = draw_stats.gpu_finish_wait_ms_max;
    s_metrics.gpu_job_max_us = draw_stats.gpu_busy_us_max;
    s_last_refresh_count = s_metrics.refresh_count;
    s_last_render_count = s_metrics.render_count;
    s_last_flush_count = s_metrics.flush_count;
    s_last_flushed_pixels = s_metrics.flushed_pixels;
    s_last_sample_ms = now;
    s_last_draw_stats = draw_stats;

    rt_memory_info(&total, &used, &max_used);
    s_metrics.heap_total = (uint32_t)total;
    s_metrics.heap_used = (uint32_t)used;
    s_metrics.heap_max_used = (uint32_t)max_used;
    objects = count_objects(s_metrics_root);
    if (objects > s_metrics.peak_ui_objects)
    {
        s_metrics.peak_ui_objects = objects;
    }
}

void ft_metrics_init(lv_display_t *display, lv_obj_t *root)
{
    rt_memset(&s_metrics, 0, sizeof(s_metrics));
    s_metrics_root = root;
    s_last_refresh_count = 0U;
    s_last_render_count = 0U;
    s_last_flush_count = 0U;
    s_last_flushed_pixels = 0U;
    s_render_start_ms = 0U;
    s_last_sample_ms = rt_tick_get_millisecond();
    lv_draw_runtime_stats_get(&s_last_draw_stats);
    s_metrics_start_ms = s_last_sample_ms;
    s_metrics_start_draw_stats = s_last_draw_stats;
    lv_display_add_event_cb(display, display_event_cb, LV_EVENT_REFR_READY, RT_NULL);
    lv_display_add_event_cb(display, display_event_cb, LV_EVENT_RENDER_START, RT_NULL);
    lv_display_add_event_cb(display, display_event_cb, LV_EVENT_RENDER_READY, RT_NULL);
    lv_display_add_event_cb(display, display_event_cb, LV_EVENT_FLUSH_FINISH, RT_NULL);
    (void)lv_timer_create(metrics_timer_cb, 1000U, RT_NULL);
    metrics_timer_cb(RT_NULL);
}

void ft_metrics_get(ft_ui_metrics_t *metrics)
{
    if (metrics != RT_NULL)
    {
        *metrics = s_metrics;
    }
}

void ft_metrics_route_baseline(void)
{
    rt_size_t total;
    rt_size_t used;
    rt_size_t max_used;

    s_route_objects = count_objects(s_metrics_root);
    rt_memory_info(&total, &used, &max_used);
    s_route_heap = (uint32_t)used;
}

void ft_metrics_route_check(void)
{
    rt_size_t total;
    rt_size_t used;
    rt_size_t max_used;

    s_metrics.last_route_object_delta =
        (int32_t)count_objects(s_metrics_root) - (int32_t)s_route_objects;
    rt_memory_info(&total, &used, &max_used);
    s_metrics.last_route_heap_delta = (int32_t)used - (int32_t)s_route_heap;
}

void ft_metrics_print_status(void)
{
    rt_kprintf("FeatherTalk UI metrics: present-fps=%lu refresh-hz=%lu refresh=%lu "
               "renders=%lu flush=%lu/%lus pixels=%lu/%lus render-ms=%lu/%lu "
               "heap=%lu/%lu peak=%lu objects-peak=%lu route-delta=%ld/%ld\n",
               (unsigned long)s_metrics.fps,
               (unsigned long)s_metrics.refresh_fps,
               (unsigned long)s_metrics.refresh_count,
               (unsigned long)s_metrics.render_count,
               (unsigned long)s_metrics.flush_count,
               (unsigned long)s_metrics.flushes_per_second,
               (unsigned long)s_metrics.flushed_pixels,
               (unsigned long)s_metrics.flushed_pixels_per_second,
               (unsigned long)s_metrics.render_time_last_ms,
               (unsigned long)s_metrics.render_time_max_ms,
               (unsigned long)s_metrics.heap_used,
               (unsigned long)s_metrics.heap_total,
               (unsigned long)s_metrics.heap_max_used,
               (unsigned long)s_metrics.peak_ui_objects,
               (long)s_metrics.last_route_object_delta,
               (long)s_metrics.last_route_heap_delta);
    rt_kprintf("draw: gpu=%lu/%lus sw=%lu/%lus gpu-share=%lu%% sw-label=%lu/s "
               "route-gpu/sw=%lu/%lu switches=%lu/%lus\n"
               "vg-lite: submits=%lu/%lus cmd=%luB/s flush=%lu/s finish=%lu/s "
               "wait=%lums/s max=%lums\n"
               "gpu-hw: busy=%lu%% avg=%lu%% peak=%lu%% (%luus/s) "
               "jobs=%lu/s avg-job=%luus max=%luus total=%luus/%lujobs/%luus\n",
               (unsigned long)s_metrics.gpu_task_count,
               (unsigned long)s_metrics.gpu_tasks_per_second,
               (unsigned long)s_metrics.sw_task_count,
               (unsigned long)s_metrics.sw_tasks_per_second,
               (unsigned long)s_metrics.gpu_task_percent,
               (unsigned long)s_metrics.sw_label_tasks_per_second,
               (unsigned long)s_metrics.routed_gpu_task_count,
               (unsigned long)s_metrics.routed_sw_task_count,
               (unsigned long)s_metrics.route_unit_switch_count,
               (unsigned long)s_metrics.route_unit_switches_per_second,
               (unsigned long)s_metrics.gpu_submit_count,
               (unsigned long)s_metrics.gpu_submits_per_second,
               (unsigned long)s_metrics.gpu_submit_bytes_per_second,
               (unsigned long)s_metrics.gpu_flushes_per_second,
               (unsigned long)s_metrics.gpu_finishes_per_second,
               (unsigned long)s_metrics.gpu_finish_wait_ms_per_second,
               (unsigned long)s_metrics.gpu_finish_wait_max_ms,
               (unsigned long)s_metrics.gpu_busy_percent,
               (unsigned long)s_metrics.gpu_busy_average_percent,
               (unsigned long)s_metrics.gpu_busy_peak_percent,
               (unsigned long)s_metrics.gpu_busy_us_per_second,
               (unsigned long)s_metrics.gpu_jobs_per_second,
               (unsigned long)s_metrics.gpu_job_average_us,
               (unsigned long)s_metrics.gpu_job_max_us,
               (unsigned long)s_metrics.gpu_busy_us_total,
               (unsigned long)s_metrics.gpu_completed_job_count,
               (unsigned long)s_metrics.gpu_job_average_total_us);
}
