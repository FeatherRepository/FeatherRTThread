#ifndef FEATHERTALK_AUDIO_H
#define FEATHERTALK_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define FT_AUDIO_MAX_SAMPLE_RATES 4U
#define FT_AUDIO_MAX_SAMPLE_BITS  2U
#define FT_AUDIO_MAX_CHANNELS     2U

typedef struct
{
    bool output_registered;
    bool input_registered;
    bool output_ready;
    bool input_ready;
    bool analog_input_supported;
    uint8_t output_volume;
    uint8_t output_volume_max;
    uint8_t input_gain;
    uint8_t input_gain_max;
    uint32_t output_sample_rate;
    uint32_t input_sample_rate;
    uint8_t output_channels;
    uint8_t input_channels;
    uint8_t output_sample_bits;
    uint8_t input_sample_bits;
    uint8_t output_sample_rate_count;
    uint8_t output_sample_bits_count;
    uint8_t output_channel_count;
    uint32_t output_sample_rates[FT_AUDIO_MAX_SAMPLE_RATES];
    uint8_t output_sample_bits_supported[FT_AUDIO_MAX_SAMPLE_BITS];
    uint8_t output_channels_supported[FT_AUDIO_MAX_CHANNELS];
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
