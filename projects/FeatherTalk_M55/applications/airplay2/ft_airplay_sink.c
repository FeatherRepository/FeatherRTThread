#include <rtthread.h>
#include <rtdevice.h>
#include "ft_airplay_sink.h"
#include "../feathertalk_audio.h"
static struct rt_mutex s_lock;
static rt_device_t s_device;
static size_t s_frame_bytes;
static int s_ready;
static int sink_init(void)
{
    int result = rt_mutex_init(&s_lock, "ap_pcm", RT_IPC_FLAG_PRIO);
    s_ready = result == RT_EOK;
    return result;
}
INIT_APP_EXPORT(sink_init);
int ft_ap_sink_open(uint32_t rate, uint8_t bits, uint8_t channels)
{
    int result;
    rt_device_t device;
    if (!s_ready) return -RT_ERROR;
    if (!ft_audio_output_format_supported(rate, bits, channels)) return -RT_EINVAL;
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (s_device) { result = -RT_EBUSY; goto done; }
    device = rt_device_find("sound0");
    if (!device) { result = -RT_ENOSYS; goto done; }
    result = ft_audio_claim_output(FT_AUDIO_OUTPUT_OWNER_AIRPLAY2);
    if (result != RT_EOK) goto done;
    result = ft_audio_set_output_format(rate, bits, channels);
    if (result == RT_EOK) result = rt_device_open(device, RT_DEVICE_OFLAG_WRONLY);
    if (result != RT_EOK) ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_AIRPLAY2);
    else { s_device = device; s_frame_bytes = (bits / 8U) * channels; }
done:
    rt_mutex_release(&s_lock);
    return result;
}
int ft_ap_sink_write(const void *pcm, size_t bytes)
{
    int result;
    if (!s_ready || !pcm || !bytes || bytes > 4096) return -RT_EINVAL;
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (!s_device) result = -RT_ERROR;
    else if (bytes % s_frame_bytes) result = -RT_EINVAL;
    else result = (int)rt_device_write(s_device, 0, pcm, bytes);
    rt_mutex_release(&s_lock);
    return result;
}
int ft_ap_sink_close(void)
{
    int result = RT_EOK;
    if (!s_ready) return -RT_ERROR;
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (s_device) {
        result = rt_device_close(s_device);
        /* A failed close retains ownership: do not race another audio source. */
        if (result == RT_EOK) {
            s_device = RT_NULL; s_frame_bytes = 0;
            ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_AIRPLAY2);
        }
    }
    rt_mutex_release(&s_lock);
    return result;
}
