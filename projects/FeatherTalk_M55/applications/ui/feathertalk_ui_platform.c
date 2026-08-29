#include <rtdevice.h>
#include <rtthread.h>
#include "feathertalk_ui_platform.h"

#define FT_LCD_BL_PWM_DEVICE    "pwm18"
#define FT_LCD_BL_PWM_CHANNEL   0U
#define FT_LCD_BL_PWM_PERIOD_NS 200000U

static struct rt_device_pwm *s_backlight_pwm;
static uint8_t s_brightness = 100U;

bool ft_platform_brightness_available(void)
{
#ifdef RT_USING_PWM
    if (s_backlight_pwm == RT_NULL)
        s_backlight_pwm = (struct rt_device_pwm *)rt_device_find(FT_LCD_BL_PWM_DEVICE);
    return s_backlight_pwm != RT_NULL;
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
    pulse = (FT_LCD_BL_PWM_PERIOD_NS * percent) / 100U;
    result = rt_pwm_set(s_backlight_pwm, FT_LCD_BL_PWM_CHANNEL,
                        FT_LCD_BL_PWM_PERIOD_NS, pulse);
    if (result == RT_EOK) s_brightness = percent;
    return result;
#else
    (void)percent;
    return -RT_ENOSYS;
#endif
}

uint8_t ft_platform_get_brightness(void) { return s_brightness; }
