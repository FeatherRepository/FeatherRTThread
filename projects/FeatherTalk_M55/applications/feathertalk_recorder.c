#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#include "feathertalk_recorder.h"
#include "feathertalk_storage.h"

#ifdef RT_USING_AUDIO
#include <drivers/audio.h>
#endif

#ifdef RT_USING_DFS
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#endif

#define FT_RECORDER_BUFFER_SIZE       2048U
#define FT_RECORDER_THREAD_STACK      8192U
#define FT_RECORDER_THREAD_PRIORITY   15U
#define FT_RECORDER_THREAD_TIMESLICE  10U
#define FT_RECORDER_WAV_HEADER_SIZE   44U
#define FT_RECORDER_MIN_FREE_BYTES    (64U * 1024U)

typedef struct
{
    const char *device_name;
} ft_recorder_device_template_t;

static const ft_recorder_device_template_t s_device_templates[FT_RECORDER_DEVICE_COUNT] =
{
    {"mic0"},
    {"amic0"},
};

static struct rt_mutex s_recorder_lock;
static ft_recorder_status_t s_recorder_status;
static rt_thread_t s_recorder_thread;
static bool s_recorder_initialized;
static volatile bool s_stop_requested;

static void recorder_lock(void)
{
    if (s_recorder_initialized)
        (void)rt_mutex_take(&s_recorder_lock, RT_WAITING_FOREVER);
}

static void recorder_unlock(void)
{
    if (s_recorder_initialized)
        (void)rt_mutex_release(&s_recorder_lock);
}

static void recorder_set_error(int error)
{
    recorder_lock();
    s_recorder_status.state = FT_RECORDER_ERROR;
    s_recorder_status.last_error = error != RT_EOK ? error : -RT_ERROR;
    s_recorder_thread = RT_NULL;
    recorder_unlock();
}

static int recorder_query_device(size_t index,
                                 ft_recorder_device_info_t *info,
                                 bool initialize)
{
#ifdef RT_USING_AUDIO
    struct rt_audio_caps caps;
    rt_device_t device;
    int result;

    if (info == RT_NULL || index >= FT_RECORDER_DEVICE_COUNT)
        return -RT_EINVAL;
    rt_memset(info, 0, sizeof(*info));
    info->device_name = s_device_templates[index].device_name;
    device = rt_device_find(info->device_name);
    if (device == RT_NULL || !(device->flag & RT_DEVICE_FLAG_RDONLY))
        return -RT_ENOENT;
    info->registered = true;
    if (initialize && !(device->flag & RT_DEVICE_FLAG_ACTIVATED))
    {
        result = rt_device_init(device);
        if (result != RT_EOK) return result;
    }
    info->ready = (device->flag & RT_DEVICE_FLAG_ACTIVATED) != 0U;
    if (!info->ready) return -RT_EIO;

    rt_memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    result = rt_device_control(device, AUDIO_CTL_GETCAPS, &caps);
    if (result != RT_EOK) return result;
    info->sample_rate = caps.udata.config.samplerate;
    info->channels = caps.udata.config.channels;
    info->sample_bits = caps.udata.config.samplebits;
    return RT_EOK;
#else
    if (info != RT_NULL)
    {
        rt_memset(info, 0, sizeof(*info));
        if (index < FT_RECORDER_DEVICE_COUNT)
            info->device_name = s_device_templates[index].device_name;
    }
    RT_UNUSED(initialize);
    return -RT_ENOSYS;
#endif
}

#ifdef RT_USING_DFS
static void recorder_put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void recorder_put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static void recorder_make_wav_header(uint8_t header[FT_RECORDER_WAV_HEADER_SIZE],
                                     uint32_t data_bytes, uint32_t sample_rate,
                                     uint8_t channels, uint8_t sample_bits)
{
    uint16_t block_align = (uint16_t)(channels * (sample_bits / 8U));
    uint32_t byte_rate = sample_rate * block_align;

    rt_memset(header, 0, FT_RECORDER_WAV_HEADER_SIZE);
    rt_memcpy(header + 0U, "RIFF", 4U);
    recorder_put_u32(header + 4U, data_bytes + 36U);
    rt_memcpy(header + 8U, "WAVE", 4U);
    rt_memcpy(header + 12U, "fmt ", 4U);
    recorder_put_u32(header + 16U, 16U);
    recorder_put_u16(header + 20U, 1U);
    recorder_put_u16(header + 22U, channels);
    recorder_put_u32(header + 24U, sample_rate);
    recorder_put_u32(header + 28U, byte_rate);
    recorder_put_u16(header + 32U, block_align);
    recorder_put_u16(header + 34U, sample_bits);
    rt_memcpy(header + 36U, "data", 4U);
    recorder_put_u32(header + 40U, data_bytes);
}

static uint32_t recorder_get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

static int recorder_validate_wav(const char *path, uint64_t expected_data_bytes,
                                 uint64_t *file_bytes)
{
    uint8_t header[FT_RECORDER_WAV_HEADER_SIZE];
    struct stat status;
    int descriptor;
    int result = -RT_EIO;

    if (path == RT_NULL || path[0] == '\0' || expected_data_bytes > UINT32_MAX)
        return -RT_EINVAL;
    if (stat(path, &status) != 0 || status.st_size < FT_RECORDER_WAV_HEADER_SIZE)
        return -RT_EIO;
    descriptor = open(path, O_RDONLY | O_BINARY, 0);
    if (descriptor < 0) return -RT_EIO;
    if (read(descriptor, header, sizeof(header)) == (int)sizeof(header) &&
        rt_memcmp(header + 0U, "RIFF", 4U) == 0 &&
        rt_memcmp(header + 8U, "WAVE", 4U) == 0 &&
        rt_memcmp(header + 12U, "fmt ", 4U) == 0 &&
        rt_memcmp(header + 36U, "data", 4U) == 0 &&
        recorder_get_u32(header + 4U) == (uint32_t)expected_data_bytes + 36U &&
        recorder_get_u32(header + 40U) == (uint32_t)expected_data_bytes &&
        (uint64_t)status.st_size == expected_data_bytes + sizeof(header))
        result = RT_EOK;
    (void)close(descriptor);
    if (file_bytes != RT_NULL) *file_bytes = (uint64_t)status.st_size;
    return result;
}

static int recorder_write_all(int descriptor, const uint8_t *buffer,
                              size_t length)
{
    size_t offset = 0U;
    while (offset < length)
    {
        int written = write(descriptor, buffer + offset, length - offset);
        if (written <= 0) return -RT_EIO;
        offset += (size_t)written;
    }
    return RT_EOK;
}

static int recorder_ensure_directory(const char *path)
{
    struct stat status;
    if (stat(path, &status) == 0)
        return S_ISDIR(status.st_mode) ? RT_EOK : -RT_EINVAL;
    if (mkdir(path, 0777) == 0) return RT_EOK;
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode) ?
           RT_EOK : -RT_EIO;
}

static int recorder_choose_path(char *mount, size_t mount_size,
                                char *path, size_t path_size)
{
    const char *candidates[] =
    {
        FT_STORAGE_SD_MOUNT_PATH,
        FT_STORAGE_FLASH_MOUNT_PATH,
    };
    ft_storage_volume_info_t volume;
    char directory[FT_RECORDER_PATH_MAX];
    struct stat status;
    uint32_t serial = rt_tick_get_millisecond();
    size_t candidate;
    unsigned suffix;

    for (candidate = 0U; candidate < sizeof(candidates) / sizeof(candidates[0]);
         candidate++)
    {
        if (ft_storage_get_volume(candidates[candidate], &volume) != RT_EOK ||
            !volume.mounted ||
            (volume.free_bytes != 0U &&
             volume.free_bytes < FT_RECORDER_MIN_FREE_BYTES))
            continue;
        if (rt_snprintf(directory, sizeof(directory), "%s/Recordings",
                        candidates[candidate]) >= (int)sizeof(directory))
            continue;
        if (recorder_ensure_directory(directory) != RT_EOK)
            continue;
        for (suffix = 0U; suffix < 100U; suffix++)
        {
            int length = rt_snprintf(path, path_size,
                                     "%s/REC_%010lu_%02u.wav", directory,
                                     (unsigned long)serial, suffix);
            if (length < 0 || (size_t)length >= path_size)
                return -RT_EFULL;
            if (stat(path, &status) != 0)
            {
                rt_strncpy(mount, candidates[candidate], mount_size - 1U);
                mount[mount_size - 1U] = '\0';
                return RT_EOK;
            }
        }
    }
    return -RT_ENOSPC;
}

static uint32_t recorder_peak(const uint8_t *buffer, size_t bytes,
                              uint8_t sample_bits)
{
    uint32_t peak = 0U;
    size_t index;
    if (sample_bits != 16U) return 0U;
    for (index = 0U; index + 1U < bytes; index += 2U)
    {
        int16_t sample = (int16_t)((uint16_t)buffer[index] |
                                   ((uint16_t)buffer[index + 1U] << 8U));
        uint32_t magnitude = sample < 0 ? (uint32_t)(-(int32_t)sample) :
                                         (uint32_t)sample;
        if (magnitude > peak) peak = magnitude;
    }
    if (peak > 32767U) peak = 32767U;
    return peak * 1000U / 32767U;
}
#endif /* RT_USING_DFS */

static void recorder_worker(void *parameter)
{
#if defined(RT_USING_AUDIO) && defined(RT_USING_DFS)
    size_t device_index = (size_t)(uintptr_t)parameter;
    ft_recorder_device_info_t info;
    uint8_t *buffer = RT_NULL;
    uint8_t header[FT_RECORDER_WAV_HEADER_SIZE];
    char path[FT_RECORDER_PATH_MAX] = {0};
    char mount[16] = {0};
    rt_device_t device = RT_NULL;
    uint64_t data_bytes = 0U;
    uint32_t bytes_per_second;
    int descriptor = -1;
    int result;

    result = recorder_query_device(device_index, &info, true);
    if (result != RT_EOK) goto failed;
    result = recorder_choose_path(mount, sizeof(mount), path, sizeof(path));
    if (result != RT_EOK) goto failed;
    buffer = rt_malloc(FT_RECORDER_BUFFER_SIZE);
    if (buffer == RT_NULL) { result = -RT_ENOMEM; goto failed; }
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (descriptor < 0) { result = -RT_EIO; goto failed; }
    recorder_make_wav_header(header, 0U, info.sample_rate,
                             info.channels, info.sample_bits);
    result = recorder_write_all(descriptor, header, sizeof(header));
    if (result != RT_EOK) goto failed;
    device = rt_device_find(info.device_name);
    if (device == RT_NULL) { result = -RT_ENOENT; goto failed; }
    result = rt_device_open(device, RT_DEVICE_OFLAG_RDONLY);
    if (result != RT_EOK) goto failed;

    bytes_per_second = info.sample_rate * info.channels *
                       (info.sample_bits / 8U);
    recorder_lock();
    s_recorder_status.state = FT_RECORDER_RECORDING;
    s_recorder_status.sample_rate = info.sample_rate;
    s_recorder_status.channels = info.channels;
    s_recorder_status.sample_bits = info.sample_bits;
    s_recorder_status.last_error = RT_EOK;
    rt_strncpy(s_recorder_status.storage_mount, mount,
               sizeof(s_recorder_status.storage_mount) - 1U);
    rt_strncpy(s_recorder_status.file_path, path,
               sizeof(s_recorder_status.file_path) - 1U);
    recorder_unlock();

    while (!s_stop_requested)
    {
        rt_ssize_t count = rt_device_read(device, 0, buffer,
                                          FT_RECORDER_BUFFER_SIZE);
        uint32_t peak;
        if (count < 0) { result = (int)count; goto failed; }
        if (count == 0) continue;
        if (data_bytes + (uint64_t)count > UINT32_MAX - 36U)
        {
            result = -RT_EFULL;
            goto failed;
        }
        result = recorder_write_all(descriptor, buffer, (size_t)count);
        if (result != RT_EOK) goto failed;
        data_bytes += (uint64_t)count;
        peak = recorder_peak(buffer, (size_t)count, info.sample_bits);
        recorder_lock();
        s_recorder_status.data_bytes = data_bytes;
        s_recorder_status.duration_ms = bytes_per_second == 0U ? 0U :
            (uint32_t)(data_bytes * 1000U / bytes_per_second);
        s_recorder_status.peak_per_mille = peak;
        if (peak > s_recorder_status.peak_max_per_mille)
            s_recorder_status.peak_max_per_mille = peak;
        recorder_unlock();
    }

    recorder_lock();
    s_recorder_status.state = FT_RECORDER_STOPPING;
    recorder_unlock();
    (void)rt_device_close(device);
    device = RT_NULL;
    recorder_make_wav_header(header, (uint32_t)data_bytes, info.sample_rate,
                             info.channels, info.sample_bits);
    if (lseek(descriptor, 0, SEEK_SET) < 0)
    {
        result = -RT_EIO;
        goto failed;
    }
    result = recorder_write_all(descriptor, header, sizeof(header));
    if (result != RT_EOK || fsync(descriptor) != 0)
    {
        result = -RT_EIO;
        goto failed;
    }
    (void)close(descriptor);
    descriptor = -1;
    rt_free(buffer);
    buffer = RT_NULL;
    recorder_lock();
    s_recorder_status.state = FT_RECORDER_SAVED;
    s_recorder_status.data_bytes = data_bytes;
    s_recorder_status.duration_ms = bytes_per_second == 0U ? 0U :
        (uint32_t)(data_bytes * 1000U / bytes_per_second);
    s_recorder_status.peak_per_mille = 0U;
    s_recorder_status.last_error = RT_EOK;
    s_recorder_thread = RT_NULL;
    recorder_unlock();
    return;

failed:
    if (device != RT_NULL) (void)rt_device_close(device);
    if (descriptor >= 0) (void)close(descriptor);
    if (path[0] != '\0') (void)unlink(path);
    if (buffer != RT_NULL) rt_free(buffer);
    recorder_set_error(result);
#else
    RT_UNUSED(parameter);
    recorder_set_error(-RT_ENOSYS);
#endif
}

int ft_recorder_get_devices(ft_recorder_device_info_t *devices,
                            size_t capacity, size_t *count)
{
    size_t index;
    if (count != RT_NULL) *count = FT_RECORDER_DEVICE_COUNT;
    if (devices == RT_NULL && capacity != 0U) return -RT_EINVAL;
    if (capacity > FT_RECORDER_DEVICE_COUNT)
        capacity = FT_RECORDER_DEVICE_COUNT;
    for (index = 0U; index < capacity; index++)
        (void)recorder_query_device(index, &devices[index], false);
    return RT_EOK;
}

int ft_recorder_select_device(size_t index)
{
    ft_recorder_device_info_t info;
    ft_recorder_state_t state;
    int result;
    if (index >= FT_RECORDER_DEVICE_COUNT) return -RT_EINVAL;
    result = recorder_query_device(index, &info, true);
    if (result != RT_EOK) return result;
    recorder_lock();
    state = s_recorder_status.state;
    if (state == FT_RECORDER_STARTING || state == FT_RECORDER_RECORDING ||
        state == FT_RECORDER_STOPPING)
    {
        recorder_unlock();
        return -RT_EBUSY;
    }
    s_recorder_status.selected_device = index;
    s_recorder_status.sample_rate = info.sample_rate;
    s_recorder_status.channels = info.channels;
    s_recorder_status.sample_bits = info.sample_bits;
    recorder_unlock();
    return RT_EOK;
}

bool ft_recorder_can_start(void)
{
    ft_recorder_device_info_t info;
    ft_storage_volume_info_t volume;
    size_t index;
    ft_recorder_state_t state;
    recorder_lock();
    state = s_recorder_status.state;
    index = s_recorder_status.selected_device;
    recorder_unlock();
    if (state == FT_RECORDER_STARTING || state == FT_RECORDER_RECORDING ||
        state == FT_RECORDER_STOPPING)
        return false;
    if (recorder_query_device(index, &info, false) != RT_EOK || !info.ready)
        return false;
    if (ft_storage_get_volume(FT_STORAGE_SD_MOUNT_PATH, &volume) == RT_EOK &&
        volume.mounted &&
        (volume.free_bytes == 0U || volume.free_bytes >= FT_RECORDER_MIN_FREE_BYTES))
        return true;
    return ft_storage_get_volume(FT_STORAGE_FLASH_MOUNT_PATH, &volume) == RT_EOK &&
           volume.mounted &&
           (volume.free_bytes == 0U || volume.free_bytes >= FT_RECORDER_MIN_FREE_BYTES);
}

int ft_recorder_start(void)
{
    ft_recorder_device_info_t info;
    ft_recorder_state_t state;
    size_t index;
    int result;

    recorder_lock();
    state = s_recorder_status.state;
    index = s_recorder_status.selected_device;
    recorder_unlock();
    if (state == FT_RECORDER_STARTING || state == FT_RECORDER_RECORDING ||
        state == FT_RECORDER_STOPPING)
        return -RT_EBUSY;
    result = recorder_query_device(index, &info, true);
    if (result != RT_EOK) return result;
    if (!ft_recorder_can_start()) return -RT_ENOSPC;

    recorder_lock();
    s_stop_requested = false;
    s_recorder_status.state = FT_RECORDER_STARTING;
    s_recorder_status.duration_ms = 0U;
    s_recorder_status.peak_per_mille = 0U;
    s_recorder_status.peak_max_per_mille = 0U;
    s_recorder_status.data_bytes = 0U;
    s_recorder_status.last_error = RT_EOK;
    s_recorder_status.storage_mount[0] = '\0';
    s_recorder_status.file_path[0] = '\0';
    s_recorder_thread = rt_thread_create("ft_rec", recorder_worker,
                                         (void *)(uintptr_t)index,
                                         FT_RECORDER_THREAD_STACK,
                                         FT_RECORDER_THREAD_PRIORITY,
                                         FT_RECORDER_THREAD_TIMESLICE);
    if (s_recorder_thread == RT_NULL)
    {
        s_recorder_status.state = FT_RECORDER_ERROR;
        s_recorder_status.last_error = -RT_ENOMEM;
        recorder_unlock();
        return -RT_ENOMEM;
    }
    recorder_unlock();
    result = rt_thread_startup(s_recorder_thread);
    if (result != RT_EOK)
    {
        (void)rt_thread_delete(s_recorder_thread);
        recorder_set_error(result);
    }
    return result;
}

int ft_recorder_stop(void)
{
    ft_recorder_state_t state;
    recorder_lock();
    state = s_recorder_status.state;
    if (state != FT_RECORDER_STARTING && state != FT_RECORDER_RECORDING)
    {
        recorder_unlock();
        return -RT_EINVAL;
    }
    s_stop_requested = true;
    s_recorder_status.state = FT_RECORDER_STOPPING;
    recorder_unlock();
    return RT_EOK;
}

int ft_recorder_get_status(ft_recorder_status_t *status)
{
    if (status == RT_NULL) return -RT_EINVAL;
    recorder_lock();
    *status = s_recorder_status;
    recorder_unlock();
    return RT_EOK;
}

static int feathertalk_recorder_init(void)
{
    ft_recorder_device_info_t info;
    if (rt_mutex_init(&s_recorder_lock, "ft_rec", RT_IPC_FLAG_PRIO) != RT_EOK)
        return -RT_ERROR;
    rt_memset(&s_recorder_status, 0, sizeof(s_recorder_status));
    s_recorder_status.state = FT_RECORDER_IDLE;
    s_recorder_status.selected_device = 0U;
    s_recorder_initialized = true;
    if (recorder_query_device(0U, &info, true) == RT_EOK)
    {
        s_recorder_status.sample_rate = info.sample_rate;
        s_recorder_status.channels = info.channels;
        s_recorder_status.sample_bits = info.sample_bits;
    }
    return RT_EOK;
}
INIT_APP_EXPORT(feathertalk_recorder_init);

#ifdef RT_USING_MSH
static int feather_recorder_status(int argc, char **argv)
{
    ft_recorder_status_t status;
    uint64_t file_bytes = 0U;
    int wav_result = -RT_EINVAL;
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    (void)ft_recorder_get_status(&status);
    #ifdef RT_USING_DFS
    if (status.state == FT_RECORDER_SAVED)
        wav_result = recorder_validate_wav(status.file_path,
                                           status.data_bytes, &file_bytes);
    #endif
    rt_kprintf("recorder state=%d device=%u %luHz/%uch/%ubit duration=%lums bytes=%lu peak=%lu/1000 maxpeak=%lu/1000 error=%d\n",
               status.state, (unsigned)status.selected_device,
               (unsigned long)status.sample_rate, status.channels,
               status.sample_bits, (unsigned long)status.duration_ms,
               (unsigned long)status.data_bytes,
               (unsigned long)status.peak_per_mille,
               (unsigned long)status.peak_max_per_mille, status.last_error);
    rt_kprintf("storage=%s file=%s wav=%s file_bytes=%lu\n",
               status.storage_mount[0] != '\0' ? status.storage_mount : "-",
               status.file_path[0] != '\0' ? status.file_path : "-",
               wav_result == RT_EOK ? "valid" :
                   (status.state == FT_RECORDER_SAVED ? "invalid" : "-"),
               (unsigned long)file_bytes);
    return RT_EOK;
}
MSH_CMD_EXPORT(feather_recorder_status, Show FeatherTalk recorder state.);

static int feather_record(int argc, char **argv)
{
    ft_recorder_status_t status;
    unsigned seconds = 2U;
    uint32_t wait_start;
    int result;
    if (argc > 1)
    {
        long value = strtol(argv[1], RT_NULL, 10);
        if (value < 1 || value > 300)
        {
            rt_kprintf("usage: feather_record [1..300 seconds]\n");
            return -RT_EINVAL;
        }
        seconds = (unsigned)value;
    }
    result = ft_recorder_start();
    if (result != RT_EOK)
    {
        rt_kprintf("record start failed: %d\n", result);
        return result;
    }
    wait_start = rt_tick_get_millisecond();
    do
    {
        rt_thread_mdelay(20U);
        (void)ft_recorder_get_status(&status);
    } while (status.state == FT_RECORDER_STARTING &&
             rt_tick_get_millisecond() - wait_start < 5000U);
    if (status.state != FT_RECORDER_RECORDING)
    {
        rt_kprintf("record device did not start: state=%d error=%d\n",
                   status.state, status.last_error);
        return status.last_error != RT_EOK ? status.last_error : -RT_ETIMEOUT;
    }
    rt_thread_mdelay(seconds * 1000U);
    result = ft_recorder_stop();
    if (result != RT_EOK) return result;
    wait_start = rt_tick_get_millisecond();
    do
    {
        rt_thread_mdelay(20U);
        (void)ft_recorder_get_status(&status);
    } while ((status.state == FT_RECORDER_STOPPING ||
              status.state == FT_RECORDER_RECORDING ||
              status.state == FT_RECORDER_STARTING) &&
             rt_tick_get_millisecond() - wait_start < 5000U);
    (void)feather_recorder_status(0, RT_NULL);
    return status.state == FT_RECORDER_SAVED ? RT_EOK :
           (status.last_error != RT_EOK ? status.last_error : -RT_ETIMEOUT);
}
MSH_CMD_EXPORT(feather_record, Record selected input to a WAV file for N seconds.);
#endif
