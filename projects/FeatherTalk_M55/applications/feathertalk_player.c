#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#include "feathertalk_audio.h"
#include "feathertalk_player.h"
#include "feathertalk_storage.h"

#if defined(RT_USING_AUDIO) && defined(RT_USING_DFS)
#include <drivers/audio.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "third_party/minimp3/minimp3.h"
#endif

#define FT_PLAYER_DEVICE          "sound0"
#define FT_PLAYER_BUFFER_SIZE     4096U
#define FT_PLAYER_MP3_INPUT_SIZE  16384U
#define FT_PLAYER_MP3_OUTPUT_SAMPLES 6144U
/* minimp3's Layer-III synthesis keeps several kilobytes of temporary spectral
 * data on the caller stack. Keep this worker isolated from the UI and give it
 * measured headroom; the old 16 KiB stack reached/corrupted its guard on M55. */
#define FT_PLAYER_THREAD_STACK    32768U
#define FT_PLAYER_THREAD_PRIORITY 13U
#define FT_PLAYER_EVENT_WAKE      (1UL << 0)
#define FT_PLAYER_DEFAULT_DIRECTORY FT_STORAGE_SD_MOUNT_PATH "/Music"

typedef enum
{
    FT_PLAYER_COMMAND_NONE = 0,
    FT_PLAYER_COMMAND_PLAY,
    FT_PLAYER_COMMAND_PAUSE,
    FT_PLAYER_COMMAND_RESUME,
    FT_PLAYER_COMMAND_STOP
} ft_player_command_t;

typedef struct
{
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t sample_rate;
    uint8_t sample_bits;
    uint8_t channels;
} ft_wav_info_t;

typedef struct
{
    uint8_t io[FT_PLAYER_MP3_INPUT_SIZE];
    mp3d_sample_t decoded[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t output[FT_PLAYER_MP3_OUTPUT_SAMPLES];
} ft_player_workspace_t;

typedef struct
{
    int descriptor;
    rt_device_t device;
    ft_player_codec_t codec;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t streamed_bytes;
    uint64_t decoded_frames;
    uint32_t input_sample_rate;
    uint32_t output_sample_rate;
    uint8_t channels;
    uint8_t sample_bits;
    size_t input_offset;
    size_t input_bytes;
    bool input_eof;
    mp3dec_t mp3;
} ft_player_stream_t;

static struct rt_mutex s_player_lock;
static struct rt_event s_player_event;
static ft_player_track_t s_tracks[FT_PLAYER_MAX_TRACKS];
static ft_player_status_t s_status;
static volatile ft_player_command_t s_command;
static volatile size_t s_requested_track;
static rt_thread_t s_player_thread;
static bool s_initialized;
static char s_directory[FT_PLAYER_PATH_MAX] = FT_PLAYER_DEFAULT_DIRECTORY;

static void player_lock(void)
{
    if (s_initialized)
        (void)rt_mutex_take(&s_player_lock, RT_WAITING_FOREVER);
}

static void player_unlock(void)
{
    if (s_initialized) (void)rt_mutex_release(&s_player_lock);
}

#if defined(RT_USING_AUDIO) && defined(RT_USING_DFS)
static uint16_t player_u16(const uint8_t *value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8U);
}

static uint32_t player_u32(const uint8_t *value)
{
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) | ((uint32_t)value[3] << 24U);
}

static int player_read_exact(int descriptor, void *buffer, size_t bytes)
{
    uint8_t *destination = (uint8_t *)buffer;
    size_t offset = 0U;
    while (offset < bytes)
    {
        int count = read(descriptor, destination + offset, bytes - offset);
        if (count <= 0) return -RT_EIO;
        offset += (size_t)count;
    }
    return RT_EOK;
}

static int player_parse_wav(const char *path, ft_wav_info_t *info,
                            uint64_t *file_bytes)
{
    uint8_t riff[12];
    uint8_t chunk[8];
    uint8_t format[40];
    struct stat status;
    uint32_t offset = 12U;
    uint16_t encoding = 0U;
    bool have_format = false;
    bool have_data = false;
    int descriptor;

    if (path == RT_NULL || info == RT_NULL || stat(path, &status) != 0 ||
        status.st_size < 44 || (uint64_t)status.st_size > UINT32_MAX)
        return -RT_EINVAL;
    descriptor = open(path, O_RDONLY | O_BINARY, 0);
    if (descriptor < 0) return -RT_EIO;
    rt_memset(info, 0, sizeof(*info));
    if (player_read_exact(descriptor, riff, sizeof(riff)) != RT_EOK ||
        rt_memcmp(riff, "RIFF", 4U) != 0 ||
        rt_memcmp(riff + 8U, "WAVE", 4U) != 0)
        goto invalid;

    while (offset + sizeof(chunk) <= (uint32_t)status.st_size)
    {
        uint32_t chunk_bytes;
        uint32_t payload;
        if (lseek(descriptor, (off_t)offset, SEEK_SET) < 0 ||
            player_read_exact(descriptor, chunk, sizeof(chunk)) != RT_EOK)
            goto invalid;
        chunk_bytes = player_u32(chunk + 4U);
        payload = offset + 8U;
        if (chunk_bytes > (uint32_t)status.st_size - payload) goto invalid;
        if (rt_memcmp(chunk, "fmt ", 4U) == 0)
        {
            size_t read_bytes = chunk_bytes < sizeof(format) ?
                                chunk_bytes : sizeof(format);
            if (chunk_bytes < 16U ||
                player_read_exact(descriptor, format, read_bytes) != RT_EOK)
                goto invalid;
            encoding = player_u16(format);
            info->channels = (uint8_t)player_u16(format + 2U);
            info->sample_rate = player_u32(format + 4U);
            info->sample_bits = (uint8_t)player_u16(format + 14U);
            have_format = true;
        }
        else if (rt_memcmp(chunk, "data", 4U) == 0)
        {
            info->data_offset = payload;
            info->data_bytes = chunk_bytes;
            have_data = true;
        }
        if (have_format && have_data) break;
        offset = payload + chunk_bytes + (chunk_bytes & 1U);
    }
    (void)close(descriptor);
    if (!have_format || !have_data || encoding != 1U ||
        info->data_bytes == 0U ||
        !ft_audio_output_format_supported(info->sample_rate,
                                          info->sample_bits,
                                          info->channels))
        return -RT_EINVAL;
    if (file_bytes != RT_NULL) *file_bytes = (uint64_t)status.st_size;
    return RT_EOK;

invalid:
    (void)close(descriptor);
    return -RT_EINVAL;
}

static bool player_has_extension(const char *name, const char *extension)
{
    size_t length;
    size_t extension_length;
    size_t index;
    if (name == RT_NULL || extension == RT_NULL) return false;
    length = strlen(name);
    extension_length = strlen(extension);
    if (length < extension_length) return false;
    name += length - extension_length;
    for (index = 0U; index < extension_length; index++)
    {
        char left = name[index];
        char right = extension[index];
        if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
        if (left != right) return false;
    }
    return true;
}

typedef struct
{
    const char *directory;
} ft_player_scan_context_t;

static int player_probe_mp3(const char *path, ft_player_track_t *track)
{
    uint8_t *buffer;
    struct stat status;
    size_t bytes;
    size_t offset;
    int descriptor;
    int result = -RT_EINVAL;
    static const uint16_t bitrate_mpeg1[16] =
        {0U, 32U, 40U, 48U, 56U, 64U, 80U, 96U,
         112U, 128U, 160U, 192U, 224U, 256U, 320U, 0U};
    static const uint16_t bitrate_mpeg2[16] =
        {0U, 8U, 16U, 24U, 32U, 40U, 48U, 56U,
         64U, 80U, 96U, 112U, 128U, 144U, 160U, 0U};
    static const uint32_t sample_rates[3] = {44100U, 48000U, 32000U};

    if (path == RT_NULL || track == RT_NULL || stat(path, &status) != 0 ||
        status.st_size <= 0 || (uint64_t)status.st_size > UINT32_MAX)
        return -RT_EINVAL;
    buffer = rt_malloc(FT_PLAYER_MP3_INPUT_SIZE);
    if (buffer == RT_NULL) return -RT_ENOMEM;
    descriptor = open(path, O_RDONLY | O_BINARY, 0);
    if (descriptor < 0)
    {
        rt_free(buffer);
        return -RT_EIO;
    }
    {
        int count = read(descriptor, buffer, FT_PLAYER_MP3_INPUT_SIZE);
        bytes = count > 0 ? (size_t)count : 0U;
    }
    for (offset = 0U; offset + 4U <= bytes; offset++)
    {
        uint32_t header = ((uint32_t)buffer[offset] << 24U) |
                          ((uint32_t)buffer[offset + 1U] << 16U) |
                          ((uint32_t)buffer[offset + 2U] << 8U) |
                          buffer[offset + 3U];
        uint32_t version = (header >> 19U) & 3U;
        uint32_t layer = (header >> 17U) & 3U;
        uint32_t bitrate_index = (header >> 12U) & 15U;
        uint32_t rate_index = (header >> 10U) & 3U;
        uint32_t bitrate;
        uint32_t rate;
        if ((header & 0xFFE00000UL) != 0xFFE00000UL || version == 1U ||
            layer != 1U || bitrate_index == 0U || bitrate_index == 15U ||
            rate_index == 3U)
            continue;
        bitrate = version == 3U ? bitrate_mpeg1[bitrate_index] :
                                  bitrate_mpeg2[bitrate_index];
        rate = sample_rates[rate_index];
        if (version == 2U) rate /= 2U;
        else if (version == 0U) rate /= 4U;
        track->file_bytes = (uint64_t)status.st_size;
        track->data_bytes = (uint32_t)status.st_size;
        track->sample_rate = rate;
        track->sample_bits = 16U;
        track->channels = ((header >> 6U) & 3U) == 3U ? 1U : 2U;
        track->codec = FT_PLAYER_CODEC_MP3;
        track->duration_ms = (uint32_t)((uint64_t)status.st_size * 8ULL /
                                       bitrate);
        result = RT_EOK;
        break;
    }
    close(descriptor);
    rt_free(buffer);
    return result;
}

static bool player_scan_entry(const ft_storage_entry_t *entry, void *context)
{
    ft_player_scan_context_t *scan = (ft_player_scan_context_t *)context;
    ft_player_track_t *track;
    ft_wav_info_t wav;
    const char *name;
    size_t name_length;
    size_t extension_length;
    uint32_t bytes_per_second;
    bool wav_file;
    bool mp3_file;

    if (entry == RT_NULL || scan == RT_NULL ||
        entry->type != FT_STORAGE_ENTRY_FILE ||
        s_status.track_count >= FT_PLAYER_MAX_TRACKS)
        return true;
    wav_file = player_has_extension(entry->name, ".wav");
    mp3_file = player_has_extension(entry->name, ".mp3");
    if (!wav_file && !mp3_file) return true;
    track = &s_tracks[s_status.track_count];
    rt_memset(track, 0, sizeof(*track));
    if (ft_storage_join_path(scan->directory, entry->name, track->path,
                             sizeof(track->path)) != RT_EOK)
        return true;
    if (wav_file)
    {
        if (player_parse_wav(track->path, &wav, &track->file_bytes) != RT_EOK)
            return true;
        track->data_bytes = wav.data_bytes;
        track->sample_rate = wav.sample_rate;
        track->sample_bits = wav.sample_bits;
        track->channels = wav.channels;
        track->codec = FT_PLAYER_CODEC_PCM_WAV;
        bytes_per_second = wav.sample_rate * wav.channels *
                           (wav.sample_bits / 8U);
        track->duration_ms = bytes_per_second == 0U ? 0U :
            (uint32_t)((uint64_t)wav.data_bytes * 1000U / bytes_per_second);
    }
    else if (player_probe_mp3(track->path, track) != RT_EOK)
        return true;
    name = entry->name;
    name_length = strlen(name);
    extension_length = 4U;
    if (name_length >= extension_length) name_length -= extension_length;
    if (name_length >= sizeof(track->name))
        name_length = sizeof(track->name) - 1U;
    rt_memcpy(track->name, name, name_length);
    track->name[name_length] = '\0';
    track->recording = strstr(scan->directory, "/Recordings") != RT_NULL;
    s_status.track_count++;
    return s_status.track_count < FT_PLAYER_MAX_TRACKS;
}

static void player_ensure_directory(const char *path)
{
    struct stat status;
    if (path == RT_NULL || stat(path, &status) == 0) return;
    (void)mkdir(path, 0777);
}

static int player_track_compare(const void *left_pointer,
                                const void *right_pointer)
{
    const ft_player_track_t *left =
        (const ft_player_track_t *)left_pointer;
    const ft_player_track_t *right =
        (const ft_player_track_t *)right_pointer;
    /* Recorder output sorts newest first. User-selected folders retain the
     * familiar case-sensitive ascending filesystem name order. */
    return left->recording ? strcmp(right->name, left->name) :
                             strcmp(left->name, right->name);
}

static void player_stream_reset(ft_player_stream_t *stream)
{
    rt_memset(stream, 0, sizeof(*stream));
    stream->descriptor = -1;
}

static void player_close_stream(ft_player_stream_t *stream)
{
    if (stream == RT_NULL) return;
    if (stream->device != RT_NULL)
    {
        (void)rt_device_close(stream->device);
        stream->device = RT_NULL;
    }
    if (stream->descriptor >= 0)
    {
        (void)close(stream->descriptor);
        stream->descriptor = -1;
    }
    ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_LOCAL_PLAYER);
    player_stream_reset(stream);
}

static uint32_t player_output_rate(uint32_t input_rate)
{
    if (ft_audio_output_format_supported(input_rate, 16U, 2U))
        return input_rate;
    if (input_rate <= 16000U) return 16000U;
    if (input_rate <= 24000U) return 24000U;
    if (input_rate <= 48000U) return 48000U;
    return 96000U;
}

static int player_open_stream(size_t index, ft_player_stream_t *stream)
{
    ft_player_track_t track;
    ft_wav_info_t wav;
    uint32_t output_rate;
    uint8_t output_bits;
    int result;

    result = ft_player_get_track(index, &track);
    if (result != RT_EOK) return result;
    player_stream_reset(stream);
    stream->codec = track.codec;
    stream->input_sample_rate = track.sample_rate;
    stream->channels = track.channels;
    stream->sample_bits = track.sample_bits;
    stream->data_bytes = track.data_bytes;
    if (track.codec == FT_PLAYER_CODEC_PCM_WAV)
    {
        result = player_parse_wav(track.path, &wav, RT_NULL);
        if (result != RT_EOK) return result;
        stream->data_offset = wav.data_offset;
        stream->data_bytes = wav.data_bytes;
        output_rate = wav.sample_rate;
        output_bits = wav.sample_bits;
    }
    else
    {
        output_rate = player_output_rate(track.sample_rate);
        output_bits = 16U;
        mp3dec_init(&stream->mp3);
    }
    stream->output_sample_rate = output_rate;
    result = ft_audio_claim_output(FT_AUDIO_OUTPUT_OWNER_LOCAL_PLAYER);
    if (result != RT_EOK) return result;
    result = ft_audio_set_output_format(output_rate, output_bits,
                                        track.channels);
    if (result != RT_EOK) goto failed;
    stream->descriptor = open(track.path, O_RDONLY | O_BINARY, 0);
    if (stream->descriptor < 0 ||
        (stream->data_offset != 0U &&
         lseek(stream->descriptor, (off_t)stream->data_offset, SEEK_SET) < 0))
    {
        result = -RT_EIO;
        goto failed;
    }
    stream->device = rt_device_find(FT_PLAYER_DEVICE);
    if (stream->device == RT_NULL ||
        rt_device_open(stream->device, RT_DEVICE_OFLAG_WRONLY) != RT_EOK)
    {
        stream->device = RT_NULL;
        result = -RT_EIO;
        goto failed;
    }
    player_lock();
    s_status.state = FT_PLAYER_PLAYING;
    s_status.current_track = index;
    s_status.position_ms = 0U;
    s_status.duration_ms = track.duration_ms;
    s_status.sample_rate = output_rate;
    s_status.sample_bits = output_bits;
    s_status.channels = track.channels;
    s_status.last_error = RT_EOK;
    s_status.generation++;
    player_unlock();
    return RT_EOK;

failed:
    player_close_stream(stream);
    return result;
}

static void player_set_state(ft_player_state_t state, int error)
{
    player_lock();
    s_status.state = state;
    s_status.last_error = error;
    s_status.generation++;
    player_unlock();
}

static void player_set_explicitly_stopped(void)
{
    player_lock();
    s_status.state = FT_PLAYER_STOPPED;
    s_status.current_track = 0U;
    s_status.position_ms = 0U;
    s_status.duration_ms = 0U;
    s_status.last_error = RT_EOK;
    s_status.generation++;
    player_unlock();
}

static int player_write_wav(ft_player_stream_t *stream,
                            ft_player_workspace_t *workspace)
{
    uint32_t remaining = stream->data_bytes - stream->streamed_bytes;
    uint32_t request = remaining < FT_PLAYER_BUFFER_SIZE ?
                       remaining : FT_PLAYER_BUFFER_SIZE;
    uint32_t frame_bytes = stream->channels * (stream->sample_bits / 8U);
    int count;
    if (request == 0U) return 1;
    request -= request % frame_bytes;
    count = read(stream->descriptor, workspace->io, request);
    if (count <= 0 || (uint32_t)count % frame_bytes != 0U ||
        rt_device_write(stream->device, 0, workspace->io,
                        (size_t)count) != count)
        return -RT_EIO;
    stream->streamed_bytes += (uint32_t)count;
    player_lock();
    s_status.position_ms = (uint32_t)((uint64_t)stream->streamed_bytes *
        1000U / (stream->input_sample_rate * stream->channels *
                 (stream->sample_bits / 8U)));
    player_unlock();
    return RT_EOK;
}

static size_t player_resample_frame(const mp3d_sample_t *input,
                                    size_t input_frames, uint8_t channels,
                                    uint32_t input_rate, uint32_t output_rate,
                                    int16_t *output, size_t output_capacity)
{
    size_t output_frames;
    size_t frame;
    size_t channel;
    if (input == RT_NULL || output == RT_NULL || input_frames == 0U ||
        channels == 0U || input_rate == 0U || output_rate == 0U)
        return 0U;
    output_frames = (size_t)(((uint64_t)input_frames * output_rate +
                              input_rate / 2U) / input_rate);
    if (output_frames * channels > output_capacity)
        output_frames = output_capacity / channels;
    if (input_rate == output_rate)
    {
        rt_memcpy(output, input, output_frames * channels * sizeof(*output));
        return output_frames * channels;
    }
    for (frame = 0U; frame < output_frames; frame++)
    {
        uint64_t position = output_frames <= 1U ? 0U :
            (uint64_t)frame * (input_frames - 1U) * 65536ULL /
            (output_frames - 1U);
        size_t left = (size_t)(position >> 16U);
        size_t right = left + 1U < input_frames ? left + 1U : left;
        uint32_t fraction = (uint32_t)position & 0xFFFFU;
        for (channel = 0U; channel < channels; channel++)
        {
            int32_t first = input[left * channels + channel];
            int32_t second = input[right * channels + channel];
            output[frame * channels + channel] = (int16_t)(
                first + (((second - first) * (int32_t)fraction) >> 16));
        }
    }
    return output_frames * channels;
}

static int player_fill_mp3(ft_player_stream_t *stream,
                           ft_player_workspace_t *workspace)
{
    int count;
    if (stream->input_eof || stream->input_bytes == FT_PLAYER_MP3_INPUT_SIZE)
        return RT_EOK;
    if (stream->input_offset != 0U && stream->input_bytes != 0U)
        memmove(workspace->io, workspace->io + stream->input_offset,
                stream->input_bytes);
    stream->input_offset = 0U;
    count = read(stream->descriptor, workspace->io + stream->input_bytes,
                 FT_PLAYER_MP3_INPUT_SIZE - stream->input_bytes);
    if (count < 0) return -RT_EIO;
    if (count == 0) stream->input_eof = true;
    else stream->input_bytes += (size_t)count;
    return RT_EOK;
}

static int player_write_mp3(ft_player_stream_t *stream,
                            ft_player_workspace_t *workspace)
{
    while (1)
    {
        mp3dec_frame_info_t frame;
        size_t output_samples;
        int samples;
        int result = player_fill_mp3(stream, workspace);
        if (result != RT_EOK) return result;
        if (stream->input_bytes == 0U && stream->input_eof) return 1;
        rt_memset(&frame, 0, sizeof(frame));
        samples = mp3dec_decode_frame(&stream->mp3,
            workspace->io + stream->input_offset, (int)stream->input_bytes,
            workspace->decoded, &frame);
        if (frame.frame_bytes <= 0)
        {
            if (!stream->input_eof &&
                stream->input_bytes < FT_PLAYER_MP3_INPUT_SIZE)
                continue;
            stream->input_offset++;
            stream->input_bytes--;
            continue;
        }
        stream->input_offset += (size_t)frame.frame_bytes;
        stream->input_bytes -= (size_t)frame.frame_bytes;
        stream->streamed_bytes += (uint32_t)frame.frame_bytes;
        if (samples <= 0) continue;
        if (frame.hz <= 0 || frame.channels != stream->channels)
            return -RT_EINVAL;
        output_samples = player_resample_frame(workspace->decoded,
            (size_t)samples, stream->channels, (uint32_t)frame.hz,
            stream->output_sample_rate, workspace->output,
            FT_PLAYER_MP3_OUTPUT_SAMPLES);
        if (output_samples == 0U ||
            rt_device_write(stream->device, 0, workspace->output,
                            output_samples * sizeof(int16_t)) !=
                (rt_ssize_t)(output_samples * sizeof(int16_t)))
            return -RT_EIO;
        stream->decoded_frames += (uint32_t)samples;
        player_lock();
        s_status.position_ms = (uint32_t)(stream->decoded_frames * 1000ULL /
                                          stream->input_sample_rate);
        player_unlock();
        return RT_EOK;
    }
}

static void player_worker(void *parameter)
{
    ft_player_workspace_t *workspace = (ft_player_workspace_t *)parameter;
    ft_player_stream_t stream;
    player_stream_reset(&stream);

    while (1)
    {
        rt_uint32_t received = 0U;
        ft_player_command_t command;
        size_t requested_track;
        int timeout = stream.descriptor >= 0 &&
                      s_status.state == FT_PLAYER_PLAYING ?
                      RT_WAITING_NO : RT_WAITING_FOREVER;
        (void)rt_event_recv(&s_player_event, FT_PLAYER_EVENT_WAKE,
                            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                            timeout, &received);
        player_lock();
        command = s_command;
        requested_track = s_requested_track;
        s_command = FT_PLAYER_COMMAND_NONE;
        player_unlock();

        if (command == FT_PLAYER_COMMAND_STOP)
        {
            player_close_stream(&stream);
            /* Reset the transport after the stream is closed.  A writer may
             * have published one final position between a caller requesting
             * STOP and this worker consuming it, so the earlier optimistic
             * reset alone is not sufficient. */
            player_set_explicitly_stopped();
            continue;
        }
        if (command == FT_PLAYER_COMMAND_PLAY)
        {
            int result;
            player_close_stream(&stream);
            player_set_state(FT_PLAYER_STARTING, RT_EOK);
            result = player_open_stream(requested_track, &stream);
            if (result != RT_EOK)
                player_set_state(FT_PLAYER_ERROR, result);
            continue;
        }
        if (command == FT_PLAYER_COMMAND_PAUSE && stream.descriptor >= 0)
        {
            player_set_state(FT_PLAYER_PAUSED, RT_EOK);
            continue;
        }
        if (command == FT_PLAYER_COMMAND_RESUME && stream.descriptor >= 0)
        {
            player_set_state(FT_PLAYER_PLAYING, RT_EOK);
            continue;
        }

        if (stream.descriptor >= 0 && s_status.state == FT_PLAYER_PLAYING)
        {
            int result = stream.codec == FT_PLAYER_CODEC_MP3 ?
                player_write_mp3(&stream, workspace) :
                player_write_wav(&stream, workspace);
            if (result == 1)
            {
                size_t next;
                bool loop;
                player_lock();
                loop = s_status.folder_loop && s_status.track_count > 0U;
                next = loop ? (s_status.current_track + 1U) %
                              s_status.track_count : 0U;
                player_unlock();
                player_close_stream(&stream);
                if (loop)
                {
                    player_set_state(FT_PLAYER_STARTING, RT_EOK);
                    result = player_open_stream(next, &stream);
                    if (result != RT_EOK)
                        player_set_state(FT_PLAYER_ERROR, result);
                }
                else
                    player_set_state(FT_PLAYER_STOPPED, RT_EOK);
                continue;
            }
            if (result != RT_EOK)
            {
                player_close_stream(&stream);
                player_set_state(FT_PLAYER_ERROR, result);
                continue;
            }
        }
    }
}
#endif

int ft_player_scan(void)
{
#if defined(RT_USING_AUDIO) && defined(RT_USING_DFS)
    ft_player_scan_context_t context;
    char directory[FT_PLAYER_PATH_MAX];
    size_t count;
    ft_player_state_t state;

    player_lock();
    state = s_status.state;
    if (state == FT_PLAYER_PLAYING || state == FT_PLAYER_PAUSED ||
        state == FT_PLAYER_STARTING)
    {
        player_unlock();
        return -RT_EBUSY;
    }
    rt_memset(s_tracks, 0, sizeof(s_tracks));
    s_status.track_count = 0U;
    s_status.current_track = 0U;
    s_status.position_ms = 0U;
    s_status.duration_ms = 0U;
    rt_strncpy(directory, s_directory, sizeof(directory) - 1U);
    directory[sizeof(directory) - 1U] = '\0';
    player_unlock();
    player_ensure_directory(directory);
    context.directory = directory;
    (void)ft_storage_list(directory, FT_STORAGE_ENTRY_FILE,
                          player_scan_entry, &context);
    qsort(s_tracks, s_status.track_count, sizeof(s_tracks[0]),
          player_track_compare);
    player_lock();
    s_status.last_error = RT_EOK;
    s_status.generation++;
    count = s_status.track_count;
    player_unlock();
    return (int)count;
#else
    return -RT_ENOSYS;
#endif
}

static bool player_directory_valid(const char *path)
{
    size_t root_length;
    struct stat status;
    if (path == RT_NULL || path[0] == '\0' || strlen(path) >= FT_PLAYER_PATH_MAX ||
        strstr(path, "/../") != RT_NULL || strstr(path, "//") != RT_NULL)
        return false;
    root_length = strncmp(path, FT_STORAGE_SD_MOUNT_PATH,
                          strlen(FT_STORAGE_SD_MOUNT_PATH)) == 0 ?
                  strlen(FT_STORAGE_SD_MOUNT_PATH) :
                  strncmp(path, FT_STORAGE_FLASH_MOUNT_PATH,
                          strlen(FT_STORAGE_FLASH_MOUNT_PATH)) == 0 ?
                  strlen(FT_STORAGE_FLASH_MOUNT_PATH) : 0U;
    if (root_length == 0U ||
        (path[root_length] != '\0' && path[root_length] != '/'))
        return false;
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

int ft_player_set_directory(const char *path)
{
#if defined(RT_USING_AUDIO) && defined(RT_USING_DFS)
    if (!player_directory_valid(path)) return -RT_EINVAL;
    if (!s_initialized) return -RT_ENOSYS;

    /* Changing the folder is a transport operation, not a setting that should
     * be rejected while audio is active.  Publish STOPPED first so the worker
     * cannot submit another block, replace the playlist root, and wake it to
     * close the old stream.  A following PLAY command is safe even if it wins
     * the event race: PLAY always closes the previous stream before opening
     * the newly scanned track. */
    player_lock();
    s_status.state = FT_PLAYER_STOPPED;
    s_status.position_ms = 0U;
    s_status.duration_ms = 0U;
    s_status.last_error = RT_EOK;
    rt_strncpy(s_directory, path, sizeof(s_directory) - 1U);
    s_directory[sizeof(s_directory) - 1U] = '\0';
    s_requested_track = 0U;
    s_command = FT_PLAYER_COMMAND_STOP;
    player_unlock();
    (void)rt_event_send(&s_player_event, FT_PLAYER_EVENT_WAKE);
    return ft_player_scan() < 0 ? -RT_EIO : RT_EOK;
#else
    RT_UNUSED(path);
    return -RT_ENOSYS;
#endif
}

int ft_player_get_directory(char *path, size_t path_size)
{
    size_t length;
    if (path == RT_NULL || path_size == 0U) return -RT_EINVAL;
    player_lock();
    length = strlen(s_directory);
    if (length + 1U > path_size)
    {
        player_unlock();
        return -RT_EFULL;
    }
    rt_memcpy(path, s_directory, length + 1U);
    player_unlock();
    return RT_EOK;
}

int ft_player_set_folder_loop(bool enabled)
{
    player_lock();
    if (s_status.folder_loop != enabled)
    {
        s_status.folder_loop = enabled;
        s_status.generation++;
    }
    player_unlock();
    return RT_EOK;
}

size_t ft_player_get_track_count(void)
{
    size_t count;
    player_lock();
    count = s_status.track_count;
    player_unlock();
    return count;
}

int ft_player_get_track(size_t index, ft_player_track_t *track)
{
    if (track == RT_NULL) return -RT_EINVAL;
    player_lock();
    if (index >= s_status.track_count)
    {
        player_unlock();
        return -RT_ENOENT;
    }
    *track = s_tracks[index];
    player_unlock();
    return RT_EOK;
}

static int player_request(ft_player_command_t command, size_t index)
{
    if (!s_initialized) return -RT_ENOSYS;
    player_lock();
    if (command == FT_PLAYER_COMMAND_PLAY && index >= s_status.track_count)
    {
        player_unlock();
        return -RT_ENOENT;
    }
    s_requested_track = index;
    s_command = command;
    player_unlock();
    return rt_event_send(&s_player_event, FT_PLAYER_EVENT_WAKE);
}

int ft_player_play(size_t index)
{
    return player_request(FT_PLAYER_COMMAND_PLAY, index);
}

int ft_player_pause(void)
{
    ft_player_status_t status;
    (void)ft_player_get_status(&status);
    return status.state == FT_PLAYER_PLAYING ?
           player_request(FT_PLAYER_COMMAND_PAUSE, status.current_track) :
           -RT_EINVAL;
}

int ft_player_resume(void)
{
    ft_player_status_t status;
    (void)ft_player_get_status(&status);
    return status.state == FT_PLAYER_PAUSED ?
           player_request(FT_PLAYER_COMMAND_RESUME, status.current_track) :
           -RT_EINVAL;
}

int ft_player_stop(void)
{
    return player_request(FT_PLAYER_COMMAND_STOP, 0U);
}

int ft_player_get_status(ft_player_status_t *status)
{
    if (status == RT_NULL) return -RT_EINVAL;
    player_lock();
    *status = s_status;
    player_unlock();
    return RT_EOK;
}

static int feathertalk_player_init(void)
{
#if defined(RT_USING_AUDIO) && defined(RT_USING_DFS)
    ft_player_workspace_t *workspace;
    if (rt_mutex_init(&s_player_lock, "ft_play", RT_IPC_FLAG_PRIO) != RT_EOK ||
        rt_event_init(&s_player_event, "ft_play", RT_IPC_FLAG_FIFO) != RT_EOK)
        return -RT_ERROR;
    s_initialized = true;
    rt_memset(&s_status, 0, sizeof(s_status));
    s_status.state = FT_PLAYER_STOPPED;
    s_status.folder_loop = true;
    workspace = rt_malloc(sizeof(*workspace));
    if (workspace == RT_NULL) return -RT_ENOMEM;
    s_player_thread = rt_thread_create("ft_player", player_worker, workspace,
        FT_PLAYER_THREAD_STACK, FT_PLAYER_THREAD_PRIORITY, 10U);
    if (s_player_thread == RT_NULL)
    {
        rt_free(workspace);
        return -RT_ENOMEM;
    }
    return rt_thread_startup(s_player_thread);
#else
    return -RT_ENOSYS;
#endif
}
INIT_APP_EXPORT(feathertalk_player_init);

#ifdef RT_USING_MSH
static const char *player_state_name(ft_player_state_t state)
{
    static const char *names[] =
        {"stopped", "starting", "playing", "paused", "error"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ?
           names[state] : "unknown";
}

static int feather_player(int argc, char **argv)
{
    ft_player_status_t status;
    size_t index;
    int result = RT_EOK;
    if (argc < 2)
    {
        rt_kprintf("usage: feather_player scan|list|dir [path]|loop <0|1>|status|play <index>|pause|resume|stop\n");
        return -RT_EINVAL;
    }
    if (strcmp(argv[1], "scan") == 0)
    {
        result = ft_player_scan();
        char directory[FT_PLAYER_PATH_MAX];
        (void)ft_player_get_directory(directory, sizeof(directory));
        rt_kprintf("local WAV/MP3 tracks in %s: %d\n", directory, result);
        return result < 0 ? result : RT_EOK;
    }
    if (strcmp(argv[1], "list") == 0)
    {
        for (index = 0U; index < ft_player_get_track_count(); index++)
        {
            ft_player_track_t track;
            if (ft_player_get_track(index, &track) != RT_EOK) continue;
            rt_kprintf("[%u] %s | %lu ms | %lu Hz %u-bit %u-ch | %s\n",
                       (unsigned)index, track.path,
                       (unsigned long)track.duration_ms,
                       (unsigned long)track.sample_rate,
                       track.sample_bits, track.channels,
                       track.codec == FT_PLAYER_CODEC_MP3 ? "MP3" : "PCM WAV");
        }
        return RT_EOK;
    }
    if (strcmp(argv[1], "dir") == 0)
    {
        char directory[FT_PLAYER_PATH_MAX];
        if (argc == 3) result = ft_player_set_directory(argv[2]);
        (void)ft_player_get_directory(directory, sizeof(directory));
        rt_kprintf("player directory: %s result=%d\n", directory, result);
        return result;
    }
    if (strcmp(argv[1], "loop") == 0 && argc == 3)
        result = ft_player_set_folder_loop(strtoul(argv[2], RT_NULL, 10) != 0U);
    if (strcmp(argv[1], "play") == 0 && argc == 3)
        result = ft_player_play((size_t)strtoul(argv[2], RT_NULL, 10));
    else if (strcmp(argv[1], "pause") == 0) result = ft_player_pause();
    else if (strcmp(argv[1], "resume") == 0) result = ft_player_resume();
    else if (strcmp(argv[1], "stop") == 0) result = ft_player_stop();
    else if (strcmp(argv[1], "status") != 0 && strcmp(argv[1], "loop") != 0)
        result = -RT_EINVAL;
    (void)ft_player_get_status(&status);
    rt_kprintf("player %s track=%u/%u position=%lu/%lu ms format=%lu/%u/%u loop=%u owner=%u error=%d result=%d\n",
               player_state_name(status.state), (unsigned)status.current_track,
               (unsigned)status.track_count,
               (unsigned long)status.position_ms,
               (unsigned long)status.duration_ms,
               (unsigned long)status.sample_rate, status.sample_bits,
               status.channels, status.folder_loop ? 1U : 0U,
               (unsigned)ft_audio_get_output_owner(),
               status.last_error, result);
    return result;
}
MSH_CMD_EXPORT(feather_player, Scan and control local WAV and MP3 playback.);
#endif
