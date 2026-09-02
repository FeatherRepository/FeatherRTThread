#include <string.h>
#include <board.h>
#include "fui_internal.h"

#define FUI_THREAD_STACK_SIZE 8192U
#define FUI_THREAD_PRIORITY   18U
#define FUI_INPUT_STACK_SIZE  4096U
#define FUI_INPUT_PRIORITY    17U
#define FUI_EVENT_QUEUE_DEPTH 32U
#define FUI_DEFAULT_TAP_DISTANCE_PX 12U
#define FUI_DEFAULT_LONG_PRESS_MS   600U
#define FUI_DEFAULT_FRAME_MS        16U

typedef struct
{
    fui_engine_config_t config;
    fui_display_list_t list;
    fui_painter_t painter;
    fui_engine_stats_t stats;
    rt_thread_t thread;
    rt_thread_t input_thread;
    rt_mq_t event_queue;
    volatile bool running;
    volatile bool dirty;
    volatile bool paused;
    volatile uint32_t benchmark_remaining;
    uint8_t target_index;
    fui_touch_sample_t last_touch;
    int16_t touch_down_x;
    int16_t touch_down_y;
    uint32_t touch_down_ms;
    bool long_press_sent;
} fui_engine_t;

static fui_engine_t s_engine;
volatile uint32_t g_fui_gpu_submit_count;
volatile uint32_t g_fui_gpu_submit_bytes;
volatile uint32_t g_fui_gpu_completed_jobs;
volatile uint32_t g_fui_gpu_busy_cycles;
volatile uint32_t g_fui_gpu_busy_last_cycles;
static volatile uint32_t s_gpu_submit_cycle;

uint32_t fui_clock_cycles(void)
{
    return DWT->CYCCNT;
}

uint32_t fui_cycles_to_us(uint32_t cycles)
{
    uint32_t mhz = SystemCoreClock / 1000000U;
    return mhz == 0U ? 0U : cycles / mhz;
}

void vg_lite_submit_perf_hook(uint32_t command_bytes)
{
    g_fui_gpu_submit_count++;
    g_fui_gpu_submit_bytes += command_bytes;
    s_gpu_submit_cycle = fui_clock_cycles();
}

void vg_lite_hardware_complete_perf_hook(void)
{
    uint32_t elapsed = fui_clock_cycles() - s_gpu_submit_cycle;
    g_fui_gpu_completed_jobs++;
    g_fui_gpu_busy_last_cycles = elapsed;
    g_fui_gpu_busy_cycles += elapsed;
}

static void dispatch_event(fui_event_type_t type, int16_t x, int16_t y,
                           int16_t dx, int16_t dy, uint32_t now)
{
    fui_event_t event;
    if (s_engine.config.event == RT_NULL) return;
    event.type = type;
    event.x = x;
    event.y = y;
    event.delta_x = dx;
    event.delta_y = dy;
    event.timestamp_ms = now;
    if (s_engine.config.event(&event, s_engine.config.user_data))
        s_engine.dirty = true;
}

static void queue_event(fui_event_type_t type, int16_t x, int16_t y,
                        int16_t dx, int16_t dy, uint32_t now)
{
    fui_event_t event;
    if (s_engine.event_queue == RT_NULL) return;
    event.type = type;
    event.x = x;
    event.y = y;
    event.delta_x = dx;
    event.delta_y = dy;
    event.timestamp_ms = now;
    if (rt_mq_send(s_engine.event_queue, &event, sizeof(event)) == RT_EOK)
        s_engine.stats.events_queued++;
    else
        s_engine.stats.events_queue_failed++;
}

static void poll_touch(void)
{
    fui_touch_sample_t sample = s_engine.last_touch;
    fui_touch_state_t old_state = s_engine.last_touch.state;
    if (s_engine.config.touch_read == RT_NULL ||
        !s_engine.config.touch_read(&sample, s_engine.config.user_data)) return;

    if (sample.state == FUI_TOUCH_PRESSED && old_state == FUI_TOUCH_RELEASED)
    {
        s_engine.touch_down_x = sample.x;
        s_engine.touch_down_y = sample.y;
        s_engine.touch_down_ms = sample.timestamp_ms;
        s_engine.long_press_sent = false;
        queue_event(FUI_EVENT_TOUCH_DOWN, sample.x, sample.y, 0, 0,
                    sample.timestamp_ms);
    }
    else if (sample.state == FUI_TOUCH_PRESSED && old_state == FUI_TOUCH_PRESSED &&
             (sample.x != s_engine.last_touch.x || sample.y != s_engine.last_touch.y))
    {
        queue_event(FUI_EVENT_TOUCH_MOVE, sample.x, sample.y,
                    sample.x - s_engine.last_touch.x,
                    sample.y - s_engine.last_touch.y, sample.timestamp_ms);
    }
    else if (sample.state == FUI_TOUCH_RELEASED && old_state == FUI_TOUCH_PRESSED)
    {
        int32_t dx = sample.x - s_engine.touch_down_x;
        int32_t dy = sample.y - s_engine.touch_down_y;
        queue_event(FUI_EVENT_TOUCH_UP, sample.x, sample.y, (int16_t)dx,
                    (int16_t)dy, sample.timestamp_ms);
        if (!s_engine.long_press_sent &&
            (dx < 0 ? -dx : dx) <= s_engine.config.tap_distance_px &&
            (dy < 0 ? -dy : dy) <= s_engine.config.tap_distance_px)
            queue_event(FUI_EVENT_TAP, sample.x, sample.y, (int16_t)dx,
                        (int16_t)dy, sample.timestamp_ms);
    }
    else if (sample.state == FUI_TOUCH_PRESSED &&
             old_state == FUI_TOUCH_PRESSED &&
             !s_engine.long_press_sent)
    {
        int32_t dx = sample.x - s_engine.touch_down_x;
        int32_t dy = sample.y - s_engine.touch_down_y;
        if ((dx < 0 ? -dx : dx) <= s_engine.config.tap_distance_px &&
            (dy < 0 ? -dy : dy) <= s_engine.config.tap_distance_px &&
            (sample.timestamp_ms - s_engine.touch_down_ms) >=
                s_engine.config.long_press_ms)
        {
            queue_event(FUI_EVENT_LONG_PRESS, sample.x, sample.y,
                        (int16_t)dx, (int16_t)dy, sample.timestamp_ms);
            s_engine.long_press_sent = true;
        }
    }
    s_engine.last_touch = sample;
}

static void input_thread(void *parameter)
{
    (void)parameter;
    while (s_engine.running)
    {
        poll_touch();
        rt_thread_mdelay(s_engine.config.idle_poll_ms);
    }
}

static void drain_events(void)
{
    fui_event_t event;
    rt_ssize_t received;
    while (s_engine.event_queue != RT_NULL)
    {
        /* RT-Thread 5 returns the received byte count on success, not RT_EOK.
         * Comparing with zero leaves every touch and automation event queued
         * forever even though rt_mq_send() succeeded. */
        received = rt_mq_recv(s_engine.event_queue, &event, sizeof(event), 0);
        if (received < 0) break;
        if ((size_t)received != sizeof(event))
        {
            s_engine.stats.events_queue_failed++;
            continue;
        }
        s_engine.stats.events_dispatched++;
        {
            uint32_t now = rt_tick_get_millisecond();
            uint32_t latency = now - event.timestamp_ms;
            s_engine.stats.event_latency_ms_last = latency;
            if (latency > s_engine.stats.event_latency_ms_max)
                s_engine.stats.event_latency_ms_max = latency;
        }
        if (s_engine.config.event != RT_NULL &&
            s_engine.config.event(&event, s_engine.config.user_data))
            s_engine.dirty = true;
    }
}

int fui_engine_render_now(void)
{
    fui_renderer_frame_stats_t renderer_stats;
    uint32_t frame_start;
    uint32_t phase_start;
    uint32_t submit_before;
    uint32_t bytes_before;
    uint32_t completed_before;
    int result;
    void *target;

    if (s_engine.config.collect == RT_NULL || s_engine.config.present == RT_NULL)
        return -RT_EINVAL;
    target = s_engine.config.framebuffers[s_engine.target_index];
    if (target == RT_NULL) return -RT_EINVAL;

    frame_start = fui_clock_cycles();
    phase_start = frame_start;
    fui_display_list_reset(&s_engine.list);
    fui_painter_init(&s_engine.painter, &s_engine.list,
                     s_engine.config.width, s_engine.config.height);
    s_engine.config.collect(&s_engine.painter, s_engine.config.user_data);
    s_engine.stats.collect_us_last = fui_cycles_to_us(fui_clock_cycles() - phase_start);
    s_engine.stats.collect_us_total += s_engine.stats.collect_us_last;
    s_engine.stats.frames_collected++;
    s_engine.stats.commands_last = s_engine.list.count;
    if (s_engine.list.count > s_engine.stats.commands_peak)
        s_engine.stats.commands_peak = s_engine.list.count;
    if (s_engine.list.overflow != 0U)
    {
        s_engine.stats.list_overflows += s_engine.list.overflow;
        s_engine.stats.frames_failed++;
        s_engine.stats.render_error_last = -RT_EFULL;
        s_engine.dirty = false;
        s_engine.benchmark_remaining = 0U;
        return -RT_EFULL;
    }

    submit_before = g_fui_gpu_submit_count;
    bytes_before = g_fui_gpu_submit_bytes;
    completed_before = g_fui_gpu_completed_jobs;
    phase_start = fui_clock_cycles();
    result = fui_renderer_render(&s_engine.list, target,
                                 s_engine.config.width,
                                 s_engine.config.height,
                                 s_engine.config.stride_pixels);
    s_engine.stats.render_us_last = fui_cycles_to_us(fui_clock_cycles() - phase_start);
    s_engine.stats.render_us_total += s_engine.stats.render_us_last;
    fui_renderer_get_frame_stats(&renderer_stats);
    s_engine.stats.encode_us_last = fui_cycles_to_us(renderer_stats.encode_cycles);
    s_engine.stats.encode_us_total += s_engine.stats.encode_us_last;
    s_engine.stats.clear_encode_us_last = fui_cycles_to_us(renderer_stats.clear_encode_cycles);
    s_engine.stats.clear_encode_us_total += s_engine.stats.clear_encode_us_last;
    s_engine.stats.path_encode_us_last = fui_cycles_to_us(renderer_stats.path_encode_cycles);
    s_engine.stats.path_encode_us_total += s_engine.stats.path_encode_us_last;
    s_engine.stats.blit_encode_us_last = fui_cycles_to_us(renderer_stats.blit_encode_cycles);
    s_engine.stats.blit_encode_us_total += s_engine.stats.blit_encode_us_last;
    s_engine.stats.clear_calls_last = renderer_stats.clear_calls;
    s_engine.stats.path_calls_last = renderer_stats.path_calls;
    s_engine.stats.blit_calls_last = renderer_stats.blit_calls;
    s_engine.stats.path_primitives_last = renderer_stats.path_primitives;
    if (renderer_stats.path_batch_peak > s_engine.stats.path_batch_peak)
        s_engine.stats.path_batch_peak = renderer_stats.path_batch_peak;
    if (result != 0)
    {
        s_engine.stats.frames_failed++;
        s_engine.stats.render_error_last = result;
        s_engine.dirty = false;
        s_engine.benchmark_remaining = 0U;
        return result;
    }
    if ((g_fui_gpu_submit_count - submit_before) != 1U ||
        (g_fui_gpu_completed_jobs - completed_before) != 1U)
    {
        s_engine.stats.contract_violations++;
        rt_kprintf("[FeatherUI] frame contract violated: submit=%lu complete=%lu cmds=%u\n",
                   (unsigned long)(g_fui_gpu_submit_count - submit_before),
                   (unsigned long)(g_fui_gpu_completed_jobs - completed_before),
                   s_engine.list.count);
    }

    phase_start = fui_clock_cycles();
    result = s_engine.config.present(target, s_engine.config.user_data);
    s_engine.stats.present_us_last = fui_cycles_to_us(fui_clock_cycles() - phase_start);
    s_engine.stats.present_us_total += s_engine.stats.present_us_last;
    if (result != 0)
    {
        s_engine.stats.frames_failed++;
        s_engine.stats.render_error_last = result;
        s_engine.dirty = false;
        s_engine.benchmark_remaining = 0U;
        return result;
    }

    s_engine.target_index ^= 1U;
    s_engine.stats.frames_presented++;
    s_engine.stats.gpu_submit_count = g_fui_gpu_submit_count;
    s_engine.stats.gpu_submit_bytes = g_fui_gpu_submit_bytes;
    s_engine.stats.gpu_submit_bytes_last = g_fui_gpu_submit_bytes - bytes_before;
    s_engine.stats.gpu_finish_count = s_engine.stats.frames_presented;
    s_engine.stats.gpu_completed_jobs = g_fui_gpu_completed_jobs;
    s_engine.stats.gpu_busy_us_total = fui_cycles_to_us(g_fui_gpu_busy_cycles);
    s_engine.stats.gpu_busy_us_last = fui_cycles_to_us(g_fui_gpu_busy_last_cycles);
    s_engine.stats.frame_us_last = fui_cycles_to_us(fui_clock_cycles() - frame_start);
    s_engine.stats.frame_us_total += s_engine.stats.frame_us_last;
    if (s_engine.stats.frame_us_last > s_engine.stats.frame_us_max)
        s_engine.stats.frame_us_max = s_engine.stats.frame_us_last;
    s_engine.dirty = false;
    s_engine.stats.render_error_last = 0;
    return 0;
}

static void engine_thread(void *parameter)
{
    uint32_t last_frame_event = 0U;
    (void)parameter;
    while (s_engine.running)
    {
        uint32_t now = rt_tick_get_millisecond();
        drain_events();
        if ((now - last_frame_event) >= s_engine.config.frame_interval_ms)
        {
            if (fui_animation_update(now)) s_engine.dirty = true;
            dispatch_event(FUI_EVENT_FRAME, 0, 0, 0, 0, now);
            last_frame_event = now;
        }
        if (s_engine.dirty && !s_engine.paused)
        {
            (void)fui_engine_render_now();
            if (s_engine.benchmark_remaining != 0U)
            {
                s_engine.benchmark_remaining--;
                if (s_engine.benchmark_remaining != 0U)
                    s_engine.dirty = true;
            }
        }
        /* A benchmark requests the next frame immediately.  Sleeping for a
         * nominal 5 ms on a 100 Hz RTOS tick otherwise inserts a real 10 ms
         * bubble and measures the scheduler rather than the renderer. */
        if (s_engine.benchmark_remaining == 0U)
            rt_thread_mdelay(s_engine.config.idle_poll_ms);
    }
}

int fui_engine_init(const fui_engine_config_t *config)
{
    if (config == RT_NULL || config->width == 0U || config->height == 0U ||
        config->stride_pixels < config->width ||
        config->framebuffers[0] == RT_NULL || config->framebuffers[1] == RT_NULL ||
        config->collect == RT_NULL || config->present == RT_NULL)
        return -RT_EINVAL;
    memset(&s_engine, 0, sizeof(s_engine));
    fui_animation_cancel_all();
    s_engine.config = *config;
    if (s_engine.config.idle_poll_ms == 0U) s_engine.config.idle_poll_ms = 5U;
    if (s_engine.config.tap_distance_px == 0U)
        s_engine.config.tap_distance_px = FUI_DEFAULT_TAP_DISTANCE_PX;
    if (s_engine.config.long_press_ms == 0U)
        s_engine.config.long_press_ms = FUI_DEFAULT_LONG_PRESS_MS;
    if (s_engine.config.frame_interval_ms == 0U)
        s_engine.config.frame_interval_ms = FUI_DEFAULT_FRAME_MS;
    s_engine.target_index = 1U;
    s_engine.last_touch.state = FUI_TOUCH_RELEASED;
    s_engine.dirty = true;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return fui_renderer_init();
}

int fui_engine_start(void)
{
    if (s_engine.running) return 0;
    s_engine.event_queue = rt_mq_create("fui_evt", sizeof(fui_event_t),
                                        FUI_EVENT_QUEUE_DEPTH,
                                        RT_IPC_FLAG_PRIO);
    if (s_engine.event_queue == RT_NULL) return -RT_ENOMEM;
    s_engine.running = true;
    s_engine.thread = rt_thread_create("feather_ui", engine_thread, RT_NULL,
                                       FUI_THREAD_STACK_SIZE,
                                       FUI_THREAD_PRIORITY, 5U);
    if (s_engine.thread == RT_NULL)
    {
        s_engine.running = false;
        return -RT_ENOMEM;
    }
    s_engine.input_thread = rt_thread_create("fui_input", input_thread, RT_NULL,
                                             FUI_INPUT_STACK_SIZE,
                                             FUI_INPUT_PRIORITY, 5U);
    if (s_engine.input_thread == RT_NULL)
    {
        s_engine.running = false;
        return -RT_ENOMEM;
    }
    rt_thread_startup(s_engine.thread);
    rt_thread_startup(s_engine.input_thread);
    return 0;
}

void fui_engine_invalidate(void)
{
    s_engine.dirty = true;
}

void fui_engine_stop(void)
{
    s_engine.running = false;
}

void fui_engine_get_stats(fui_engine_stats_t *stats)
{
    if (stats != RT_NULL) *stats = s_engine.stats;
}

void fui_engine_benchmark_start(uint32_t frames)
{
    if (frames == 0U) return;
    s_engine.benchmark_remaining = frames;
    s_engine.dirty = true;
}

uint32_t fui_engine_benchmark_remaining(void)
{
    return s_engine.benchmark_remaining;
}

void fui_engine_set_paused(bool paused)
{
    s_engine.paused = paused;
    if (!paused) s_engine.dirty = true;
}

bool fui_engine_is_paused(void)
{
    return s_engine.paused;
}

int fui_engine_post_event(const fui_event_t *event)
{
    int result;
    if (event == RT_NULL || s_engine.event_queue == RT_NULL)
        return -RT_EINVAL;
    result = rt_mq_send(s_engine.event_queue, event, sizeof(*event));
    if (result == RT_EOK) s_engine.stats.events_queued++;
    else s_engine.stats.events_queue_failed++;
    return result;
}
