#include <rtdevice.h>
#include <rtthread.h>
#include <board.h>
#include <lvgl.h>
#include <drv_touch.h>
#include "feathertalk_ui_platform.h"

#define FT_LCD_BL_PWM_DEVICE    "pwm18"
#define FT_LCD_BL_PWM_CHANNEL   0U
#define FT_LCD_BL_PWM_PERIOD_NS 200000U
#define FT_LCD_BL_DEFAULT_PERCENT 80U
#define FT_LCD_BL_MIN_DUTY_PERCENT 50U
#define FT_TOUCH_LONG_PRESS_MS      500U
#define FT_TOUCH_SCROLL_LIMIT_PX     18U

static struct rt_device_pwm *s_backlight_pwm;
static uint8_t s_brightness =
    (FT_LCD_BL_DEFAULT_PERCENT - FT_LCD_BL_MIN_DUTY_PERCENT) * 100U /
    (100U - FT_LCD_BL_MIN_DUTY_PERCENT);

#ifdef RT_USING_PWM
static bool backlight_read_percent(uint8_t *percent)
{
    struct rt_pwm_configuration configuration = {0};
    uint64_t scaled;

    if (s_backlight_pwm == RT_NULL || percent == NULL) return false;
    configuration.channel = FT_LCD_BL_PWM_CHANNEL;
    if (rt_device_control(&s_backlight_pwm->parent, PWM_CMD_GET, &configuration) != RT_EOK ||
        configuration.period == 0U)
        return false;

    if ((uint64_t)configuration.pulse * 100U <=
        (uint64_t)configuration.period * FT_LCD_BL_MIN_DUTY_PERCENT)
    {
        *percent = 0U;
        return true;
    }

    scaled = (((uint64_t)configuration.pulse * 100U -
               (uint64_t)configuration.period * FT_LCD_BL_MIN_DUTY_PERCENT) * 100U +
              ((uint64_t)configuration.period *
               (100U - FT_LCD_BL_MIN_DUTY_PERCENT) / 2U)) /
             ((uint64_t)configuration.period *
              (100U - FT_LCD_BL_MIN_DUTY_PERCENT));
    if (scaled > 100U) scaled = 100U;
    *percent = (uint8_t)scaled;
    return true;
}
#endif

bool ft_platform_brightness_available(void)
{
#ifdef RT_USING_PWM
    if (s_backlight_pwm == RT_NULL)
        s_backlight_pwm = (struct rt_device_pwm *)rt_device_find(FT_LCD_BL_PWM_DEVICE);
    return s_backlight_pwm != RT_NULL &&
           Cy_GPIO_GetHSIOM(CYBSP_DISP_BACKLIGHT_PWM_PORT,
                            CYBSP_DISP_BACKLIGHT_PWM_PIN) ==
               CYBSP_DISP_BACKLIGHT_PWM_HSIOM;
#else
    return false;
#endif
}

int ft_platform_set_brightness(uint8_t percent)
{
#ifdef RT_USING_PWM
    rt_uint32_t pulse;
    rt_err_t result;
    if (percent > 100U) percent = 100U;
    if (!ft_platform_brightness_available()) return -RT_ENOSYS;
    result = rt_pwm_enable(s_backlight_pwm, FT_LCD_BL_PWM_CHANNEL);
    if (result != RT_EOK) return result;
    pulse = (FT_LCD_BL_PWM_PERIOD_NS *
             (FT_LCD_BL_MIN_DUTY_PERCENT * 100U +
              percent * (100U - FT_LCD_BL_MIN_DUTY_PERCENT))) /
            10000U;
    result = rt_pwm_set(s_backlight_pwm, FT_LCD_BL_PWM_CHANNEL,
                        FT_LCD_BL_PWM_PERIOD_NS, pulse);
    if (result == RT_EOK)
    {
        if (!backlight_read_percent(&s_brightness)) s_brightness = percent;
    }
    return result;
#else
    (void)percent;
    return -RT_ENOSYS;
#endif
}

uint8_t ft_platform_get_brightness(void)
{
#ifdef RT_USING_PWM
    uint8_t actual;
    if (ft_platform_brightness_available() && backlight_read_percent(&actual))
        s_brightness = actual;
#endif
    return s_brightness;
}

int ft_platform_touch_configure(void)
{
    lv_indev_t *input = lv_indev_get_next(RT_NULL);
    while (input != RT_NULL)
    {
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER)
        {
            lv_indev_set_long_press_time(input, FT_TOUCH_LONG_PRESS_MS);
            lv_indev_set_scroll_limit(input, FT_TOUCH_SCROLL_LIMIT_PX);
            return RT_EOK;
        }
        input = lv_indev_get_next(input);
    }
    return -RT_ENOSYS;
}

void ft_platform_touch_print_status(void)
{
    st7102_touch_diagnostics_t diagnostics = {0};
    ST7102_get_diagnostics(&diagnostics);
    rt_kprintf("FeatherTalk UI touch: long-press=%ums scroll-limit=%upx "
               "frames=%lu held=%lu press=%lu release=%lu\n",
               FT_TOUCH_LONG_PRESS_MS,
               FT_TOUCH_SCROLL_LIMIT_PX,
               (unsigned long)diagnostics.coordinate_frames,
               (unsigned long)diagnostics.held_reports,
               (unsigned long)diagnostics.press_reports,
               (unsigned long)diagnostics.release_reports);
}
