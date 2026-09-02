#include <rtthread.h>
#include "feathertalk_ui_internal.h"
#include "lv_draw_runtime_stats.h"
#include "lv_gpu_batch.h"
#include "drv_lcd.h"
#include "ipc/feathertalk_ipc.h"

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
static volatile uint32_t s_bench_requested_frames;
static volatile bool s_bench_active;
static uint32_t s_bench_frames_remaining;
static uint32_t s_bench_frames_requested;
static uint32_t s_bench_start_ms;
static uint32_t s_bench_start_render_count;
static lv_draw_runtime_stats_t s_bench_start_draw_stats;
static lv_gpu_glyph_cache_stats_t s_bench_start_glyph_cache_stats;
static rt_thread_t s_bench_watchdog_thread;

static void bench_watchdog_sample(void)
{
    lv_draw_runtime_stats_t draw_stats;
    lcd_gpu_surface_stats_t lcd_stats;
    lv_gpu_batch_debug_t batch_debug;
    uint32_t rendered;
    uint32_t batch_frames;
    uint32_t gpu_jobs;

    if (!s_bench_active)
        return;

    lv_draw_runtime_stats_get(&draw_stats);
    lcd_gpu_surface_get_stats(&lcd_stats);
    lv_gpu_batch_debug_get(&batch_debug);
    rendered = s_metrics.render_count - s_bench_start_render_count;
    batch_frames = draw_stats.gpu_batch_frames -
                   s_bench_start_draw_stats.gpu_batch_frames;
    gpu_jobs = draw_stats.gpu_completed_jobs -
               s_bench_start_draw_stats.gpu_completed_jobs;
    rt_kprintf("[UI-BENCH-WATCH] elapsed=%lums rendered=%lu remaining=%lu "
               "batch=%lu jobs=%lu stage=%lu active=%u pending=%u "
               "inflight=%u slots=%u/%u presents=%lu frame-irqs=%lu\n",
               (unsigned long)(rt_tick_get_millisecond() - s_bench_start_ms),
               (unsigned long)rendered,
               (unsigned long)s_bench_frames_remaining,
               (unsigned long)batch_frames,
               (unsigned long)gpu_jobs,
               (unsigned long)batch_debug.stage,
               batch_debug.active ? 1U : 0U,
               batch_debug.commands_pending ? 1U : 0U,
               batch_debug.inflight ? 1U : 0U,
               (unsigned int)batch_debug.active_slot,
               (unsigned int)batch_debug.inflight_slot,
               (unsigned long)lcd_stats.presents,
               (unsigned long)lcd_stats.dc_frame_irqs);
}

static void bench_watchdog_entry(void *parameter)
{
    uint32_t observed_start_ms = 0U;
    uint32_t last_rendered = 0U;

    RT_UNUSED(parameter);
    while (true)
    {
        rt_thread_mdelay(1000U);
        if (!s_bench_active)
        {
            observed_start_ms = 0U;
            continue;
        }

        if (observed_start_ms != s_bench_start_ms)
        {
            observed_start_ms = s_bench_start_ms;
            last_rendered = s_metrics.render_count;
            continue;
        }

        if (last_rendered == s_metrics.render_count)
            bench_watchdog_sample();
        last_rendered = s_metrics.render_count;
    }
}

static void bench_invalidate_async(void *user_data)
{
    LV_UNUSED(user_data);
    if (s_bench_active && s_metrics_root != RT_NULL &&
        lv_obj_is_valid(s_metrics_root))
        lv_obj_invalidate(s_metrics_root);
}

static void bench_complete(void)
{
    lv_draw_runtime_stats_t draw_stats;
    uint32_t rendered;
    uint32_t elapsed;
    uint32_t batch_frames;
    uint32_t gpu_busy_us;
    uint32_t gpu_jobs;
    uint32_t gpu_busy_percent_x100;
    uint32_t glyph_draw_us;
    uint32_t label_encode_us;
    uint32_t drain_ms;
    uint32_t gpu_wait_count;
    uint32_t scanout_wait_count;
    lv_gpu_glyph_cache_stats_t glyph_cache_stats;

    /* LV_EVENT_RENDER_READY is emitted after the final frame is submitted, not
     * after its GPU job and display commit retire. Drain once so the benchmark
     * covers the complete end-to-end pipeline and never reports N frames with
     * only N-1 completed GPU jobs. */
    drain_ms = rt_tick_get_millisecond();
    lv_gpu_batch_wait_idle();
    drain_ms = rt_tick_get_millisecond() - drain_ms;
    lv_draw_runtime_stats_get(&draw_stats);
    lv_gpu_batch_glyph_cache_stats_get(&glyph_cache_stats);
    rendered = s_metrics.render_count - s_bench_start_render_count;
    elapsed = rt_tick_get_millisecond() - s_bench_start_ms;
    batch_frames = draw_stats.gpu_batch_frames - s_bench_start_draw_stats.gpu_batch_frames;
    gpu_busy_us = draw_stats.gpu_busy_us_total - s_bench_start_draw_stats.gpu_busy_us_total;
    gpu_jobs = draw_stats.gpu_completed_jobs - s_bench_start_draw_stats.gpu_completed_jobs;
    gpu_wait_count = draw_stats.gpu_pipeline_wait_count -
                     s_bench_start_draw_stats.gpu_pipeline_wait_count;
    scanout_wait_count = draw_stats.scanout_pipeline_wait_count -
                         s_bench_start_draw_stats.scanout_pipeline_wait_count;
    gpu_busy_percent_x100 = elapsed == 0U ? 0U :
        (uint32_t)(((uint64_t)gpu_busy_us * 10000ULL) /
                   ((uint64_t)elapsed * 1000ULL));
    glyph_draw_us = batch_frames == 0U ? 0U :
        (draw_stats.gpu_glyph_draw_us_total -
         s_bench_start_draw_stats.gpu_glyph_draw_us_total) / batch_frames;
    label_encode_us = batch_frames == 0U ? 0U :
        (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_LABEL] -
         s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_LABEL]) /
        batch_frames;
    rt_kprintf("[UI-BENCH] complete requested=%lu rendered=%lu elapsed=%lums fps=%lu.%02lu "
               "batch=%lu submits=%lu collect=%luus encode=%luus finish=%luus "
               "retire-gpu=%luus scanout=%luus drain=%lums "
               "gpu-busy=%lu.%02lu%% jobs=%lu\n",
               (unsigned long)s_bench_frames_requested,
               (unsigned long)rendered,
               (unsigned long)elapsed,
               (unsigned long)(elapsed == 0U ? 0U :
                   ((uint64_t)rendered * 1000ULL) / elapsed),
               (unsigned long)(elapsed == 0U ? 0U :
                   ((((uint64_t)rendered * 100000ULL) / elapsed) % 100ULL)),
               (unsigned long)batch_frames,
               (unsigned long)(draw_stats.gpu_submit_count -
                               s_bench_start_draw_stats.gpu_submit_count),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_batch_collect_us_total -
                    s_bench_start_draw_stats.gpu_batch_collect_us_total) / batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_batch_encode_us_total -
                    s_bench_start_draw_stats.gpu_batch_encode_us_total) / batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_batch_finish_us_total -
                    s_bench_start_draw_stats.gpu_batch_finish_us_total) / batch_frames),
               (unsigned long)(gpu_wait_count == 0U ? 0U :
                   (draw_stats.gpu_pipeline_wait_us_total -
                    s_bench_start_draw_stats.gpu_pipeline_wait_us_total) / gpu_wait_count),
               (unsigned long)(scanout_wait_count == 0U ? 0U :
                   (draw_stats.scanout_pipeline_wait_us_total -
                    s_bench_start_draw_stats.scanout_pipeline_wait_us_total) /
                   scanout_wait_count),
               (unsigned long)drain_ms,
               (unsigned long)(gpu_busy_percent_x100 / 100U),
               (unsigned long)(gpu_busy_percent_x100 % 100U),
               (unsigned long)gpu_jobs);
    rt_kprintf("[UI-BENCH] encode-us/frame fill=%lu border=%lu shadow=%lu label=%lu "
               "image=%lu layer=%lu line=%lu arc=%lu triangle=%lu mask=%lu vector=%lu\n",
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_FILL] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_FILL]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_BORDER] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_BORDER]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_BOX_SHADOW] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_BOX_SHADOW]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_LABEL] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_LABEL]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_IMAGE] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_IMAGE]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_LAYER] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_LAYER]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_LINE] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_LINE]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_ARC] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_ARC]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_TRIANGLE] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_TRIANGLE]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_MASK_RECTANGLE] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_MASK_RECTANGLE]) /
                   batch_frames),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_VECTOR] -
                    s_bench_start_draw_stats.gpu_task_encode_us_total[LV_DRAW_TASK_TYPE_VECTOR]) /
                   batch_frames));
    rt_kprintf("[UI-BENCH] glyph-cache bitmap=%lu/%lu store=%lu used=%luB overflow=%lu dsc=%lu/%lu\n",
               (unsigned long)(glyph_cache_stats.hits - s_bench_start_glyph_cache_stats.hits),
               (unsigned long)(glyph_cache_stats.misses - s_bench_start_glyph_cache_stats.misses),
               (unsigned long)(glyph_cache_stats.stores - s_bench_start_glyph_cache_stats.stores),
               (unsigned long)glyph_cache_stats.bytes_used,
               (unsigned long)(glyph_cache_stats.overflows - s_bench_start_glyph_cache_stats.overflows),
               (unsigned long)(glyph_cache_stats.descriptor_hits -
                               s_bench_start_glyph_cache_stats.descriptor_hits),
               (unsigned long)(glyph_cache_stats.descriptor_misses -
                               s_bench_start_glyph_cache_stats.descriptor_misses));
    rt_kprintf("[UI-BENCH] label-split glyph-draw=%luus layout/font=%luus glyphs/frame=%lu\n",
               (unsigned long)glyph_draw_us,
               (unsigned long)(label_encode_us > glyph_draw_us ?
                               label_encode_us - glyph_draw_us : 0U),
               (unsigned long)(batch_frames == 0U ? 0U :
                   (draw_stats.gpu_glyph_draw_count -
                    s_bench_start_draw_stats.gpu_glyph_draw_count) / batch_frames));
    s_bench_active = false;
    feathertalk_ipc_set_periodic_report_enabled(true);
}

static void bench_start_if_requested(void)
{
    uint32_t requested = s_bench_requested_frames;

    if (requested == 0U || s_bench_active) return;
    s_bench_requested_frames = 0U;
    s_bench_frames_requested = requested;
    s_bench_frames_remaining = requested;
    /* Exclude a job submitted by the normal UI cadence immediately before the
     * benchmark. Without this retirement an animated scene can report 61 GPU
     * completions for 60 measured renders. */
    lv_gpu_batch_wait_idle();
    s_bench_start_ms = rt_tick_get_millisecond();
    s_bench_start_render_count = s_metrics.render_count;
    lv_draw_runtime_stats_get(&s_bench_start_draw_stats);
    lv_gpu_batch_glyph_cache_stats_get(&s_bench_start_glyph_cache_stats);
    s_bench_active = true;
    feathertalk_ipc_set_periodic_report_enabled(false);
    bench_invalidate_async(RT_NULL);
    rt_kprintf("[UI-BENCH] started %lu full-screen frames\n",
               (unsigned long)requested);
}

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
        if (s_bench_active && s_bench_frames_remaining > 0U)
        {
            s_bench_frames_remaining--;
            if (s_bench_frames_remaining == 0U)
                bench_complete();
            else
                (void)lv_async_call(bench_invalidate_async, RT_NULL);
        }
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
    bench_start_if_requested();
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
    if (s_bench_watchdog_thread == RT_NULL)
    {
        s_bench_watchdog_thread = rt_thread_create("uibwatch",
                                                   bench_watchdog_entry,
                                                   RT_NULL,
                                                   4096U,
                                                   8U,
                                                   20U);
        if (s_bench_watchdog_thread != RT_NULL)
            (void)rt_thread_startup(s_bench_watchdog_thread);
    }
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
    lv_draw_runtime_stats_t draw_stats;
    lcd_gpu_surface_stats_t lcd_stats;
    lv_gpu_batch_debug_t batch_debug;

    lv_draw_runtime_stats_get(&draw_stats);
    lcd_gpu_surface_get_stats(&lcd_stats);
    lv_gpu_batch_debug_get(&batch_debug);
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
    rt_kprintf("gpu-batch: frames=%lu submits/frame=%lu.%02lu max=%lu "
               "tasks/frame-max=%lu boundaries-sw/resource/explicit=%lu/%lu/%lu "
               "transient=%luB overflows=%lu\n",
               (unsigned long)draw_stats.gpu_batch_frames,
               (unsigned long)(draw_stats.gpu_batch_frames == 0U ? 0U :
                   draw_stats.gpu_batch_frame_submits_total / draw_stats.gpu_batch_frames),
               (unsigned long)(draw_stats.gpu_batch_frames == 0U ? 0U :
                   ((draw_stats.gpu_batch_frame_submits_total % draw_stats.gpu_batch_frames) * 100U) /
                   draw_stats.gpu_batch_frames),
               (unsigned long)draw_stats.gpu_batch_frame_submits_max,
               (unsigned long)draw_stats.gpu_batch_frame_tasks_max,
               (unsigned long)draw_stats.gpu_batch_software_boundaries,
               (unsigned long)draw_stats.gpu_batch_resource_boundaries,
               (unsigned long)draw_stats.gpu_batch_explicit_boundaries,
               (unsigned long)draw_stats.gpu_batch_transient_bytes_max,
               (unsigned long)draw_stats.gpu_batch_transient_overflows);
    rt_kprintf("gpu-pipeline state: stage=%lu active=%u pending=%u inflight=%u "
               "slots=%u/%u\n",
               (unsigned long)batch_debug.stage,
               batch_debug.active ? 1U : 0U,
               batch_debug.commands_pending ? 1U : 0U,
               batch_debug.inflight ? 1U : 0U,
               (unsigned int)batch_debug.active_slot,
               (unsigned int)batch_debug.inflight_slot);
    rt_kprintf("gpu-batch phase avg/max: collect=%lu/%luus encode=%lu/%luus "
               "finish=%lu/%luus\n"
               "pipeline wait avg/max: gpu=%lu/%luus scanout=%lu/%luus\n"
               "scanout: presents=%lu frame-irqs=%lu wait-avg/max=%lu/%lums "
               "timeouts=%lu\n",
               (unsigned long)(draw_stats.gpu_batch_frames == 0U ? 0U :
                   draw_stats.gpu_batch_collect_us_total / draw_stats.gpu_batch_frames),
               (unsigned long)draw_stats.gpu_batch_collect_us_max,
               (unsigned long)(draw_stats.gpu_batch_frames == 0U ? 0U :
                   draw_stats.gpu_batch_encode_us_total / draw_stats.gpu_batch_frames),
               (unsigned long)draw_stats.gpu_batch_encode_us_max,
               (unsigned long)(draw_stats.gpu_batch_frames == 0U ? 0U :
                   draw_stats.gpu_batch_finish_us_total / draw_stats.gpu_batch_frames),
               (unsigned long)draw_stats.gpu_batch_finish_us_max,
               (unsigned long)(draw_stats.gpu_pipeline_wait_count == 0U ? 0U :
                   draw_stats.gpu_pipeline_wait_us_total /
                   draw_stats.gpu_pipeline_wait_count),
               (unsigned long)draw_stats.gpu_pipeline_wait_us_max,
               (unsigned long)(draw_stats.scanout_pipeline_wait_count == 0U ? 0U :
                   draw_stats.scanout_pipeline_wait_us_total /
                   draw_stats.scanout_pipeline_wait_count),
               (unsigned long)draw_stats.scanout_pipeline_wait_us_max,
               (unsigned long)lcd_stats.presents,
               (unsigned long)lcd_stats.dc_frame_irqs,
               (unsigned long)(lcd_stats.presents == 0U ? 0U :
                   lcd_stats.present_wait_ms_total / lcd_stats.presents),
               (unsigned long)lcd_stats.present_wait_ms_max,
               (unsigned long)lcd_stats.present_timeouts);
}

int ft_metrics_bench_request(uint32_t frames)
{
    if (frames == 0U || frames > 600U) return -RT_EINVAL;
    if (s_bench_requested_frames != 0U || s_bench_active)
        return -RT_EBUSY;
    s_bench_requested_frames = frames;
    return RT_EOK;
}
