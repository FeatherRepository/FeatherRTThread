#include <rtthread.h>
#include "feathertalk_ui_internal.h"

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

    LV_UNUSED(timer);
    now = rt_tick_get_millisecond();
    elapsed = now - s_last_sample_ms;
    refreshes = s_metrics.refresh_count - s_last_refresh_count;
    renders = s_metrics.render_count - s_last_render_count;
    flushes = s_metrics.flush_count - s_last_flush_count;
    pixels = s_metrics.flushed_pixels - s_last_flushed_pixels;
    if (elapsed > 0U)
    {
        s_metrics.fps = (renders * 1000U) / elapsed;
        s_metrics.refresh_fps = (refreshes * 1000U) / elapsed;
        s_metrics.flushes_per_second = (flushes * 1000U) / elapsed;
        s_metrics.flushed_pixels_per_second =
            (uint32_t)(((uint64_t)pixels * 1000ULL) / elapsed);
    }
    s_last_refresh_count = s_metrics.refresh_count;
    s_last_render_count = s_metrics.render_count;
    s_last_flush_count = s_metrics.flush_count;
    s_last_flushed_pixels = s_metrics.flushed_pixels;
    s_last_sample_ms = now;

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
}
