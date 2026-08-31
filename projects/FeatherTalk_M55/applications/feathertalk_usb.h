#ifndef FEATHERTALK_USB_H
#define FEATHERTALK_USB_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    FT_USB_ROLE_DEVICE = 0,
    FT_USB_ROLE_HOST
} ft_usb_role_t;

typedef enum
{
    FT_USB_FUNCTION_NONE = 0,
    FT_USB_FUNCTION_STORAGE,
    FT_USB_FUNCTION_AUDIO
} ft_usb_function_t;

typedef struct
{
    ft_usb_role_t role;
    ft_usb_function_t function;
    bool host_supported;
    bool storage_supported;
    bool audio_supported;
    bool flash_present;
    bool sd_present;
    bool active;
    bool connected;
    bool configured;
    uint32_t block_size;
    uint32_t block_count;
    uint8_t lun_count;
    uint32_t flash_block_size;
    uint32_t flash_block_count;
    uint32_t sd_block_size;
    uint32_t sd_block_count;
    bool uac_output_streaming;
    bool uac_input_streaming;
    bool uac_format_pending;
    uint32_t uac_output_sample_rate;
    uint8_t uac_output_sample_bits;
    uint8_t uac_output_channels;
    uint32_t uac_input_sample_rate;
    uint8_t uac_input_sample_bits;
    uint8_t uac_input_channels;
    uint32_t uac_host_update_count;
    uint32_t uac_device_update_count;
    uint32_t uac_sync_generation;
    uint64_t uac_host_to_device_bytes;
    uint64_t uac_device_to_host_bytes;
    uint32_t uac_output_overruns;
    uint32_t uac_input_underruns;
    int last_error;
} ft_usb_status_t;

void ft_usb_get_status(ft_usb_status_t *status);
int ft_usb_set_function(ft_usb_function_t function);
int ft_usb_set_uac_output_format(uint32_t sample_rate, uint8_t sample_bits,
                                 uint8_t channels);
bool ft_usb_uac_output_supported(uint32_t sample_rate, uint8_t sample_bits,
                                 uint8_t channels);
bool ft_usb_uac_input_supported(uint32_t sample_rate, uint8_t sample_bits,
                                uint8_t channels);
void ft_usb_refresh(void);

#endif /* FEATHERTALK_USB_H */
