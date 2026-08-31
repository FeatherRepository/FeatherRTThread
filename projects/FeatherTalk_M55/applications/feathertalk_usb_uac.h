#ifndef FEATHERTALK_USB_UAC_H
#define FEATHERTALK_USB_UAC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool active;
    bool connected;
    bool configured;
    bool output_streaming;
    bool input_streaming;
    bool format_pending;
    uint32_t output_sample_rate;
    uint8_t output_sample_bits;
    uint8_t output_channels;
    uint32_t input_sample_rate;
    uint8_t input_sample_bits;
    uint8_t input_channels;
    uint32_t host_update_count;
    uint32_t device_update_count;
    uint32_t sync_generation;
    uint64_t host_to_device_bytes;
    uint64_t device_to_host_bytes;
    uint32_t output_overruns;
    uint32_t input_underruns;
    int last_error;
} ft_usb_uac_status_t;

int ft_usb_uac_start(void);
int ft_usb_uac_stop(void);
void ft_usb_uac_get_status(ft_usb_uac_status_t *status);
int ft_usb_uac_set_output_format(uint32_t sample_rate, uint8_t sample_bits,
                                 uint8_t channels, bool reconnect_host);
bool ft_usb_uac_output_format_supported(uint32_t sample_rate,
                                        uint8_t sample_bits,
                                        uint8_t channels);
bool ft_usb_uac_input_format_supported(uint32_t sample_rate,
                                       uint8_t sample_bits,
                                       uint8_t channels);

#endif /* FEATHERTALK_USB_UAC_H */
