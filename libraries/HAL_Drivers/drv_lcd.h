#ifndef DRV_LCD_H
#define DRV_LCD_H

#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t stride_pixels;
    uint8_t framebuffer_count;
} lcd_gpu_surface_info_t;

typedef struct
{
    uint32_t dc_frame_irqs;
    uint32_t presents;
    uint32_t present_wait_ms_total;
    uint32_t present_wait_ms_max;
    uint32_t present_timeouts;
} lcd_gpu_surface_stats_t;

/* Direct scanout interface for GPU-native or frame-batched LVGL UIs. Both RGB565
 * surfaces live in .cy_gpu_buf and are addressable by GCNano and DC8000Nano.
 * The caller renders into the non-visible surface, finishes its single GPU
 * command list, then presents it at the next display commit. */
rt_err_t lcd_gpu_surface_get_info(lcd_gpu_surface_info_t *info);
void *lcd_gpu_surface_get(uint8_t index);
rt_err_t lcd_gpu_surface_present(void *framebuffer);
/* Queue a framebuffer commit without blocking the LVGL thread.  Before it
 * starts drawing into the other buffer, the next frame must call
 * lcd_gpu_surface_wait_present() to retire this commit. */
rt_err_t lcd_gpu_surface_present_async(void *framebuffer);
rt_err_t lcd_gpu_surface_wait_present(uint32_t timeout_ms);
void lcd_gpu_surface_cpu_read_begin(void *framebuffer);
void lcd_gpu_surface_get_stats(lcd_gpu_surface_stats_t *stats);

rt_err_t lcd_wait_frame_done(uint32_t timeout_ms);

/* Set the user-visible backlight level.  Zero still maps to the board-safe
 * 50 percent PWM duty, matching the product brightness policy. */
rt_err_t lcd_backlight_set_percent(uint8_t percent);
rt_err_t lcd_backlight_get_percent(uint8_t *percent);

#ifdef __cplusplus
}
#endif

#endif /* DRV_LCD_H */
