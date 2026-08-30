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
    int last_error;
} ft_usb_status_t;

void ft_usb_get_status(ft_usb_status_t *status);
int ft_usb_set_function(ft_usb_function_t function);
void ft_usb_refresh(void);

#endif /* FEATHERTALK_USB_H */
