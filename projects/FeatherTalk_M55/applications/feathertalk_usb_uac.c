#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include <board.h>
#include <drivers/audio.h>
#include "feathertalk_audio.h"
#include "feathertalk_usb_uac.h"
#include "usbd_core.h"
#include "usbd_audio.h"

#if defined(FEATHERTALK_USING_USB_UAC) && \
    defined(RT_USING_CHERRYUSB) && defined(RT_CHERRYUSB_DEVICE) && \
    defined(RT_CHERRYUSB_DEVICE_AUDIO) && defined(RT_USING_AUDIO)

#define FT_UAC_BUS_ID                 0U
#define FT_UAC_OUT_EP                 0x02U
#define FT_UAC_IN_EP                  0x81U
#define FT_UAC_OUT_INTERFACE          1U
#define FT_UAC_IN_INTERFACE           2U
#define FT_UAC_OUT_CLOCK_ID           0x01U
#define FT_UAC_OUT_FEATURE_ID         0x03U
#define FT_UAC_IN_CLOCK_ID            0x05U
#define FT_UAC_IN_FEATURE_ID          0x07U
#define FT_UAC_VENDOR_ID              0xFFFFU
#define FT_UAC_PRODUCT_ID             0xF502U
#define FT_UAC_MAX_POWER_MA           100U
#define FT_UAC_OUT_ALT_COUNT          2U
#define FT_UAC_MAX_PACKET             576U
#define FT_UAC_INPUT_PACKET           64U
#define FT_UAC_RING_SIZE              16384U
/* RT-Audio owns two 4096-byte replay-pool blocks.  Do not start sound0 from
 * a single 192/288/384/576-byte USB packet: its replay queue would still be
 * empty when sound_start() requests the first frame. */
#define FT_UAC_AUDIO_BLOCK            4096U
#define FT_UAC_THREAD_STACK           4096U
#define FT_UAC_THREAD_PRIORITY        12U
#define FT_UAC_EVENT_FORMAT           0x01U
#define FT_UAC_EVENT_OUTPUT_DATA      0x02U
#define FT_UAC_EVENT_INPUT_READY      0x04U
#define FT_UAC_EVENT_STOP             0x08U

#ifdef CONFIG_USB_HS
#define FT_UAC_EP_INTERVAL 0x04U
#else
#define FT_UAC_EP_INTERVAL 0x01U
#endif

#define FT_UAC_CHANNEL_CONFIG_MONO   0x00000001UL
#define FT_UAC_CHANNEL_CONFIG_STEREO 0x00000003UL
#define FT_UAC_FEATURE_CONTROLS \
    DBVAL(AUDIO_V2_CONTROL_MUTE | AUDIO_V2_CONTROL_VOLUME), \
    DBVAL(AUDIO_V2_CONTROL_MUTE | AUDIO_V2_CONTROL_VOLUME), \
    DBVAL(AUDIO_V2_CONTROL_MUTE | AUDIO_V2_CONTROL_VOLUME)

#define FT_UAC_AC_SIZE (AUDIO_V2_SIZEOF_AC_HEADER_DESC + \
    AUDIO_V2_SIZEOF_AC_CLOCK_SOURCE_DESC + \
    AUDIO_V2_SIZEOF_AC_INPUT_TERMINAL_DESC + \
    AUDIO_V2_SIZEOF_AC_FEATURE_UNIT_DESC(2) + \
    AUDIO_V2_SIZEOF_AC_OUTPUT_TERMINAL_DESC + \
    AUDIO_V2_SIZEOF_AC_CLOCK_SOURCE_DESC + \
    AUDIO_V2_SIZEOF_AC_INPUT_TERMINAL_DESC + \
    AUDIO_V2_SIZEOF_AC_FEATURE_UNIT_DESC(2) + \
    AUDIO_V2_SIZEOF_AC_OUTPUT_TERMINAL_DESC)

#define FT_UAC_CONFIG_SIZE (9U + AUDIO_V2_AC_DESCRIPTOR_LEN + \
    AUDIO_V2_SIZEOF_AC_CLOCK_SOURCE_DESC + \
    AUDIO_V2_SIZEOF_AC_INPUT_TERMINAL_DESC + \
    AUDIO_V2_SIZEOF_AC_FEATURE_UNIT_DESC(2) + \
    AUDIO_V2_SIZEOF_AC_OUTPUT_TERMINAL_DESC + \
    AUDIO_V2_SIZEOF_AC_CLOCK_SOURCE_DESC + \
    AUDIO_V2_SIZEOF_AC_INPUT_TERMINAL_DESC + \
    AUDIO_V2_SIZEOF_AC_FEATURE_UNIT_DESC(2) + \
    AUDIO_V2_SIZEOF_AC_OUTPUT_TERMINAL_DESC + \
    AUDIO_V2_AS_ALTSETTING0_DESCRIPTOR_LEN + \
    FT_UAC_OUT_ALT_COUNT * AUDIO_V2_AS_ALTSETTING_DESCRIPTOR_LEN + \
    AUDIO_V2_AS_ALTSETTING0_DESCRIPTOR_LEN + \
    AUDIO_V2_AS_ALTSETTING_DESCRIPTOR_LEN)

static uint8_t s_device_descriptor[] =
{
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00,
                               FT_UAC_VENDOR_ID, FT_UAC_PRODUCT_ID,
                               0x0100, 0x01)
};

static const uint8_t s_config_descriptor[] =
{
    USB_CONFIG_DESCRIPTOR_INIT(FT_UAC_CONFIG_SIZE, 0x03, 0x01,
                               USB_CONFIG_BUS_POWERED,
                               FT_UAC_MAX_POWER_MA),
    AUDIO_V2_AC_DESCRIPTOR_INIT(0x00, 0x03, FT_UAC_AC_SIZE,
                                AUDIO_CATEGORY_UNDEF, 0x00, 0x00),
    AUDIO_V2_AC_CLOCK_SOURCE_DESCRIPTOR_INIT(FT_UAC_OUT_CLOCK_ID, 0x03, 0x03),
    AUDIO_V2_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(
        0x02, AUDIO_TERMINAL_STREAMING, FT_UAC_OUT_CLOCK_ID, 2,
        FT_UAC_CHANNEL_CONFIG_STEREO, 0x0000),
    AUDIO_V2_AC_FEATURE_UNIT_DESCRIPTOR_INIT(
        FT_UAC_OUT_FEATURE_ID, 0x02, FT_UAC_FEATURE_CONTROLS),
    AUDIO_V2_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(
        0x04, AUDIO_OUTTERM_SPEAKER, FT_UAC_OUT_FEATURE_ID,
        FT_UAC_OUT_CLOCK_ID, 0x0000),
    AUDIO_V2_AC_CLOCK_SOURCE_DESCRIPTOR_INIT(FT_UAC_IN_CLOCK_ID, 0x03, 0x03),
    AUDIO_V2_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(
        0x06, AUDIO_INTERM_MIC, FT_UAC_IN_CLOCK_ID, 2,
        FT_UAC_CHANNEL_CONFIG_STEREO, 0x0000),
    AUDIO_V2_AC_FEATURE_UNIT_DESCRIPTOR_INIT(
        FT_UAC_IN_FEATURE_ID, 0x06, FT_UAC_FEATURE_CONTROLS),
    AUDIO_V2_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(
        0x08, AUDIO_TERMINAL_STREAMING, FT_UAC_IN_FEATURE_ID,
        FT_UAC_IN_CLOCK_ID, 0x0000),

    /* Host playback: the USB terminal has a fixed stereo cluster, then
     * alternate settings select 16-bit or packed 24-bit samples. */
    0x09, USB_DESCRIPTOR_TYPE_INTERFACE, FT_UAC_OUT_INTERFACE, 0x00,
    0x00, USB_DEVICE_CLASS_AUDIO, AUDIO_SUBCLASS_AUDIOSTREAMING,
    AUDIO_PROTOCOLv20, 0x00,
    AUDIO_V2_AS_ALTSETTING_DESCRIPTOR_INIT(
        FT_UAC_OUT_INTERFACE, 0x01, 0x02, 2,
        FT_UAC_CHANNEL_CONFIG_STEREO, 2, 16, FT_UAC_OUT_EP, 0x09,
        384, FT_UAC_EP_INTERVAL),
    AUDIO_V2_AS_ALTSETTING_DESCRIPTOR_INIT(
        FT_UAC_OUT_INTERFACE, 0x02, 0x02, 2,
        FT_UAC_CHANNEL_CONFIG_STEREO, 3, 24, FT_UAC_OUT_EP, 0x09,
        576, FT_UAC_EP_INTERVAL),

    /* The current PDM driver is genuinely fixed at 16 kHz / 16 bit /
     * stereo, so USB must not advertise formats it cannot produce. */
    0x09, USB_DESCRIPTOR_TYPE_INTERFACE, FT_UAC_IN_INTERFACE, 0x00,
    0x00, USB_DEVICE_CLASS_AUDIO, AUDIO_SUBCLASS_AUDIOSTREAMING,
    AUDIO_PROTOCOLv20, 0x00,
    AUDIO_V2_AS_ALTSETTING_DESCRIPTOR_INIT(
        FT_UAC_IN_INTERFACE, 0x01, 0x08, 2,
        FT_UAC_CHANNEL_CONFIG_STEREO, 2, 16, FT_UAC_IN_EP, 0x05,
        FT_UAC_INPUT_PACKET + 4U, FT_UAC_EP_INTERVAL),
};

static const uint8_t s_qualifier_descriptor[] =
{
    0x0A, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

static const char *s_string_descriptors[] =
{
    (const char[]){0x09, 0x04},
    "FeatherRepository",
    "FeatherTalk Bidirectional USB Audio",
    "FTALK-UAC2-0001",
};

static const uint8_t s_output_rates[] =
{
    AUDIO_SAMPLE_FREQ_NUM(4),
    AUDIO_SAMPLE_FREQ_4B(16000), AUDIO_SAMPLE_FREQ_4B(16000),
    AUDIO_SAMPLE_FREQ_4B(0),
    AUDIO_SAMPLE_FREQ_4B(24000), AUDIO_SAMPLE_FREQ_4B(24000),
    AUDIO_SAMPLE_FREQ_4B(0),
    AUDIO_SAMPLE_FREQ_4B(48000), AUDIO_SAMPLE_FREQ_4B(48000),
    AUDIO_SAMPLE_FREQ_4B(0),
    AUDIO_SAMPLE_FREQ_4B(96000), AUDIO_SAMPLE_FREQ_4B(96000),
    AUDIO_SAMPLE_FREQ_4B(0),
};

static const uint8_t s_input_rates[] =
{
    AUDIO_SAMPLE_FREQ_NUM(1),
    AUDIO_SAMPLE_FREQ_4B(16000), AUDIO_SAMPLE_FREQ_4B(16000),
    AUDIO_SAMPLE_FREQ_4B(0),
};

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX
static uint8_t s_out_buffers[2][FT_UAC_MAX_PACKET];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX
static uint8_t s_in_buffer[FT_UAC_MAX_PACKET];

static uint8_t s_out_ring[FT_UAC_RING_SIZE];
static uint8_t s_output_worker_block[FT_UAC_AUDIO_BLOCK];
static uint8_t s_input_worker_block[FT_UAC_AUDIO_BLOCK];
static volatile uint32_t s_out_ring_read;
static volatile uint32_t s_out_ring_write;
static volatile uint32_t s_out_ring_used;
static volatile uint8_t s_out_buffer_index;
static volatile bool s_in_endpoint_busy;
static volatile bool s_output_device_open;
static struct rt_event s_uac_event;
static bool s_primitives_ready;
static rt_thread_t s_output_thread;
static rt_thread_t s_input_thread;
static ft_usb_uac_status_t s_status;
static uint32_t s_requested_generation;
static volatile uint32_t s_negotiated_output_rate;
static volatile uint8_t s_negotiated_output_bits;
static volatile uint8_t s_negotiated_output_channels;
static uint8_t s_output_volume = 70U;
static uint8_t s_input_gain = 40U;
static bool s_output_mute;
static bool s_input_mute;
static volatile uint32_t s_output_control_generation;
static volatile uint32_t s_input_control_generation;

static struct usbd_interface s_control_interface;
static struct usbd_interface s_output_interface;
static struct usbd_interface s_input_interface;
static struct usbd_endpoint s_output_endpoint;
static struct usbd_endpoint s_input_endpoint;

static struct audio_entity_info s_entities[] =
{
    {.bDescriptorSubtype = AUDIO_CONTROL_CLOCK_SOURCE,
     .bEntityId = FT_UAC_OUT_CLOCK_ID, .ep = FT_UAC_OUT_EP},
    {.bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT,
     .bEntityId = FT_UAC_OUT_FEATURE_ID, .ep = FT_UAC_OUT_EP},
    {.bDescriptorSubtype = AUDIO_CONTROL_CLOCK_SOURCE,
     .bEntityId = FT_UAC_IN_CLOCK_ID, .ep = FT_UAC_IN_EP},
    {.bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT,
     .bEntityId = FT_UAC_IN_FEATURE_ID, .ep = FT_UAC_IN_EP},
};

static const uint8_t *uac_device_descriptor_cb(uint8_t speed)
{
    RT_UNUSED(speed);
    return s_device_descriptor;
}

static const uint8_t *uac_config_descriptor_cb(uint8_t speed)
{
    RT_UNUSED(speed);
    return s_config_descriptor;
}

static const uint8_t *uac_qualifier_descriptor_cb(uint8_t speed)
{
    RT_UNUSED(speed);
    return s_qualifier_descriptor;
}

static const char *uac_string_descriptor_cb(uint8_t speed, uint8_t index)
{
    RT_UNUSED(speed);
    if (index >= sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]))
        return RT_NULL;
    return s_string_descriptors[index];
}

static const struct usb_descriptor s_uac_descriptor =
{
    .device_descriptor_callback = uac_device_descriptor_cb,
    .config_descriptor_callback = uac_config_descriptor_cb,
    .device_quality_descriptor_callback = uac_qualifier_descriptor_cb,
    .string_descriptor_callback = uac_string_descriptor_cb,
};

bool ft_usb_uac_output_format_supported(uint32_t sample_rate,
                                        uint8_t sample_bits,
                                        uint8_t channels)
{
    return channels == 2U &&
           ft_audio_output_format_supported(sample_rate, sample_bits,
                                             channels);
}

bool ft_usb_uac_input_format_supported(uint32_t sample_rate,
                                       uint8_t sample_bits,
                                       uint8_t channels)
{
    return sample_rate == 16000U && sample_bits == 16U && channels == 2U;
}

static uint32_t output_packet_size(void)
{
    uint32_t bytes = s_negotiated_output_rate *
        s_negotiated_output_channels *
        (s_negotiated_output_bits / 8U) / 1000U;
    if (bytes == 0U || bytes > FT_UAC_MAX_PACKET) bytes = FT_UAC_MAX_PACKET;
    return bytes;
}

static void output_ring_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_out_ring_read = 0U;
    s_out_ring_write = 0U;
    s_out_ring_used = 0U;
    s_status.output_ring_used = 0U;
    rt_hw_interrupt_enable(level);
}

static void output_ring_write(const uint8_t *data, uint32_t length)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint32_t free_bytes = FT_UAC_RING_SIZE - s_out_ring_used;
    uint32_t index;

    if (length > free_bytes)
    {
        s_status.output_overruns++;
        length = free_bytes;
    }
    for (index = 0U; index < length; index++)
    {
        s_out_ring[s_out_ring_write++] = data[index];
        if (s_out_ring_write == FT_UAC_RING_SIZE) s_out_ring_write = 0U;
    }
    s_out_ring_used += length;
    s_status.output_ring_used = s_out_ring_used;
    s_status.host_to_device_bytes += length;
    rt_hw_interrupt_enable(level);
}

static uint32_t output_ring_read(uint8_t *data, uint32_t capacity,
                                 uint32_t frame_bytes)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint32_t length = s_out_ring_used < capacity ? s_out_ring_used : capacity;
    uint32_t index;

    if (frame_bytes == 0U) frame_bytes = 1U;
    length -= length % frame_bytes;
    for (index = 0U; index < length; index++)
    {
        data[index] = s_out_ring[s_out_ring_read++];
        if (s_out_ring_read == FT_UAC_RING_SIZE) s_out_ring_read = 0U;
    }
    s_out_ring_used -= length;
    s_status.output_ring_used = s_out_ring_used;
    s_status.output_ring_read_bytes += length;
    rt_hw_interrupt_enable(level);
    return length;
}

static void queue_output_format(uint32_t rate, uint8_t bits,
                                uint8_t channels, bool from_host)
{
    rt_base_t level;
    if (!ft_usb_uac_output_format_supported(rate, bits, channels)) return;
    level = rt_hw_interrupt_disable();
    s_status.output_sample_rate = rate;
    s_status.output_sample_bits = bits;
    s_status.output_channels = channels;
    s_status.format_pending = true;
    s_status.sync_generation++;
    if (from_host) s_status.host_update_count++;
    else s_status.device_update_count++;
    s_requested_generation = s_status.sync_generation;
    rt_hw_interrupt_enable(level);
    output_ring_reset();
    rt_event_send(&s_uac_event, FT_UAC_EVENT_FORMAT);
}

static void output_endpoint_callback(uint8_t busid, uint8_t ep,
                                     uint32_t nbytes)
{
    uint8_t completed = s_out_buffer_index;
    RT_UNUSED(ep);
    if (!s_status.active || !s_status.output_streaming) return;
    s_status.output_callback_count++;
    if (s_status.output_sample_rate != s_negotiated_output_rate ||
        s_status.output_sample_bits != s_negotiated_output_bits ||
        s_status.output_channels != s_negotiated_output_channels)
        queue_output_format(s_negotiated_output_rate,
                            s_negotiated_output_bits,
                            s_negotiated_output_channels, true);
    output_ring_write(s_out_buffers[completed], nbytes);
    s_out_buffer_index ^= 1U;
    (void)usbd_ep_start_read(busid, FT_UAC_OUT_EP,
                             s_out_buffers[s_out_buffer_index],
                             output_packet_size());
    rt_event_send(&s_uac_event, FT_UAC_EVENT_OUTPUT_DATA);
}

static void input_endpoint_callback(uint8_t busid, uint8_t ep,
                                    uint32_t nbytes)
{
    RT_UNUSED(busid);
    RT_UNUSED(ep);
    RT_UNUSED(nbytes);
    s_in_endpoint_busy = false;
    rt_event_send(&s_uac_event, FT_UAC_EVENT_INPUT_READY);
}

static void audio_interface_notify(uint8_t busid, uint8_t event, void *arg)
{
    struct usb_interface_descriptor *interface_descriptor;
    uint8_t alternate;

    if (event != USBD_EVENT_SET_INTERFACE || arg == RT_NULL) return;
    interface_descriptor = (struct usb_interface_descriptor *)arg;
    alternate = interface_descriptor->bAlternateSetting;
    if (interface_descriptor->bInterfaceNumber == FT_UAC_OUT_INTERFACE)
    {
        if (alternate == 0U)
        {
            s_status.output_streaming = false;
            output_ring_reset();
            rt_event_send(&s_uac_event, FT_UAC_EVENT_STOP);
            return;
        }
        if (alternate == 1U)
        {
            s_negotiated_output_bits = 16U;
            s_negotiated_output_channels = 2U;
        }
        else if (alternate == 2U)
        {
            s_negotiated_output_bits = 24U;
            s_negotiated_output_channels = 2U;
        }
        else
            return;
        s_status.output_streaming = true;
        s_out_buffer_index = 0U;
        (void)usbd_ep_start_read(busid, FT_UAC_OUT_EP,
                                 s_out_buffers[0], output_packet_size());
    }
    else if (interface_descriptor->bInterfaceNumber == FT_UAC_IN_INTERFACE)
    {
        s_status.input_streaming = alternate == 1U;
        if (s_status.input_streaming)
        {
            s_status.host_update_count++;
            s_status.sync_generation++;
            rt_event_send(&s_uac_event, FT_UAC_EVENT_INPUT_READY);
        }
    }
}

static void usb_event_handler(uint8_t busid, uint8_t event)
{
    RT_UNUSED(busid);
    if (event == USBD_EVENT_CONNECTED)
        s_status.connected = true;
    else if (event == USBD_EVENT_CONFIGURED)
    {
        s_status.connected = true;
        s_status.configured = true;
    }
    else if (event == USBD_EVENT_DISCONNECTED || event == USBD_EVENT_DEINIT)
    {
        s_status.connected = false;
        s_status.configured = false;
        s_status.output_streaming = false;
        s_status.input_streaming = false;
        output_ring_reset();
    }
}

void usbd_audio_set_sampling_freq(uint8_t busid, uint8_t ep,
                                  uint32_t sampling_freq)
{
    RT_UNUSED(busid);
    if (ep == FT_UAC_OUT_EP)
    {
        if (ft_usb_uac_output_format_supported(
                sampling_freq, s_negotiated_output_bits,
                s_negotiated_output_channels))
            s_negotiated_output_rate = sampling_freq;
    }
    else if (ep == FT_UAC_IN_EP && sampling_freq == 16000U)
    {
        s_status.host_update_count++;
        s_status.sync_generation++;
    }
}

uint32_t usbd_audio_get_sampling_freq(uint8_t busid, uint8_t ep)
{
    RT_UNUSED(busid);
    return ep == FT_UAC_OUT_EP ? s_negotiated_output_rate :
           ep == FT_UAC_IN_EP ? s_status.input_sample_rate : 0U;
}

void usbd_audio_get_sampling_freq_table(uint8_t busid, uint8_t ep,
                                        uint8_t **sampling_freq_table)
{
    RT_UNUSED(busid);
    if (sampling_freq_table == RT_NULL) return;
    *sampling_freq_table = (uint8_t *)(ep == FT_UAC_OUT_EP ?
        s_output_rates : s_input_rates);
}

void usbd_audio_set_volume(uint8_t busid, uint8_t ep, uint8_t channel,
                           int volume_db)
{
    RT_UNUSED(busid);
    RT_UNUSED(channel);
    if (volume_db < -100) volume_db = -100;
    if (volume_db > 0) volume_db = 0;
    if (ep == FT_UAC_OUT_EP)
    {
        s_output_volume = (uint8_t)(volume_db + 100);
        s_output_control_generation++;
        rt_event_send(&s_uac_event, FT_UAC_EVENT_OUTPUT_DATA);
    }
    else if (ep == FT_UAC_IN_EP)
    {
        s_input_gain = (uint8_t)((volume_db + 100) * 75 / 100);
        s_input_control_generation++;
    }
}

int usbd_audio_get_volume(uint8_t busid, uint8_t ep, uint8_t channel)
{
    RT_UNUSED(busid);
    RT_UNUSED(channel);
    return ep == FT_UAC_OUT_EP ? (int)s_output_volume - 100 :
           ep == FT_UAC_IN_EP ? (int)s_input_gain * 100 / 75 - 100 : -100;
}

void usbd_audio_set_mute(uint8_t busid, uint8_t ep, uint8_t channel, bool mute)
{
    RT_UNUSED(busid);
    RT_UNUSED(channel);
    if (ep == FT_UAC_OUT_EP)
    {
        s_output_mute = mute;
        s_output_control_generation++;
        rt_event_send(&s_uac_event, FT_UAC_EVENT_OUTPUT_DATA);
    }
    else if (ep == FT_UAC_IN_EP)
    {
        s_input_mute = mute;
        s_input_control_generation++;
    }
}

bool usbd_audio_get_mute(uint8_t busid, uint8_t ep, uint8_t channel)
{
    RT_UNUSED(busid);
    RT_UNUSED(channel);
    return ep == FT_UAC_OUT_EP ? s_output_mute :
           ep == FT_UAC_IN_EP ? s_input_mute : false;
}

void usbd_audio_open(uint8_t busid, uint8_t interface_number)
{
    RT_UNUSED(busid);
    RT_UNUSED(interface_number);
}

void usbd_audio_close(uint8_t busid, uint8_t interface_number)
{
    RT_UNUSED(busid);
    RT_UNUSED(interface_number);
}

static void output_worker(void *parameter)
{
    rt_device_t device = RT_NULL;
    uint32_t applied_generation = 0U;
    uint32_t applied_control_generation = 0U;
    RT_UNUSED(parameter);

    while (1)
    {
        rt_uint32_t received = 0U;
        s_status.output_worker_state = 1U;
        (void)rt_event_recv(&s_uac_event,
            FT_UAC_EVENT_FORMAT | FT_UAC_EVENT_OUTPUT_DATA |
            FT_UAC_EVENT_STOP,
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 20U, &received);
        s_status.output_worker_wakeups++;
        s_status.output_worker_state = 2U;
        if (applied_control_generation != s_output_control_generation)
        {
            (void)ft_audio_set_output_volume(s_output_mute ?
                                             0U : s_output_volume);
            applied_control_generation = s_output_control_generation;
        }
        if (!s_status.active || !s_status.output_streaming)
        {
            if (device != RT_NULL)
            {
                s_status.output_worker_state = 7U;
                (void)rt_device_close(device);
                device = RT_NULL;
                s_output_device_open = false;
            }
            ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_USB_UAC);
            continue;
        }
        if (applied_generation != s_requested_generation)
        {
            int result;
            s_status.output_worker_state = 3U;
            if (device != RT_NULL)
            {
                (void)rt_device_close(device);
                device = RT_NULL;
                s_output_device_open = false;
            }
            ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_USB_UAC);
            result = ft_audio_claim_output(FT_AUDIO_OUTPUT_OWNER_USB_UAC);
            if (result != RT_EOK)
            {
                s_status.last_error = result;
                continue;
            }
            result = ft_audio_set_output_format(
                s_status.output_sample_rate, s_status.output_sample_bits,
                s_status.output_channels);
            s_status.last_error = result;
            s_status.format_pending = result != RT_EOK;
            if (result != RT_EOK)
            {
                ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_USB_UAC);
                continue;
            }
            applied_generation = s_requested_generation;
        }
        if (device == RT_NULL)
        {
            s_status.output_worker_state = 4U;
            if (ft_audio_claim_output(FT_AUDIO_OUTPUT_OWNER_USB_UAC) != RT_EOK)
            {
                s_status.last_error = -RT_EBUSY;
                continue;
            }
            device = rt_device_find("sound0");
            if (device == RT_NULL ||
                rt_device_open(device, RT_DEVICE_OFLAG_WRONLY) != RT_EOK)
            {
                device = RT_NULL;
                ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_USB_UAC);
                s_status.last_error = -RT_EIO;
                continue;
            }
            s_status.output_sound_open_count++;
            s_output_device_open = true;
        }
        while (s_out_ring_used >= FT_UAC_AUDIO_BLOCK &&
               s_status.output_streaming)
        {
            uint32_t frame_bytes = s_status.output_channels *
                (s_status.output_sample_bits / 8U);
            uint32_t count = output_ring_read(s_output_worker_block,
                                              sizeof(s_output_worker_block),
                                              frame_bytes);
            if (count == 0U) break;
            s_status.output_worker_state = 5U;
            s_status.output_sound_write_calls++;
            if (rt_device_write(device, 0, s_output_worker_block, count) !=
                (rt_ssize_t)count)
            {
                s_status.last_error = -RT_EIO;
                break;
            }
            s_status.output_sound_write_bytes += count;
            s_status.output_worker_state = 6U;
        }
    }
}

static void input_worker(void *parameter)
{
    rt_device_t device = RT_NULL;
    uint32_t applied_control_generation = 0U;
    RT_UNUSED(parameter);

    while (1)
    {
        if (applied_control_generation != s_input_control_generation)
        {
            (void)ft_audio_set_input_gain(s_input_mute ? 0U : s_input_gain);
            applied_control_generation = s_input_control_generation;
        }
        if (!s_status.active || !s_status.input_streaming)
        {
            if (device != RT_NULL)
            {
                (void)rt_device_close(device);
                device = RT_NULL;
            }
            rt_thread_mdelay(10U);
            continue;
        }
        if (device == RT_NULL)
        {
            device = rt_device_find("mic0");
            if (device == RT_NULL ||
                rt_device_open(device, RT_DEVICE_OFLAG_RDONLY) != RT_EOK)
            {
                device = RT_NULL;
                s_status.last_error = -RT_EIO;
                rt_thread_mdelay(20U);
                continue;
            }
        }
        {
            rt_ssize_t count = rt_device_read(device, 0,
                                              s_input_worker_block,
                                              sizeof(s_input_worker_block));
            rt_ssize_t offset = 0;
            if (count <= 0) continue;
            while (offset < count && s_status.input_streaming)
            {
                uint32_t packet = (uint32_t)(count - offset);
                rt_uint32_t received;
                if (packet > FT_UAC_INPUT_PACKET) packet = FT_UAC_INPUT_PACKET;
                rt_memset(s_in_buffer, 0, FT_UAC_INPUT_PACKET);
                rt_memcpy(s_in_buffer, s_input_worker_block + offset,
                          packet);
                s_in_endpoint_busy = true;
                if (usbd_ep_start_write(FT_UAC_BUS_ID, FT_UAC_IN_EP,
                                        s_in_buffer,
                                        FT_UAC_INPUT_PACKET) < 0)
                {
                    s_in_endpoint_busy = false;
                    s_status.input_underruns++;
                    break;
                }
                (void)rt_event_recv(&s_uac_event,
                    FT_UAC_EVENT_INPUT_READY | FT_UAC_EVENT_STOP,
                    RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                    20U, &received);
                if (s_in_endpoint_busy)
                {
                    s_status.input_underruns++;
                    break;
                }
                s_status.device_to_host_bytes += FT_UAC_INPUT_PACKET;
                offset += packet;
            }
        }
    }
}

static int ensure_workers(void)
{
    if (!s_primitives_ready)
    {
        if (rt_event_init(&s_uac_event, "ft_uac", RT_IPC_FLAG_FIFO) != RT_EOK)
            return -RT_ERROR;
        s_primitives_ready = true;
    }
    if (s_output_thread == RT_NULL)
    {
        s_output_thread = rt_thread_create("uac_out", output_worker, RT_NULL,
            FT_UAC_THREAD_STACK, FT_UAC_THREAD_PRIORITY, 10U);
        if (s_output_thread == RT_NULL) return -RT_ENOMEM;
        rt_thread_startup(s_output_thread);
    }
    if (s_input_thread == RT_NULL)
    {
        s_input_thread = rt_thread_create("uac_in", input_worker, RT_NULL,
            FT_UAC_THREAD_STACK, FT_UAC_THREAD_PRIORITY, 10U);
        if (s_input_thread == RT_NULL) return -RT_ENOMEM;
        rt_thread_startup(s_input_thread);
    }
    return RT_EOK;
}

static int initialize_usb(void)
{
    struct usbd_interface *interface_pointer;

    rt_memset(&s_control_interface, 0, sizeof(s_control_interface));
    rt_memset(&s_output_interface, 0, sizeof(s_output_interface));
    rt_memset(&s_input_interface, 0, sizeof(s_input_interface));
    s_output_endpoint.ep_addr = FT_UAC_OUT_EP;
    s_output_endpoint.ep_cb = output_endpoint_callback;
    s_input_endpoint.ep_addr = FT_UAC_IN_EP;
    s_input_endpoint.ep_cb = input_endpoint_callback;

    usbd_desc_register(FT_UAC_BUS_ID, &s_uac_descriptor);
    interface_pointer = usbd_audio_init_intf(
        FT_UAC_BUS_ID, &s_control_interface, 0x0200,
        s_entities, sizeof(s_entities) / sizeof(s_entities[0]));
    usbd_add_interface(FT_UAC_BUS_ID, interface_pointer);
    interface_pointer = usbd_audio_init_intf(
        FT_UAC_BUS_ID, &s_output_interface, 0x0200,
        s_entities, sizeof(s_entities) / sizeof(s_entities[0]));
    interface_pointer->notify_handler = audio_interface_notify;
    usbd_add_interface(FT_UAC_BUS_ID, interface_pointer);
    interface_pointer = usbd_audio_init_intf(
        FT_UAC_BUS_ID, &s_input_interface, 0x0200,
        s_entities, sizeof(s_entities) / sizeof(s_entities[0]));
    interface_pointer->notify_handler = audio_interface_notify;
    usbd_add_interface(FT_UAC_BUS_ID, interface_pointer);
    usbd_add_endpoint(FT_UAC_BUS_ID, &s_output_endpoint);
    usbd_add_endpoint(FT_UAC_BUS_ID, &s_input_endpoint);
    return usbd_initialize(FT_UAC_BUS_ID, USBHS_BASE,
                           usb_event_handler) == 0 ? RT_EOK : -RT_ERROR;
}

int ft_usb_uac_start(void)
{
    ft_audio_status_t audio;
    int result;

    if (s_status.active) return RT_EOK;
    result = ensure_workers();
    if (result != RT_EOK) return result;
    (void)ft_audio_get_status(&audio);
    rt_memset(&s_status, 0, sizeof(s_status));
    if (ft_usb_uac_output_format_supported(audio.output_sample_rate,
            audio.output_sample_bits, audio.output_channels))
    {
        s_status.output_sample_rate = audio.output_sample_rate;
        s_status.output_sample_bits = audio.output_sample_bits;
        s_status.output_channels = audio.output_channels;
    }
    else
    {
        s_status.output_sample_rate = 16000U;
        s_status.output_sample_bits = 16U;
        s_status.output_channels = 2U;
    }
    s_status.input_sample_rate = 16000U;
    s_status.input_sample_bits = 16U;
    s_status.input_channels = 2U;
    s_negotiated_output_rate = s_status.output_sample_rate;
    s_negotiated_output_bits = s_status.output_sample_bits;
    s_negotiated_output_channels = s_status.output_channels;
    s_output_device_open = false;
    s_status.active = true;
    s_status.sync_generation = 1U;
    s_requested_generation = 1U;
    s_device_descriptor[12] = 0x02U;
    s_device_descriptor[13] = 0x01U;
    output_ring_reset();
    result = initialize_usb();
    if (result != RT_EOK)
    {
        s_status.active = false;
        s_status.last_error = result;
        return result;
    }
    s_status.last_error = RT_EOK;
    return RT_EOK;
}

int ft_usb_uac_stop(void)
{
    int result = RT_EOK;
    if (s_status.active && usbd_deinitialize(FT_UAC_BUS_ID) != 0)
        result = -RT_ERROR;
    s_status.active = false;
    s_status.connected = false;
    s_status.configured = false;
    s_status.output_streaming = false;
    s_status.input_streaming = false;
    s_status.format_pending = false;
    output_ring_reset();
    if (s_primitives_ready)
        rt_event_send(&s_uac_event, FT_UAC_EVENT_STOP);
    s_status.last_error = result;
    return result;
}

void ft_usb_uac_get_status(ft_usb_uac_status_t *status)
{
    rt_base_t level;
    if (status == RT_NULL) return;
    level = rt_hw_interrupt_disable();
    *status = s_status;
    rt_hw_interrupt_enable(level);
}

int ft_usb_uac_set_output_format(uint32_t sample_rate, uint8_t sample_bits,
                                 uint8_t channels, bool reconnect_host)
{
    int result = RT_EOK;

    if (!ft_usb_uac_output_format_supported(sample_rate, sample_bits, channels))
        return -RT_EINVAL;
    if (!s_status.active)
    {
        result = ft_audio_set_output_format(sample_rate, sample_bits, channels);
        if (result != RT_EOK) return result;
        s_status.output_sample_rate = sample_rate;
        s_status.output_sample_bits = sample_bits;
        s_status.output_channels = channels;
        return RT_EOK;
    }

    /* A UI-side format update may arrive while the host is streaming. Stop
     * the USB endpoints first so the worker can close sound0 before its TDM
     * clocks are changed. The new bcdDevice value forces a host capability
     * refresh after the physical format has been accepted. */
    if (reconnect_host)
    {
        (void)usbd_deinitialize(FT_UAC_BUS_ID);
        s_status.connected = false;
        s_status.configured = false;
        s_status.output_streaming = false;
        s_status.input_streaming = false;
        output_ring_reset();
        rt_event_send(&s_uac_event, FT_UAC_EVENT_STOP);
        {
            uint32_t wait_ms;
            for (wait_ms = 0U; wait_ms < 200U && s_output_device_open;
                 wait_ms += 5U)
                rt_thread_mdelay(5U);
            if (s_output_device_open)
            {
                s_status.last_error = -RT_EBUSY;
                (void)initialize_usb();
                return -RT_EBUSY;
            }
            /* Keep the pull-up absent long enough for Windows to retire the
             * old WaveRT pins before the same VID/PID/serial reappears. */
            rt_thread_mdelay(250U);
        }
    }

    result = ft_audio_set_output_format(sample_rate, sample_bits, channels);
    if (result != RT_EOK)
    {
        s_status.last_error = result;
        if (reconnect_host) (void)initialize_usb();
        return result;
    }
    queue_output_format(sample_rate, sample_bits, channels, false);
    s_negotiated_output_rate = sample_rate;
    s_negotiated_output_bits = sample_bits;
    s_negotiated_output_channels = channels;
    s_status.format_pending = false;
    if (reconnect_host)
    {
        s_device_descriptor[12]++;
        result = initialize_usb();
        s_status.last_error = result;
    }
    return result;
}

#else

bool ft_usb_uac_output_format_supported(uint32_t sample_rate,
                                        uint8_t sample_bits,
                                        uint8_t channels)
{
    RT_UNUSED(sample_rate); RT_UNUSED(sample_bits); RT_UNUSED(channels);
    return false;
}
bool ft_usb_uac_input_format_supported(uint32_t sample_rate,
                                       uint8_t sample_bits,
                                       uint8_t channels)
{
    RT_UNUSED(sample_rate); RT_UNUSED(sample_bits); RT_UNUSED(channels);
    return false;
}
int ft_usb_uac_start(void) { return -RT_ENOSYS; }
int ft_usb_uac_stop(void) { return -RT_ENOSYS; }
void ft_usb_uac_get_status(ft_usb_uac_status_t *status)
{ if (status != RT_NULL) rt_memset(status, 0, sizeof(*status)); }
int ft_usb_uac_set_output_format(uint32_t sample_rate, uint8_t sample_bits,
                                 uint8_t channels, bool reconnect_host)
{
    RT_UNUSED(sample_rate); RT_UNUSED(sample_bits); RT_UNUSED(channels);
    RT_UNUSED(reconnect_host); return -RT_ENOSYS;
}

#endif
