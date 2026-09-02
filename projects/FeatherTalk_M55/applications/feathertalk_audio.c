#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#include "feathertalk_audio.h"

#ifdef RT_USING_AUDIO
#include <drivers/audio.h>

#define FT_AUDIO_OUTPUT_DEVICE "sound0"
#define FT_AUDIO_INPUT_DEVICE  "mic0"
#define FT_AUDIO_INPUT_GAIN_MAX 75U

/* These are driver capabilities, not UI choices.  Keeping the table beside
 * sound0's adapter gives every consumer (local settings, UAC and MSH) the
 * same source of truth when the codec/TDM implementation changes. */
static const uint32_t s_output_sample_rates[] = {16000U, 24000U, 48000U, 96000U};
static const uint8_t s_output_sample_bits[] = {16U, 24U};
static const uint8_t s_output_channels[] = {1U, 2U};
static volatile ft_audio_output_owner_t s_output_owner =
    FT_AUDIO_OUTPUT_OWNER_NONE;

int ft_audio_claim_output(ft_audio_output_owner_t owner)
{
    rt_base_t level;
    int result = RT_EOK;
    if (owner == FT_AUDIO_OUTPUT_OWNER_NONE) return -RT_EINVAL;
    level = rt_hw_interrupt_disable();
    if (s_output_owner != FT_AUDIO_OUTPUT_OWNER_NONE &&
        s_output_owner != owner)
        result = -RT_EBUSY;
    else
        s_output_owner = owner;
    rt_hw_interrupt_enable(level);
    return result;
}

void ft_audio_release_output(ft_audio_output_owner_t owner)
{
    rt_base_t level;
    level = rt_hw_interrupt_disable();
    if (s_output_owner == owner) s_output_owner = FT_AUDIO_OUTPUT_OWNER_NONE;
    rt_hw_interrupt_enable(level);
}

ft_audio_output_owner_t ft_audio_get_output_owner(void)
{
    return s_output_owner;
}

static void audio_fill_output_capabilities(ft_audio_status_t *status)
{
    status->output_volume_max = AUDIO_VOLUME_MAX;
    status->input_gain_max = FT_AUDIO_INPUT_GAIN_MAX;
    status->output_sample_rate_count =
        (uint8_t)(sizeof(s_output_sample_rates) / sizeof(s_output_sample_rates[0]));
    status->output_sample_bits_count =
        (uint8_t)(sizeof(s_output_sample_bits) / sizeof(s_output_sample_bits[0]));
    status->output_channel_count =
        (uint8_t)(sizeof(s_output_channels) / sizeof(s_output_channels[0]));
    memcpy(status->output_sample_rates, s_output_sample_rates,
           sizeof(s_output_sample_rates));
    memcpy(status->output_sample_bits_supported, s_output_sample_bits,
           sizeof(s_output_sample_bits));
    memcpy(status->output_channels_supported, s_output_channels,
           sizeof(s_output_channels));
}

static int audio_get_caps(rt_device_t device, int main_type,
                          struct rt_audio_caps *caps)
{
    memset(caps, 0, sizeof(*caps));
    caps->main_type = main_type;
    caps->sub_type = AUDIO_DSP_PARAM;
    return rt_device_control(device, AUDIO_CTL_GETCAPS, caps);
}

static int audio_get_level(rt_device_t device, uint8_t *value)
{
    struct rt_audio_caps caps;
    int result;

    memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = AUDIO_MIXER_VOLUME;
    result = rt_device_control(device, AUDIO_CTL_GETCAPS, &caps);
    if (result == RT_EOK)
        *value = (uint8_t)caps.udata.value;
    return result;
}

static int audio_set_level(const char *device_name, uint8_t value)
{
    struct rt_audio_caps caps;
    rt_device_t device = rt_device_find(device_name);
    int result;

    if (device == RT_NULL) return -RT_ENOSYS;
    result = rt_device_init(device);
    if (result != RT_EOK) return result;
    memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = AUDIO_MIXER_VOLUME;
    caps.udata.value = value;
    return rt_device_control(device, AUDIO_CTL_CONFIGURE, &caps);
}

int ft_audio_get_status(ft_audio_status_t *status)
{
    struct rt_audio_caps caps;
    rt_device_t output;
    rt_device_t input;
    int result = RT_EOK;
    int current;

    if (status == RT_NULL) return -RT_EINVAL;
    memset(status, 0, sizeof(*status));
    audio_fill_output_capabilities(status);
    status->analog_input_supported = false;

    output = rt_device_find(FT_AUDIO_OUTPUT_DEVICE);
    input = rt_device_find(FT_AUDIO_INPUT_DEVICE);
    status->output_registered = output != RT_NULL;
    status->input_registered = input != RT_NULL;
    status->output_ready = output != RT_NULL &&
        (output->flag & RT_DEVICE_FLAG_ACTIVATED) != 0U;
    status->input_ready = input != RT_NULL &&
        (input->flag & RT_DEVICE_FLAG_ACTIVATED) != 0U;

    if (status->output_ready)
    {
        current = audio_get_caps(output, AUDIO_TYPE_OUTPUT, &caps);
        if (current == RT_EOK)
        {
            status->output_sample_rate = caps.udata.config.samplerate;
            status->output_channels = (uint8_t)caps.udata.config.channels;
            status->output_sample_bits = (uint8_t)caps.udata.config.samplebits;
        }
        else
        {
            result = current;
        }
        current = audio_get_level(output, &status->output_volume);
        if (current != RT_EOK) result = current;
    }
    else
    {
        result = -RT_ENOSYS;
    }

    if (status->input_ready)
    {
        current = audio_get_caps(input, AUDIO_TYPE_INPUT, &caps);
        if (current == RT_EOK)
        {
            status->input_sample_rate = caps.udata.config.samplerate;
            status->input_channels = (uint8_t)caps.udata.config.channels;
            status->input_sample_bits = (uint8_t)caps.udata.config.samplebits;
        }
        else
        {
            result = current;
        }
        current = audio_get_level(input, &status->input_gain);
        if (current != RT_EOK) result = current;
    }
    else
    {
        result = -RT_ENOSYS;
    }

    status->last_error = result;
    return (status->output_ready || status->input_ready) ? RT_EOK : result;
}

int ft_audio_set_output_volume(uint8_t volume)
{
    if (volume > AUDIO_VOLUME_MAX) volume = AUDIO_VOLUME_MAX;
    return audio_set_level(FT_AUDIO_OUTPUT_DEVICE, volume);
}

int ft_audio_set_input_gain(uint8_t gain)
{
    if (gain > FT_AUDIO_INPUT_GAIN_MAX) gain = FT_AUDIO_INPUT_GAIN_MAX;
    return audio_set_level(FT_AUDIO_INPUT_DEVICE, gain);
}

bool ft_audio_output_format_supported(uint32_t sample_rate,
                                      uint8_t sample_bits,
                                      uint8_t channels)
{
    size_t i;
    bool rate_supported = false;
    bool bits_supported = false;
    bool channels_supported = false;
    for (i = 0U; i < sizeof(s_output_sample_rates) / sizeof(s_output_sample_rates[0]); i++)
        if (s_output_sample_rates[i] == sample_rate) rate_supported = true;
    for (i = 0U; i < sizeof(s_output_sample_bits) / sizeof(s_output_sample_bits[0]); i++)
        if (s_output_sample_bits[i] == sample_bits) bits_supported = true;
    for (i = 0U; i < sizeof(s_output_channels) / sizeof(s_output_channels[0]); i++)
        if (s_output_channels[i] == channels) channels_supported = true;
    return rate_supported && bits_supported && channels_supported;
}

int ft_audio_set_output_format(uint32_t sample_rate, uint8_t sample_bits,
                               uint8_t channels)
{
    struct rt_audio_caps caps;
    rt_device_t device;
    int result;

    if (!ft_audio_output_format_supported(sample_rate, sample_bits, channels))
        return -RT_EINVAL;
    device = rt_device_find(FT_AUDIO_OUTPUT_DEVICE);
    if (device == RT_NULL) return -RT_ENOSYS;
    result = rt_device_init(device);
    if (result != RT_EOK) return result;
    memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = sample_rate;
    caps.udata.config.samplebits = sample_bits;
    caps.udata.config.channels = channels;
    return rt_device_control(device, AUDIO_CTL_CONFIGURE, &caps);
}

#ifdef RT_USING_MSH
static int feather_audio_status(int argc, char **argv)
{
    ft_audio_status_t status;
    int result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);
    result = ft_audio_get_status(&status);
    rt_kprintf("output sound0: %s/%s, %lu Hz, %u ch, %u bit, volume %u\n",
               status.output_registered ? "registered" : "missing",
               status.output_ready ? "ready" : "not-ready",
               (unsigned long)status.output_sample_rate,
               status.output_channels, status.output_sample_bits,
               status.output_volume);
    rt_kprintf("input mic0: %s/%s, %lu Hz, %u ch, %u bit, gain %u\n",
               status.input_registered ? "registered" : "missing",
               status.input_ready ? "ready" : "not-ready",
               (unsigned long)status.input_sample_rate,
               status.input_channels, status.input_sample_bits,
               status.input_gain);
    rt_kprintf("analog input: driver unavailable, result %d\n", result);
    return result;
}
MSH_CMD_EXPORT(feather_audio_status, Show FeatherTalk audio devices and levels.);

static int feather_audio_format(int argc, char **argv)
{
    uint32_t sample_rate;
    uint8_t sample_bits;
    uint8_t channels;
    int result;

    if (argc != 4)
    {
        rt_kprintf("usage: feather_audio_format <16000|24000|48000|96000> <16|24> <1|2>\n");
        return -RT_EINVAL;
    }
    sample_rate = (uint32_t)strtoul(argv[1], RT_NULL, 10);
    sample_bits = (uint8_t)strtoul(argv[2], RT_NULL, 10);
    channels = (uint8_t)strtoul(argv[3], RT_NULL, 10);
    result = ft_audio_set_output_format(sample_rate, sample_bits, channels);
    rt_kprintf("sound0 format: %lu Hz, %u bit, %u ch -> %d\n",
               (unsigned long)sample_rate, sample_bits, channels, result);
    return result;
}
MSH_CMD_EXPORT(feather_audio_format, Configure sound0 sample rate depth and channels.);
#endif

#else /* RT_USING_AUDIO */

int ft_audio_get_status(ft_audio_status_t *status)
{
    if (status == RT_NULL) return -RT_EINVAL;
    memset(status, 0, sizeof(*status));
    status->last_error = -RT_ENOSYS;
    return -RT_ENOSYS;
}

int ft_audio_set_output_volume(uint8_t volume)
{
    RT_UNUSED(volume);
    return -RT_ENOSYS;
}

int ft_audio_set_input_gain(uint8_t gain)
{
    RT_UNUSED(gain);
    return -RT_ENOSYS;
}

bool ft_audio_output_format_supported(uint32_t sample_rate,
                                      uint8_t sample_bits,
                                      uint8_t channels)
{
    RT_UNUSED(sample_rate);
    RT_UNUSED(sample_bits);
    RT_UNUSED(channels);
    return false;
}

int ft_audio_set_output_format(uint32_t sample_rate, uint8_t sample_bits,
                               uint8_t channels)
{
    RT_UNUSED(sample_rate);
    RT_UNUSED(sample_bits);
    RT_UNUSED(channels);
    return -RT_ENOSYS;
}

int ft_audio_claim_output(ft_audio_output_owner_t owner)
{
    RT_UNUSED(owner);
    return -RT_ENOSYS;
}

void ft_audio_release_output(ft_audio_output_owner_t owner)
{
    RT_UNUSED(owner);
}

ft_audio_output_owner_t ft_audio_get_output_owner(void)
{
    return FT_AUDIO_OUTPUT_OWNER_NONE;
}

#endif /* RT_USING_AUDIO */
