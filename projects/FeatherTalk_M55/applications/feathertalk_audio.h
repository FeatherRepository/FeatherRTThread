#ifndef FEATHERTALK_AUDIO_H
#define FEATHERTALK_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool output_registered;
    bool input_registered;
    bool output_ready;
    bool input_ready;
    bool analog_input_supported;
    uint8_t output_volume;
    uint8_t input_gain;
    uint32_t output_sample_rate;
    uint32_t input_sample_rate;
    uint8_t output_channels;
    uint8_t input_channels;
    uint8_t output_sample_bits;
    uint8_t input_sample_bits;
    int last_error;
} ft_audio_status_t;

int ft_audio_get_status(ft_audio_status_t *status);
int ft_audio_set_output_volume(uint8_t volume);
int ft_audio_set_input_gain(uint8_t gain);
bool ft_audio_output_format_supported(uint32_t sample_rate,
                                      uint8_t sample_bits,
                                      uint8_t channels);
int ft_audio_set_output_format(uint32_t sample_rate, uint8_t sample_bits,
                               uint8_t channels);

#endif /* FEATHERTALK_AUDIO_H */
