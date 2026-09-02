#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <board.h>
#if defined(BSP_USING_LVGL)
#include "lvgl.h"
#if !defined(FEATHERTALK_USING_UI_SHELL)
#include "lv_demos.h"
#endif
#endif
#include <feathertalk/version.h>
#include "ipc/feathertalk_ipc.h"
#ifdef FEATHERTALK_USING_GPU_UI
#include "gpu_ui/feathertalk_gpu_ui.h"
#endif
#ifdef FEATHERTALK_USING_UI_SHELL
#include "ui/feathertalk_ui.h"
#endif

#if defined(BSP_LVGL_DEMO_VIRTUAL3D_EMOJI)
#error "Virtual3D resources were removed from FeatherTalk_M55; select the Music, Benchmark, or Stress demo."
#endif

#define LED_PIN_G GET_PIN(16, 6)
#define LCD_BL_GPIO_NUM GET_PIN(15, 7)

#ifndef BSP_LCD_STARTUP_STABILIZE_MS
#define BSP_LCD_STARTUP_STABILIZE_MS 1500U
#endif

#ifndef BSP_LCD_FIRST_FRAME_DELAY_MS
#define BSP_LCD_FIRST_FRAME_DELAY_MS 300U
#endif

#ifndef BSP_LCD_ROTATION_DEGREES
#define BSP_LCD_ROTATION_DEGREES 0
#endif

#if defined(FEATHERTALK_USING_GPU_UI)
#define BSP_UI_NAME "feather-gpu-native"
#elif defined(FEATHERTALK_USING_UI_SHELL)
#define BSP_LVGL_DEMO_NAME "feathertalk-shell"
#elif defined(BSP_LVGL_DEMO_BENCHMARK)
#define BSP_LVGL_DEMO_NAME "benchmark"
#elif defined(BSP_LVGL_DEMO_STRESS)
#define BSP_LVGL_DEMO_NAME "stress"
#else
#define BSP_LVGL_DEMO_NAME "music"
#endif

#if defined(BSP_USING_LVGL)
extern int lvgl_thread_init(void);
#endif

static void m55_cpu_cache_enable(void)
{
#if defined(RT_USING_CACHE)
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    if (!rt_hw_cpu_icache_status())
    {
        rt_hw_cpu_icache_enable();
    }
#endif

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (!rt_hw_cpu_dcache_status())
    {
        rt_hw_cpu_dcache_enable();
    }
#endif

    rt_kprintf("M55 cache enabled: I=%d D=%d\n",
               rt_hw_cpu_icache_status(),
               rt_hw_cpu_dcache_status());
#endif
}

static void m55_lcd_backlight_enable(void)
{
    rt_pin_mode(LCD_BL_GPIO_NUM, PIN_MODE_OUTPUT);
    rt_pin_write(LCD_BL_GPIO_NUM, PIN_HIGH);

    /*
     * en_gpio() holds P20_6 as a GPIO while the display power rails settle.
     * Hand the pin back to TCPWM0 line 265 after the first frame; otherwise
     * pwm18 changes only the counter registers and cannot reach the panel.
     */
    Cy_GPIO_Pin_Init(CYBSP_DISP_BACKLIGHT_PWM_PORT,
                     CYBSP_DISP_BACKLIGHT_PWM_PIN,
                     &CYBSP_DISP_BACKLIGHT_PWM_config);
}

#if defined(BSP_USING_LVGL)
void lv_user_gui_init(void)
{
#if defined(FEATHERTALK_USING_UI_SHELL)
    (void)feathertalk_ui_init();
#elif defined(BSP_LVGL_DEMO_BENCHMARK)
    lv_demo_benchmark();
#elif defined(BSP_LVGL_DEMO_STRESS)
    lv_demo_stress();
#else
    lv_demo_music();
#endif
}
#endif

int main(void)
{
    uint32_t last_led_ms = rt_tick_get_millisecond();
    rt_bool_t led_on = RT_FALSE;

    /* rt_hw_context_switch_to() forces PendSV+SysTick to the lowest priority
       at scheduler start; raise the OS tick above every peripheral IRQ so a
       storming device interrupt can never freeze the kernel tick. */
    NVIC_SetPriority(SysTick_IRQn, 0);

    rt_kprintf("FeatherTalk M55 %s\n", FEATHERTALK_M55_FIRMWARE_VERSION);
    rt_kprintf("IPC ABI %u\n", FEATHERTALK_IPC_ABI_VERSION);
#if defined(FEATHERTALK_USING_GPU_UI)
    rt_kprintf("UI %s start, lcd rotation=%d\n", BSP_UI_NAME, BSP_LCD_ROTATION_DEGREES);
#else
    rt_kprintf("LVGL %s demo start, lcd rotation=%d\n", BSP_LVGL_DEMO_NAME, BSP_LCD_ROTATION_DEGREES);
#endif
    rt_kprintf("Console: M55 diagnostics on uart2; product MSH control stays on M33 uart5\n");

    rt_pin_mode(LED_PIN_G, PIN_MODE_OUTPUT);
    if (feathertalk_ipc_start() != RT_EOK)
    {
        rt_kprintf("FeatherTalk M55 IPC responder failed to start\n");
    }
    m55_cpu_cache_enable();
    rt_thread_mdelay(BSP_LCD_STARTUP_STABILIZE_MS);
#if defined(FEATHERTALK_USING_GPU_UI)
    if (feathertalk_gpu_ui_init() != RT_EOK)
        rt_kprintf("FeatherUI startup failed\n");
#else
    lvgl_thread_init();
#endif
    rt_thread_mdelay(BSP_LCD_FIRST_FRAME_DELAY_MS);
    m55_lcd_backlight_enable();
    feathertalk_ipc_set_lvgl_ready();

    while (1)
    {
        uint32_t now = rt_tick_get_millisecond();

        if ((now - last_led_ms) >= 500U)
        {
            led_on = !led_on;
            rt_pin_write(LED_PIN_G, led_on ? PIN_HIGH : PIN_LOW);
            last_led_ms = now;
        }

        rt_thread_mdelay(50);
    }

    return 0;
}
