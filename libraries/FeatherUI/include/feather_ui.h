#ifndef FEATHER_UI_H
#define FEATHER_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FUI_VERSION_MAJOR 0U
#define FUI_VERSION_MINOR 6U
#define FUI_VERSION_PATCH 0U

#ifndef FUI_DISPLAY_LIST_CAPACITY
#define FUI_DISPLAY_LIST_CAPACITY 768U
#endif

#ifndef FUI_LINE_BATCH_CAPACITY
#define FUI_LINE_BATCH_CAPACITY 8U
#endif

#ifndef FUI_LINE_SEGMENT_CAPACITY
#define FUI_LINE_SEGMENT_CAPACITY 512U
#endif

typedef uint32_t fui_color_t;

#define FUI_ARGB(a, r, g, b) \
    ((((uint32_t)(a) & 0xffU) << 24) | (((uint32_t)(r) & 0xffU) << 16) | \
     (((uint32_t)(g) & 0xffU) << 8) | ((uint32_t)(b) & 0xffU))
#define FUI_RGB(r, g, b) FUI_ARGB(0xffU, (r), (g), (b))

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
} fui_rect_t;

typedef struct
{
    const void *pixels;
    uint16_t width;
    uint16_t height;
    uint16_t stride_pixels;
} fui_image_rgb565_t;

typedef enum
{
    FUI_TOUCH_RELEASED = 0,
    FUI_TOUCH_PRESSED = 1
} fui_touch_state_t;

typedef struct
{
    fui_touch_state_t state;
    int16_t x;
    int16_t y;
    uint32_t timestamp_ms;
} fui_touch_sample_t;

typedef enum
{
    FUI_EVENT_TOUCH_DOWN = 0,
    FUI_EVENT_TOUCH_MOVE,
    FUI_EVENT_TOUCH_UP,
    FUI_EVENT_TAP,
    FUI_EVENT_LONG_PRESS,
    FUI_EVENT_FRAME
} fui_event_type_t;

typedef struct
{
    fui_event_type_t type;
    int16_t x;
    int16_t y;
    int16_t delta_x;
    int16_t delta_y;
    uint32_t timestamp_ms;
} fui_event_t;

#ifndef FUI_ANIMATION_CAPACITY
#define FUI_ANIMATION_CAPACITY 48U
#endif

#define FUI_ANIMATION_PROPERTY_ALL   UINT16_MAX
#define FUI_ANIMATION_REPEAT_FOREVER UINT16_MAX

typedef enum
{
    FUI_EASING_LINEAR = 0,
    FUI_EASING_OUT_CUBIC,
    FUI_EASING_IN_OUT_CUBIC,
    FUI_EASING_OUT_BACK,
    FUI_EASING_OUT_SPRING
} fui_easing_t;

typedef void (*fui_animation_apply_cb_t)(void *target, uint16_t property,
                                         int32_t value);
typedef void (*fui_animation_complete_cb_t)(void *target, uint16_t property);

typedef struct
{
    void *target;
    uint16_t property;
    int32_t from;
    int32_t to;
    uint32_t duration_ms;
    uint32_t delay_ms;
    fui_easing_t easing;
    uint16_t repeat_count;
    bool autoreverse;
    fui_animation_apply_cb_t apply;
    fui_animation_complete_cb_t complete;
} fui_animation_spec_t;

typedef struct
{
    uint32_t started;
    uint32_t completed;
    uint32_t cancelled;
    uint16_t active;
    uint16_t peak_active;
} fui_animation_stats_t;

typedef struct fui_painter fui_painter_t;

typedef void (*fui_collect_cb_t)(fui_painter_t *painter, void *user_data);
typedef bool (*fui_event_cb_t)(const fui_event_t *event, void *user_data);
typedef bool (*fui_touch_read_cb_t)(fui_touch_sample_t *sample, void *user_data);
typedef int (*fui_present_cb_t)(void *framebuffer, void *user_data);

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t stride_pixels;
    void *framebuffers[2];
    fui_collect_cb_t collect;
    fui_event_cb_t event;
    fui_touch_read_cb_t touch_read;
    fui_present_cb_t present;
    void *user_data;
    uint16_t idle_poll_ms;
    uint16_t tap_distance_px;
    uint16_t long_press_ms;
    uint16_t frame_interval_ms;
} fui_engine_config_t;

typedef struct
{
    uint32_t frames_collected;
    uint32_t frames_presented;
    uint32_t frames_failed;
    uint32_t list_overflows;
    uint32_t commands_last;
    uint32_t commands_peak;
    uint32_t collect_us_last;
    uint32_t collect_us_total;
    uint32_t render_us_last;
    uint32_t render_us_total;
    uint32_t encode_us_last;
    uint32_t encode_us_total;
    uint32_t clear_encode_us_last;
    uint32_t clear_encode_us_total;
    uint32_t path_encode_us_last;
    uint32_t path_encode_us_total;
    uint32_t blit_encode_us_last;
    uint32_t blit_encode_us_total;
    uint32_t present_us_last;
    uint32_t present_us_total;
    uint32_t frame_us_last;
    uint32_t frame_us_total;
    uint32_t frame_us_max;
    uint32_t gpu_submit_count;
    uint32_t gpu_submit_bytes;
    uint32_t gpu_submit_bytes_last;
    uint32_t gpu_finish_count;
    uint32_t gpu_completed_jobs;
    uint32_t gpu_busy_us_total;
    uint32_t gpu_busy_us_last;
    uint32_t clear_calls_last;
    uint32_t path_calls_last;
    uint32_t blit_calls_last;
    uint32_t path_primitives_last;
    uint32_t path_batch_peak;
    uint32_t events_queued;
    uint32_t events_queue_failed;
    uint32_t events_dispatched;
    uint32_t event_latency_ms_last;
    uint32_t event_latency_ms_max;
    uint32_t contract_violations;
    int32_t render_error_last;
} fui_engine_stats_t;

int fui_engine_init(const fui_engine_config_t *config);
int fui_engine_start(void);
int fui_engine_render_now(void);
void fui_engine_invalidate(void);
void fui_engine_stop(void);
void fui_engine_get_stats(fui_engine_stats_t *stats);
void fui_engine_benchmark_start(uint32_t frames);
uint32_t fui_engine_benchmark_remaining(void);
void fui_engine_set_paused(bool paused);
bool fui_engine_is_paused(void);
int fui_engine_post_event(const fui_event_t *event);

int fui_animation_start(const fui_animation_spec_t *spec,
                        uint32_t start_time_ms);
void fui_animation_cancel(void *target, uint16_t property);
void fui_animation_cancel_all(void);
bool fui_animation_update(uint32_t now_ms);
bool fui_animation_is_active(void);
void fui_animation_get_stats(fui_animation_stats_t *stats);

void fui_painter_set_clip(fui_painter_t *painter, fui_rect_t clip);
void fui_painter_reset_clip(fui_painter_t *painter);
bool fui_painter_clear(fui_painter_t *painter, fui_color_t color);
bool fui_painter_rect(fui_painter_t *painter, fui_rect_t rect,
                      uint16_t radius, fui_color_t color);
bool fui_painter_line(fui_painter_t *painter, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint16_t width,
                      fui_color_t color);
bool fui_painter_line_batch(fui_painter_t *painter, int16_t x1, int16_t y1,
                            int16_t x2, int16_t y2, uint16_t width,
                            fui_color_t color);
bool fui_painter_text(fui_painter_t *painter, int16_t x, int16_t y,
                      uint8_t scale, fui_color_t color, const char *text);
bool fui_painter_image_rgb565(fui_painter_t *painter, int16_t x, int16_t y,
                              const fui_image_rgb565_t *image);

bool fui_rect_contains(const fui_rect_t *rect, int16_t x, int16_t y);

#ifdef __cplusplus
}
#endif

#endif /* FEATHER_UI_H */
