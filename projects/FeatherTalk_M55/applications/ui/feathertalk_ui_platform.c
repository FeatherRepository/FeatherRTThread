#include <rtdevice.h>
#include <rtthread.h>
#include <rthw.h>
#include <board.h>
#include <lvgl.h>
#include <drv_touch.h>
#include <cycfg_qspi_memslot.h>
#include <string.h>
#include "feathertalk_ui_platform.h"

#define FT_LCD_BL_PWM_DEVICE    "pwm18"
#define FT_LCD_BL_PWM_CHANNEL   0U
#define FT_LCD_BL_PWM_PERIOD_NS 200000U
#define FT_LCD_BL_DEFAULT_PERCENT 80U
#define FT_LCD_BL_MIN_DUTY_PERCENT 50U
#define FT_TOUCH_LONG_PRESS_MS      500U
#define FT_TOUCH_SCROLL_LIMIT_PX     18U
#define FT_M55_XIP_CAPACITY_BYTES    0x00800000UL
#define FT_M55_DTCM_CAPACITY_BYTES   0x00040000UL
#define FT_GFX_SRAM_CAPACITY_BYTES   0x00300000UL

extern unsigned char __m55_image_start__;
extern unsigned char __m55_image_end__;
extern unsigned char __bss_end__;
extern unsigned char __gfx_mem_used_start__;
extern unsigned char __gfx_mem_used_end__;
extern struct rt_memheap *drv_hyperam_get_memheap(void);

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

static void collect_registered_devices(ft_platform_system_info_t *info)
{
    struct rt_object_information *devices;
    rt_list_t *node;
    size_t used = 0U;

    devices = rt_object_get_information(RT_Object_Class_Device);
    if (devices == RT_NULL) return;

    rt_list_for_each(node, &devices->object_list)
    {
        struct rt_object *object = rt_list_entry(node, struct rt_object, list);
        const char *name = object->name;
        int written;

        if (name == RT_NULL || name[0] == '\0') continue;
        info->registered_device_count++;
        if (used >= sizeof(info->registered_devices) - 1U) continue;
        written = rt_snprintf(info->registered_devices + used,
                              sizeof(info->registered_devices) - used,
                              "%s%s", used == 0U ? "" : ", ", name);
        if (written > 0)
        {
            size_t appended = (size_t)written;
            size_t available = sizeof(info->registered_devices) - used;
            used += appended < available ? appended : available - 1U;
        }
    }
}

void ft_platform_get_system_info(ft_platform_system_info_t *info)
{
    struct rt_memheap *hyperram_heap;
    rt_size_t total = 0U;
    rt_size_t used = 0U;
    rt_size_t peak = 0U;
    uint32_t npu_hf;

    if (info == RT_NULL) return;
    rt_memset(info, 0, sizeof(*info));

    info->m55_core_hz = SystemCoreClock;
    info->m33_domain_hz = Cy_SysClk_ClkHfGetFrequency(0U);
    npu_hf = Cy_Sysclk_PeriPclkGetClkHfNum(PCLK_MXU55_CLK_HF);
    info->npu_hz = Cy_SysClk_ClkHfGetFrequency(npu_hf);
    info->gfx_hz = Cy_SysClk_ClkHfGetFrequency(CY_MMIO_GFXSS_GPU_CLK_HF_NR);
    info->flash_smif_hz = CYBSP_SMIF_CORE_0_XSPI_FLASH_config.inputFrequencyMHz * 1000000UL;
    info->hyperram_smif_hz = CYBSP_SMIF_CORE_1_PSRAM_config.inputFrequencyMHz * 1000000UL;
    info->instruction_cache_enabled = rt_hw_cpu_icache_status() != 0;
    info->data_cache_enabled = rt_hw_cpu_dcache_status() != 0;

    info->firmware_used_bytes =
        (uint32_t)((uintptr_t)&__m55_image_end__ - (uintptr_t)&__m55_image_start__);
    info->firmware_capacity_bytes = FT_M55_XIP_CAPACITY_BYTES;
    info->boot_rom_bytes = CY_ROM_M0_SIZE;
    info->onchip_rram_bytes = CY_RRAM_SIZE;
    info->onchip_ram_bytes = CY_CM55_ITCM_INTERNAL_SIZE +
                             CY_CM55_DTCM_INTERNAL_SIZE +
                             CY_SRAM_SIZE + CY_SOCMEM_RAM_SIZE;
    info->dtcm_static_bytes =
        (uint32_t)((uintptr_t)&__bss_end__ - (uintptr_t)0x20000000UL);
    info->dtcm_capacity_bytes = FT_M55_DTCM_CAPACITY_BYTES;
    info->gfx_used_bytes =
        (uint32_t)((uintptr_t)&__gfx_mem_used_end__ -
                   (uintptr_t)&__gfx_mem_used_start__);
    info->gfx_capacity_bytes = FT_GFX_SRAM_CAPACITY_BYTES;

    rt_memory_info(&total, &used, &peak);
    info->internal_heap_total = (uint32_t)total;
    info->internal_heap_used = (uint32_t)used;
    info->internal_heap_peak = (uint32_t)peak;

    hyperram_heap = drv_hyperam_get_memheap();
    if (hyperram_heap != RT_NULL && hyperram_heap->pool_size != 0U)
    {
        rt_memheap_info(hyperram_heap, &total, &used, &peak);
        info->external_heap_total = (uint32_t)total;
        info->external_heap_used = (uint32_t)used;
        info->external_heap_peak = (uint32_t)peak;
    }

    info->external_flash_bytes = deviceCfg_S25FS128S_SMIF0_SlaveSlot_1.memSize;
    info->external_hyperram_bytes = deviceCfg_S70KS1283_SMIF1_SlaveSlot_2.memSize;
    collect_registered_devices(info);
}
