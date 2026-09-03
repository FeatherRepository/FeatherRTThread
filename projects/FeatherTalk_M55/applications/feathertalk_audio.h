#ifndef FEATHERTALK_AUDIO_H
#define FEATHERTALK_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define FT_AUDIO_MAX_SAMPLE_RATES 4U
#define FT_AUDIO_MAX_SAMPLE_BITS  2U
#define FT_AUDIO_MAX_CHANNELS     2U

typedef enum
{
    FT_AUDIO_OUTPUT_OWNER_NONE = 0,
    FT_AUDIO_OUTPUT_OWNER_LOCAL_PLAYER,
    FT_AUDIO_OUTPUT_OWNER_USB_UAC,
    FT_AUDIO_OUTPUT_OWNER_BT_A2DP    /* M4b: 蓝牙 A2DP Sink 流 */
} ft_audio_output_owner_t;

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
/* sound0 has one physical TDM/codec path.  Streaming clients must claim it
 * before changing its format or opening the replay device. */
int ft_audio_claim_output(ft_audio_output_owner_t owner);
void ft_audio_release_output(ft_audio_output_owner_t owner);
ft_audio_output_owner_t ft_audio_get_output_owner(void);

#endif /* FEATHERTALK_AUDIO_H */
