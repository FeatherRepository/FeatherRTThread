#ifndef FEATHERTALK_UI_PLATFORM_H
#define FEATHERTALK_UI_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT_PLATFORM_DEVICE_SUMMARY_SIZE 256U

typedef struct
{
    uint32_t m55_core_hz;
    uint32_t m33_domain_hz;
    uint32_t npu_hz;
    uint32_t gfx_hz;
    uint32_t flash_smif_hz;
    uint32_t hyperram_smif_hz;
    uint32_t firmware_used_bytes;
    uint32_t firmware_capacity_bytes;
    uint32_t boot_rom_bytes;
    uint32_t onchip_rram_bytes;
    uint32_t onchip_ram_bytes;
    uint32_t dtcm_static_bytes;
    uint32_t dtcm_capacity_bytes;
    uint32_t gfx_used_bytes;
    uint32_t gfx_capacity_bytes;
    uint32_t internal_heap_total;
    uint32_t internal_heap_used;
    uint32_t internal_heap_peak;
    uint32_t external_heap_total;
    uint32_t external_heap_used;
    uint32_t external_heap_peak;
    uint32_t external_flash_bytes;
    uint32_t external_hyperram_bytes;
    uint16_t registered_device_count;
    bool instruction_cache_enabled;
    bool data_cache_enabled;
    char registered_devices[FT_PLATFORM_DEVICE_SUMMARY_SIZE];
} ft_platform_system_info_t;

bool ft_platform_brightness_available(void);
int ft_platform_set_brightness(uint8_t percent);
uint8_t ft_platform_get_brightness(void);
int ft_platform_touch_configure(void);
void ft_platform_touch_print_status(void);
void ft_platform_get_system_info(ft_platform_system_info_t *info);

#endif /* FEATHERTALK_UI_PLATFORM_H */
