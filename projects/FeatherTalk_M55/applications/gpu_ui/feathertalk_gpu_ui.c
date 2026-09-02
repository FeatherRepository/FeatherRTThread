#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include <board.h>
#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#include "drv_lcd.h"
#include "drv_touch.h"
#include "feather_ui.h"
#include "feathertalk_gpu_scene.h"
#include "feathertalk_gpu_image.h"
#include "feathertalk_gpu_ui.h"

#define FT_UI_TARGET_FRAME_RATE 60U
#define FT_UI_LONG_PRESS_MS     600U

static uint16_t s_touch_width;
static uint16_t s_touch_height;
static uint16_t s_touch_raw_width;
static uint16_t s_touch_raw_height;
static int16_t s_touch_raw_x;
static int16_t s_touch_raw_y;
static int16_t s_touch_mapped_x;
static int16_t s_touch_mapped_y;
static uint32_t s_touch_samples;
static fui_touch_state_t s_touch_state;

#ifndef BSP_LCD_PHYSICAL_HOR_RES
#define BSP_LCD_PHYSICAL_HOR_RES 480U
#endif
#ifndef BSP_LCD_PHYSICAL_VER_RES
#define BSP_LCD_PHYSICAL_VER_RES 800U
#endif
#ifndef BSP_LCD_ROTATION_DEGREES
#define BSP_LCD_ROTATION_DEGREES 0
#endif

static int16_t touch_scale_axis(int16_t raw, uint16_t raw_extent,
                                uint16_t physical_extent)
{
    int32_t value = raw;
    if (value < 0) value = 0;
    if (raw_extent > 1U && value >= raw_extent) value = raw_extent - 1U;
    if (raw_extent > 1U && physical_extent > 1U && raw_extent != physical_extent)
        value = (value * (physical_extent - 1U) + (raw_extent - 1U) / 2U) /
                (raw_extent - 1U);
    return (int16_t)value;
}

static void touch_transform(int16_t raw_x, int16_t raw_y,
                            int16_t *screen_x, int16_t *screen_y)
{
    int32_t physical_x = touch_scale_axis(raw_x, s_touch_raw_width,
                                           BSP_LCD_PHYSICAL_HOR_RES);
    int32_t physical_y = touch_scale_axis(raw_y, s_touch_raw_height,
                                           BSP_LCD_PHYSICAL_VER_RES);
    int32_t x;
    int32_t y;
#if BSP_LCD_ROTATION_DEGREES == 90
    x = physical_y;
    y = (int32_t)BSP_LCD_PHYSICAL_HOR_RES - 1 - physical_x;
#elif BSP_LCD_ROTATION_DEGREES == 180
    x = (int32_t)BSP_LCD_PHYSICAL_HOR_RES - 1 - physical_x;
    y = (int32_t)BSP_LCD_PHYSICAL_VER_RES - 1 - physical_y;
#elif BSP_LCD_ROTATION_DEGREES == 270
    x = (int32_t)BSP_LCD_PHYSICAL_VER_RES - 1 - physical_y;
    y = physical_x;
#else
    x = physical_x;
    y = physical_y;
#endif
    if (x < 0) x = 0;
    else if (x >= s_touch_width) x = s_touch_width - 1U;
    if (y < 0) y = 0;
    else if (y >= s_touch_height) y = s_touch_height - 1U;
    *screen_x = (int16_t)x;
    *screen_y = (int16_t)y;
}

static bool touch_read(fui_touch_sample_t *sample, void *user_data)
{
    static int16_t last_x;
    static int16_t last_y;
    rt_int16_t x = 0, y = 0;
    (void)user_data;
    if (sample == RT_NULL || s_touch_width == 0U || s_touch_height == 0U)
        return false;
    if (ST7102_get_single_touch(&x, &y) == CY_RSLT_SUCCESS)
    {
        s_touch_raw_x = x;
        s_touch_raw_y = y;
        touch_transform(x, y, &last_x, &last_y);
        sample->state = FUI_TOUCH_PRESSED;
    }
    else sample->state = FUI_TOUCH_RELEASED;
    sample->x = last_x;
    sample->y = last_y;
    sample->timestamp_ms = rt_tick_get_millisecond();
    s_touch_mapped_x = last_x;
    s_touch_mapped_y = last_y;
    s_touch_state = sample->state;
    s_touch_samples++;
    return true;
}

static int present_frame(void *framebuffer, void *user_data)
{
    (void)user_data;
    return lcd_gpu_surface_present(framebuffer);
}

int feathertalk_gpu_ui_init(void)
{
    lcd_gpu_surface_info_t surface;
    fui_engine_config_t config;
    if (lcd_gpu_surface_get_info(&surface) != RT_EOK || surface.framebuffer_count != 2U)
    {
        rt_kprintf("[FeatherUI] direct scanout surfaces unavailable\n");
        return -RT_ERROR;
    }
    if (rt_hw_ST7102_port() != CY_RSLT_SUCCESS)
    {
        rt_kprintf("[FeatherUI] touch init failed\n");
        return -RT_ERROR;
    }
    s_touch_width = surface.width;
    s_touch_height = surface.height;
    s_touch_raw_width = BSP_LCD_PHYSICAL_HOR_RES;
    s_touch_raw_height = BSP_LCD_PHYSICAL_VER_RES;
    if (ST7102_get_resolution(&s_touch_raw_width, &s_touch_raw_height) != RT_EOK)
    {
        s_touch_raw_width = BSP_LCD_PHYSICAL_HOR_RES;
        s_touch_raw_height = BSP_LCD_PHYSICAL_VER_RES;
    }
    if (ft_gpu_scene_init(surface.width, surface.height) != RT_EOK) return -RT_ERROR;
    memset(&config, 0, sizeof(config));
    config.width = surface.width;
    config.height = surface.height;
    config.stride_pixels = surface.stride_pixels;
    config.framebuffers[0] = lcd_gpu_surface_get(0U);
    config.framebuffers[1] = lcd_gpu_surface_get(1U);
    config.collect = ft_gpu_scene_collect;
    config.event = ft_gpu_scene_event;
    config.touch_read = touch_read;
    config.present = present_frame;
    config.idle_poll_ms = 5U;
    config.tap_distance_px = (uint16_t)((surface.width < surface.height ?
        surface.width : surface.height) / 40U);
    if (config.tap_distance_px < 6U) config.tap_distance_px = 6U;
    if (config.tap_distance_px > 18U) config.tap_distance_px = 18U;
    config.long_press_ms = FT_UI_LONG_PRESS_MS;
    config.frame_interval_ms = (uint16_t)(1000U / FT_UI_TARGET_FRAME_RATE);
    if (fui_engine_init(&config) != 0 || fui_engine_start() != 0) return -RT_ERROR;
    rt_kprintf("[FeatherUI] %u.%u.%u GPU shell: %ux%u stride=%u, %u pages\n",
               FUI_VERSION_MAJOR, FUI_VERSION_MINOR, FUI_VERSION_PATCH,
               surface.width, surface.height, surface.stride_pixels,
               FT_GPU_PAGE_COUNT);
    rt_kprintf("[FeatherUI] touch raw=%ux%u physical=%ux%u rotation=%u poll=%u ms\n",
               s_touch_raw_width, s_touch_raw_height,
               (unsigned int)BSP_LCD_PHYSICAL_HOR_RES,
               (unsigned int)BSP_LCD_PHYSICAL_VER_RES,
               (unsigned int)BSP_LCD_ROTATION_DEGREES,
               config.idle_poll_ms);
    return RT_EOK;
}

#ifdef RT_USING_FINSH
static int feather_ui_stats(int argc, char **argv)
{
    fui_engine_stats_t s;
    fui_animation_stats_t animations;
    lcd_gpu_surface_info_t surfaces;
    (void)argc; (void)argv;
    fui_engine_get_stats(&s);
    fui_animation_get_stats(&animations);
    rt_kprintf("FeatherUI page=%u depth=%u select=%u dialog=%u frames=%lu/%lu failed=%lu error=%ld cmds=%lu peak=%lu overflow=%lu\n",
               (unsigned int)ft_gpu_scene_current_page(), (unsigned int)ft_gpu_scene_route_depth(),
               ft_gpu_scene_select_visible() ? 1U : 0U,
               ft_gpu_scene_dialog_visible() ? 1U : 0U,
               (unsigned long)s.frames_presented, (unsigned long)s.frames_collected,
               (unsigned long)s.frames_failed, (long)s.render_error_last,
               (unsigned long)s.commands_last,
               (unsigned long)s.commands_peak, (unsigned long)s.list_overflows);
    rt_kprintf("frame=%lu us collect=%lu render=%lu gpu=%lu present=%lu max=%lu\n",
               (unsigned long)s.frame_us_last, (unsigned long)s.collect_us_last,
               (unsigned long)s.render_us_last, (unsigned long)s.gpu_busy_us_last,
               (unsigned long)s.present_us_last, (unsigned long)s.frame_us_max);
    rt_kprintf("encode=%lu us clear=%lu/%lu path=%lu/%lu blit=%lu/%lu batch=%lu/%lu\n",
               (unsigned long)s.encode_us_last, (unsigned long)s.clear_encode_us_last,
               (unsigned long)s.clear_calls_last, (unsigned long)s.path_encode_us_last,
               (unsigned long)s.path_calls_last, (unsigned long)s.blit_encode_us_last,
               (unsigned long)s.blit_calls_last, (unsigned long)s.path_primitives_last,
               (unsigned long)s.path_batch_peak);
    rt_kprintf("GPU submit=%lu complete=%lu last-bytes=%lu total-bytes=%lu contract=%lu\n",
               (unsigned long)s.gpu_submit_count, (unsigned long)s.gpu_completed_jobs,
               (unsigned long)s.gpu_submit_bytes_last, (unsigned long)s.gpu_submit_bytes,
               (unsigned long)s.contract_violations);
    rt_kprintf("animations active=%u peak=%u started=%lu completed=%lu cancelled=%lu\n",
               animations.active, animations.peak_active,
               (unsigned long)animations.started,
               (unsigned long)animations.completed,
               (unsigned long)animations.cancelled);
    rt_kprintf("events queued=%lu dispatched=%lu failed=%lu latency=%lu/%lu ms\n",
               (unsigned long)s.events_queued,
               (unsigned long)s.events_dispatched,
               (unsigned long)s.events_queue_failed,
               (unsigned long)s.event_latency_ms_last,
               (unsigned long)s.event_latency_ms_max);
    rt_kprintf("touch samples=%lu state=%u raw=%d,%d mapped=%d,%d extent=%ux%u\n",
               (unsigned long)s_touch_samples, (unsigned int)s_touch_state,
               s_touch_raw_x, s_touch_raw_y,
               s_touch_mapped_x, s_touch_mapped_y,
               s_touch_raw_width, s_touch_raw_height);
    if (lcd_gpu_surface_get_info(&surfaces) == RT_EOK)
        rt_kprintf("surfaces=%p/%p size=%ux%u stride=%u bytes=%lu\n",
                   lcd_gpu_surface_get(0U), lcd_gpu_surface_get(1U),
                   surfaces.width, surfaces.height, surfaces.stride_pixels,
                   (unsigned long)surfaces.stride_pixels * surfaces.height * 2UL);
    return 0;
}
MSH_CMD_EXPORT(feather_ui_stats, show FeatherUI scene and GPU statistics);

static int feather_ui_image(int argc, char **argv)
{
    unsigned int slot;
    (void)argc; (void)argv;
    for (slot = 0U; slot < FT_GPU_IMAGE_SLOT_COUNT; slot++)
    {
        ft_gpu_image_info_t image;
        if (!ft_gpu_image_get((ft_gpu_image_slot_t)slot, &image)) continue;
        rt_kprintf("image[%u] state=%u visible=%ux%u stride=%u source=%ux%u "
                   "pixels=%p decode=%lu ms path=%s error=%s\n",
                   slot, (unsigned int)image.state,
                   image.image.width, image.image.height,
                   image.image.stride_pixels,
                   image.source_width, image.source_height,
                   image.image.pixels, (unsigned long)image.decode_ms,
                   image.path[0] != '\0' ? image.path : "-",
                   image.error[0] != '\0' ? image.error : "-");
    }
    return 0;
}
MSH_CMD_EXPORT(feather_ui_image, show decoded GPU image slots);

static int feather_ui_bench(int argc, char **argv)
{
    fui_engine_stats_t before, after;
    uint32_t frames = 120U, start_ms, elapsed_ms, rendered;
    if (argc > 1)
    {
        long requested = strtol(argv[1], RT_NULL, 10);
        if (requested < 1L || requested > 1000L) return -RT_EINVAL;
        frames = (uint32_t)requested;
    }
    fui_engine_get_stats(&before);
    start_ms = rt_tick_get_millisecond();
    fui_engine_benchmark_start(frames);
    while (fui_engine_benchmark_remaining() != 0U &&
           rt_tick_get_millisecond() - start_ms < frames * 100U + 2000U)
        rt_thread_mdelay(10U);
    elapsed_ms = rt_tick_get_millisecond() - start_ms;
    fui_engine_get_stats(&after);
    rendered = after.frames_presented - before.frames_presented;
    if (rendered == 0U) return -RT_ERROR;
    rt_kprintf("FeatherUI bench frames=%lu elapsed=%lu ms throughput=%lu.%lu fps\n",
               (unsigned long)rendered, (unsigned long)elapsed_ms,
               (unsigned long)((rendered * 1000U) / elapsed_ms),
               (unsigned long)(((rendered * 10000U) / elapsed_ms) % 10U));
    rt_kprintf("avg collect=%lu encode=%lu render=%lu gpu=%lu present=%lu frame=%lu us\n",
               (unsigned long)((after.collect_us_total - before.collect_us_total) / rendered),
               (unsigned long)((after.encode_us_total - before.encode_us_total) / rendered),
               (unsigned long)((after.render_us_total - before.render_us_total) / rendered),
               (unsigned long)((after.gpu_busy_us_total - before.gpu_busy_us_total) / rendered),
               (unsigned long)((after.present_us_total - before.present_us_total) / rendered),
               (unsigned long)((after.frame_us_total - before.frame_us_total) / rendered));
    rt_kprintf("submits=%lu completes=%lu contract-delta=%lu bytes/frame=%lu\n",
               (unsigned long)(after.gpu_submit_count - before.gpu_submit_count),
               (unsigned long)(after.gpu_completed_jobs - before.gpu_completed_jobs),
               (unsigned long)(after.contract_violations - before.contract_violations),
               (unsigned long)((after.gpu_submit_bytes - before.gpu_submit_bytes) / rendered));
    return fui_engine_benchmark_remaining() == 0U ? 0 : -RT_ETIMEOUT;
}
MSH_CMD_EXPORT(feather_ui_bench, benchmark one-submit GPU frames);

static int feather_ui_pause(int argc, char **argv)
{
    bool paused;
    if (argc != 2 || (strcmp(argv[1], "0") != 0 && strcmp(argv[1], "1") != 0))
        return -RT_EINVAL;
    paused = strcmp(argv[1], "1") == 0;
    fui_engine_set_paused(paused);
    rt_kprintf("FeatherUI rendering %s\n", paused ? "paused" : "running");
    return 0;
}
MSH_CMD_EXPORT(feather_ui_pause, pause rendering for framebuffer capture);

static int feather_ui_test(int argc, char **argv)
{
    (void)argc; (void)argv;
    return ft_gpu_scene_run_test();
}
MSH_CMD_EXPORT(feather_ui_test, exercise all GPU UI routes and bindings);

static int feather_ui_page(int argc, char **argv)
{
    long page;
    if (argc != 2) return -RT_EINVAL;
    page = strtol(argv[1], RT_NULL, 10);
    if (page < 0 || page >= FT_GPU_PAGE_COUNT) return -RT_EINVAL;
    return ft_gpu_scene_open((ft_gpu_page_t)page);
}
MSH_CMD_EXPORT(feather_ui_page, open a GPU UI page by numeric id);

static int feather_ui_event(int argc, char **argv)
{
    fui_event_t event;
    int result;
    if (argc < 4 || argc > 6)
    {
        rt_kprintf("usage: feather_ui_event down|move|up|tap|long x y [dx dy]\n");
        return -RT_EINVAL;
    }
    memset(&event, 0, sizeof(event));
    if (strcmp(argv[1], "down") == 0) event.type = FUI_EVENT_TOUCH_DOWN;
    else if (strcmp(argv[1], "move") == 0) event.type = FUI_EVENT_TOUCH_MOVE;
    else if (strcmp(argv[1], "up") == 0) event.type = FUI_EVENT_TOUCH_UP;
    else if (strcmp(argv[1], "tap") == 0) event.type = FUI_EVENT_TAP;
    else if (strcmp(argv[1], "long") == 0) event.type = FUI_EVENT_LONG_PRESS;
    else return -RT_EINVAL;
    event.x = (int16_t)strtol(argv[2], RT_NULL, 10);
    event.y = (int16_t)strtol(argv[3], RT_NULL, 10);
    if (argc > 4) event.delta_x = (int16_t)strtol(argv[4], RT_NULL, 10);
    if (argc > 5) event.delta_y = (int16_t)strtol(argv[5], RT_NULL, 10);
    event.timestamp_ms = rt_tick_get_millisecond();
    result = fui_engine_post_event(&event);
    rt_kprintf("FeatherUI event %s (%d,%d) queue=%d\n",
               argv[1], event.x, event.y, result);
    return result;
}
MSH_CMD_EXPORT(feather_ui_event, inject a queued touch event for UI automation);
#endif
