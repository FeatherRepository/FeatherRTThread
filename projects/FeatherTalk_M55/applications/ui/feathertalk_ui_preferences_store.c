#include <rtthread.h>

#include <limits.h>
#include <string.h>

#include "board_storage.h"
#include "feathertalk_ui_preferences_store.h"

#ifdef RT_USING_DFS
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#define FT_PREF_STORE_DIRECTORY       "/flash/.feathertalk"
#define FT_PREF_STORE_SLOT_A_PATH     FT_PREF_STORE_DIRECTORY "/ui-config-a.bin"
#define FT_PREF_STORE_SLOT_B_PATH     FT_PREF_STORE_DIRECTORY "/ui-config-b.bin"

/* On disk the magic bytes are "FTPC". No native C structure is persisted. */
#define FT_PREF_STORE_MAGIC           0x43505446UL
#define FT_PREF_STORE_SCHEMA          1U
#define FT_PREF_STORE_HEADER_SIZE     24U
#define FT_PREF_STORE_PAYLOAD_SIZE    268U
#define FT_PREF_STORE_RECORD_SIZE     (FT_PREF_STORE_HEADER_SIZE + FT_PREF_STORE_PAYLOAD_SIZE)
#define FT_PREF_STORE_CRC_OFFSET      16U
#define FT_PREF_STORE_PAYLOAD_OFFSET  FT_PREF_STORE_HEADER_SIZE

#define FT_PREF_STORE_DEBOUNCE_MS     2000U
#define FT_PREF_STORE_EVENT_WAKE      0x01U
#define FT_PREF_STORE_THREAD_STACK    4096U
#define FT_PREF_STORE_THREAD_PRIORITY 24U
#define FT_PREF_STORE_THREAD_TICK     10U

#define FT_PREF_BACKGROUND_COUNT      4U
#define FT_PREF_LANGUAGE_COUNT        2U
#define FT_PREF_DEFAULT_AUDIO_OUTPUT_VOLUME 70U
#define FT_PREF_DEFAULT_AUDIO_INPUT_GAIN    40U
#define FT_PREF_DEFAULT_AUDIO_OUTPUT_SAMPLE_RATE 16000U
#define FT_PREF_DEFAULT_AUDIO_OUTPUT_SAMPLE_BITS 16U
#define FT_PREF_DEFAULT_AUDIO_OUTPUT_CHANNELS    2U

typedef struct
{
    bool initialized;
    bool worker_started;
    bool loaded_from_storage;
    bool dirty;
    bool write_in_progress;
    bool frozen;
    bool test_suspended;
    int8_t active_slot;
    uint8_t valid_slots;
    uint32_t generation;
    uint32_t update_serial;
    uint32_t successful_writes;
    uint32_t failed_writes;
    uint32_t ignored_test_updates;
    rt_tick_t last_update_tick;
    int last_error;
    ft_preferences_store_payload_t payload;
} ft_preferences_store_state_t;

static struct rt_mutex s_store_lock;
static struct rt_event s_store_event;
static bool s_primitives_ready;
static ft_preferences_store_state_t s_store;
static rt_thread_t s_store_thread;

static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16_le(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static uint8_t sample_rate_code(uint32_t sample_rate)
{
    switch (sample_rate)
    {
    case 16000U: return 1U;
    case 24000U: return 2U;
    case 48000U: return 3U;
    case 96000U: return 4U;
    default: return 0U;
    }
}

static uint32_t sample_rate_from_code(uint8_t code)
{
    switch (code)
    {
    case 1U: return 16000U;
    case 2U: return 24000U;
    case 3U: return 48000U;
    case 4U: return 96000U;
    default: return FT_PREF_DEFAULT_AUDIO_OUTPUT_SAMPLE_RATE;
    }
}

static uint32_t record_crc32(const uint8_t *record, size_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;

    for (index = 0U; index < size; index++)
    {
        uint8_t value = (index >= FT_PREF_STORE_CRC_OFFSET &&
                         index < FT_PREF_STORE_CRC_OFFSET + 4U)
                            ? 0U : record[index];
        uint8_t bit;

        crc ^= value;
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc >> 1) ^ (0xEDB88320UL &
                  (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ 0xFFFFFFFFUL;
}

static bool path_has_parent_component(const char *path, size_t length)
{
    size_t start = 0U;

    while (start < length)
    {
        size_t end;

        while (start < length && path[start] == '/') start++;
        end = start;
        while (end < length && path[end] != '/') end++;
        if (end - start == 2U && path[start] == '.' && path[start + 1U] == '.')
            return true;
        start = end;
    }
    return false;
}

bool ft_preferences_store_payload_valid(
    const ft_preferences_store_payload_t *payload)
{
    const char *terminator;
    size_t path_length;
    size_t index;

    if (payload == RT_NULL || payload->accent_rgb > 0xFFFFFFUL ||
        payload->tile_opa < FT_PREFERENCES_STORE_TILE_OPA_MIN ||
        payload->background >= FT_PREF_BACKGROUND_COUNT ||
        payload->timezone_offset_minutes <
            FT_PREFERENCES_STORE_TIMEZONE_MINUTES_MIN ||
        payload->timezone_offset_minutes >
            FT_PREFERENCES_STORE_TIMEZONE_MINUTES_MAX ||
        payload->language >= FT_PREF_LANGUAGE_COUNT ||
        payload->audio_output_volume >
            FT_PREFERENCES_STORE_AUDIO_OUTPUT_VOLUME_MAX ||
        payload->audio_input_gain >
            FT_PREFERENCES_STORE_AUDIO_INPUT_GAIN_MAX ||
        sample_rate_code(payload->audio_output_sample_rate) == 0U ||
        (payload->audio_output_sample_bits != 16U &&
         payload->audio_output_sample_bits != 24U) ||
        (payload->audio_output_channels != 1U &&
         payload->audio_output_channels != 2U))
        return false;

    terminator = (const char *)memchr(payload->wallpaper_path, '\0',
                                      sizeof(payload->wallpaper_path));
    if (terminator == RT_NULL) return false;
    path_length = (size_t)(terminator - payload->wallpaper_path);
    if (payload->background == 3U && path_length == 0U)
        return false;
    if (path_length > 0U &&
        strncmp(payload->wallpaper_path, "/flash/", 7U) != 0 &&
        strncmp(payload->wallpaper_path, "/sdcard/", 8U) != 0)
        return false;
    if (path_has_parent_component(payload->wallpaper_path, path_length))
        return false;
    for (index = 0U; index < path_length; index++)
    {
        uint8_t value = (uint8_t)payload->wallpaper_path[index];
        if (value < 0x20U || value == 0x7FU || value == '\\')
            return false;
    }
    return true;
}

static bool payload_equal(const ft_preferences_store_payload_t *left,
                          const ft_preferences_store_payload_t *right)
{
    return left->accent_rgb == right->accent_rgb &&
           left->tile_opa == right->tile_opa &&
           left->background == right->background &&
           left->use_24_hour == right->use_24_hour &&
           left->timezone_offset_minutes == right->timezone_offset_minutes &&
           left->language == right->language &&
           left->audio_output_volume == right->audio_output_volume &&
           left->audio_input_gain == right->audio_input_gain &&
           left->audio_output_sample_rate == right->audio_output_sample_rate &&
           left->audio_output_sample_bits == right->audio_output_sample_bits &&
           left->audio_output_channels == right->audio_output_channels &&
           strcmp(left->wallpaper_path, right->wallpaper_path) == 0;
}

static void serialize_record(const ft_preferences_store_payload_t *payload,
                             uint32_t generation,
                             uint8_t record[FT_PREF_STORE_RECORD_SIZE])
{
    uint8_t *data = record + FT_PREF_STORE_PAYLOAD_OFFSET;
    size_t path_length = strlen(payload->wallpaper_path);

    memset(record, 0, FT_PREF_STORE_RECORD_SIZE);
    put_u32_le(record + 0U, FT_PREF_STORE_MAGIC);
    put_u16_le(record + 4U, FT_PREF_STORE_SCHEMA);
    put_u16_le(record + 6U, FT_PREF_STORE_HEADER_SIZE);
    put_u32_le(record + 8U, FT_PREF_STORE_PAYLOAD_SIZE);
    put_u32_le(record + 12U, generation);
    /* Schema-1 bytes 20..23 were reserved. Zero in an older record maps to
     * 16 kHz / 16-bit / stereo, preserving on-device compatibility. */
    record[20] = sample_rate_code(payload->audio_output_sample_rate);
    record[21] = payload->audio_output_sample_bits;
    record[22] = payload->audio_output_channels;

    put_u32_le(data + 0U, payload->accent_rgb);
    data[4] = payload->tile_opa;
    data[5] = payload->background;
    data[6] = payload->use_24_hour ? 1U : 0U;
    data[7] = payload->language;
    put_u16_le(data + 8U, (uint16_t)payload->timezone_offset_minutes);
    /* Bytes 10 and 11 were reserved in schema 1. Store value + 1 so records
     * written before audio settings (both bytes zero) remain distinguishable
     * from a deliberate zero-volume setting without a schema migration. */
    data[10] = (uint8_t)(payload->audio_output_volume + 1U);
    data[11] = (uint8_t)(payload->audio_input_gain + 1U);
    memcpy(data + 12U, payload->wallpaper_path, path_length + 1U);

    put_u32_le(record + FT_PREF_STORE_CRC_OFFSET,
               record_crc32(record, FT_PREF_STORE_RECORD_SIZE));
}

static int deserialize_record(const uint8_t record[FT_PREF_STORE_RECORD_SIZE],
                              ft_preferences_store_payload_t *payload,
                              uint32_t *generation)
{
    const uint8_t *data = record + FT_PREF_STORE_PAYLOAD_OFFSET;
    uint32_t stored_crc;

    if (get_u32_le(record + 0U) != FT_PREF_STORE_MAGIC ||
        get_u16_le(record + 4U) != FT_PREF_STORE_SCHEMA ||
        get_u16_le(record + 6U) != FT_PREF_STORE_HEADER_SIZE ||
        get_u32_le(record + 8U) != FT_PREF_STORE_PAYLOAD_SIZE)
        return -RT_ERROR;
    stored_crc = get_u32_le(record + FT_PREF_STORE_CRC_OFFSET);
    if (stored_crc != record_crc32(record, FT_PREF_STORE_RECORD_SIZE))
        return -RT_ERROR;
    if (data[6] > 1U || memchr(data + 12U, '\0',
                              FT_PREFERENCES_STORE_WALLPAPER_PATH_MAX) == RT_NULL)
        return -RT_ERROR;

    memset(payload, 0, sizeof(*payload));
    payload->accent_rgb = get_u32_le(data + 0U);
    payload->tile_opa = data[4];
    payload->background = data[5];
    payload->use_24_hour = data[6] != 0U;
    payload->language = data[7];
    payload->timezone_offset_minutes = (int16_t)get_u16_le(data + 8U);
    payload->audio_output_volume = data[10] == 0U ?
        FT_PREF_DEFAULT_AUDIO_OUTPUT_VOLUME : (uint8_t)(data[10] - 1U);
    payload->audio_input_gain = data[11] == 0U ?
        FT_PREF_DEFAULT_AUDIO_INPUT_GAIN : (uint8_t)(data[11] - 1U);
    payload->audio_output_sample_rate = sample_rate_from_code(record[20]);
    payload->audio_output_sample_bits = record[21] == 0U ?
        FT_PREF_DEFAULT_AUDIO_OUTPUT_SAMPLE_BITS : record[21];
    payload->audio_output_channels = record[22] == 0U ?
        FT_PREF_DEFAULT_AUDIO_OUTPUT_CHANNELS : record[22];
    memcpy(payload->wallpaper_path, data + 12U,
           FT_PREFERENCES_STORE_WALLPAPER_PATH_MAX);
    if (!ft_preferences_store_payload_valid(payload)) return -RT_ERROR;
    *generation = get_u32_le(record + 12U);
    return RT_EOK;
}

static int read_record(const char *path,
                       ft_preferences_store_payload_t *payload,
                       uint32_t *generation)
{
    uint8_t record[FT_PREF_STORE_RECORD_SIZE];
    size_t offset = 0U;
    int descriptor;
    int result = -RT_EIO;

    descriptor = open(path, O_RDONLY | O_BINARY, 0);
    if (descriptor < 0) return -RT_ERROR;
    while (offset < sizeof(record))
    {
        int count = read(descriptor, record + offset,
                         sizeof(record) - offset);
        if (count <= 0) goto done;
        offset += (size_t)count;
    }
    {
        uint8_t trailing;
        int count = read(descriptor, &trailing, 1U);
        if (count != 0) goto done;
    }
    result = deserialize_record(record, payload, generation);

done:
    close(descriptor);
    return result;
}

static int ensure_store_directory(void)
{
    struct stat information;

    if (stat(FT_PREF_STORE_DIRECTORY, &information) == 0)
        return S_ISDIR(information.st_mode) ? RT_EOK : -RT_EIO;
    if (mkdir(FT_PREF_STORE_DIRECTORY, 0777) == 0) return RT_EOK;
    if (stat(FT_PREF_STORE_DIRECTORY, &information) == 0 &&
        S_ISDIR(information.st_mode))
        return RT_EOK;
    return -RT_EIO;
}

static int write_record(const char *path,
                        const ft_preferences_store_payload_t *payload,
                        uint32_t generation)
{
    uint8_t record[FT_PREF_STORE_RECORD_SIZE];
    ft_preferences_store_payload_t verified_payload;
    uint32_t verified_generation = 0U;
    size_t offset = 0U;
    int descriptor;
    int result = -RT_EIO;

    if (ensure_store_directory() != RT_EOK) return -RT_EIO;
    serialize_record(payload, generation, record);
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (descriptor < 0) return -RT_EIO;
    while (offset < sizeof(record))
    {
        int count = write(descriptor, record + offset,
                          sizeof(record) - offset);
        if (count <= 0) goto close_file;
        offset += (size_t)count;
    }
    if (fsync(descriptor) != 0) goto close_file;
    result = RT_EOK;

close_file:
    if (close(descriptor) != 0) result = -RT_EIO;
    if (result != RT_EOK) return result;
    if (board_flash_storage_sync() != RT_EOK) return -RT_EIO;

    /* The inactive slot becomes active only after an on-media readback has
     * passed schema/range checks and the full-record CRC. */
    result = read_record(path, &verified_payload, &verified_generation);
    if (result != RT_EOK || verified_generation != generation ||
        !payload_equal(payload, &verified_payload))
        return -RT_EIO;
    return RT_EOK;
}

static bool generation_newer(uint32_t left, uint32_t right)
{
    uint32_t delta = left - right;
    return delta != 0U && delta < 0x80000000UL;
}

static rt_tick_t debounce_ticks(void)
{
    rt_tick_t ticks = rt_tick_from_millisecond(FT_PREF_STORE_DEBOUNCE_MS);
    return ticks == 0U ? 1U : ticks;
}

static int persist_once(bool allow_frozen)
{
    ft_preferences_store_payload_t snapshot;
    uint32_t snapshot_serial;
    uint32_t next_generation;
    int8_t target_slot;
    int result;

    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    if (!s_store.initialized)
        result = -RT_ENOSYS;
    else if (s_store.write_in_progress)
        result = -RT_EBUSY;
    else if (!s_store.dirty)
        result = RT_EOK;
    else if (s_store.test_suspended || (s_store.frozen && !allow_frozen))
        result = -RT_EBUSY;
    else
    {
        snapshot = s_store.payload;
        snapshot_serial = s_store.update_serial;
        next_generation = s_store.generation + 1U;
        target_slot = s_store.active_slot == 0 ? 1 : 0;
        s_store.write_in_progress = true;
        result = INT_MIN;
    }
    rt_mutex_release(&s_store_lock);
    if (result != INT_MIN) return result;

    result = write_record(target_slot == 0 ? FT_PREF_STORE_SLOT_A_PATH
                                           : FT_PREF_STORE_SLOT_B_PATH,
                          &snapshot, next_generation);

    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        s_store.active_slot = target_slot;
        s_store.valid_slots |= (uint8_t)(1U << (uint8_t)target_slot);
        s_store.generation = next_generation;
        s_store.successful_writes++;
        if (s_store.update_serial == snapshot_serial)
            s_store.dirty = false;
        s_store.last_error = RT_EOK;
    }
    else
    {
        s_store.failed_writes++;
        s_store.dirty = true;
        s_store.last_update_tick = rt_tick_get();
        s_store.last_error = result;
    }
    s_store.write_in_progress = false;
    rt_mutex_release(&s_store_lock);
    rt_event_send(&s_store_event, FT_PREF_STORE_EVENT_WAKE);
    return result;
}

static int flush_internal(bool allow_frozen)
{
    for (;;)
    {
        bool writing;
        bool dirty;
        bool blocked;
        int result;

        rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
        if (!s_store.initialized)
        {
            rt_mutex_release(&s_store_lock);
            return -RT_ENOSYS;
        }
        writing = s_store.write_in_progress;
        dirty = s_store.dirty;
        blocked = s_store.test_suspended ||
                  (s_store.frozen && !allow_frozen);
        rt_mutex_release(&s_store_lock);

        if (writing)
        {
            rt_thread_mdelay(5U);
            continue;
        }
        if (!dirty) return RT_EOK;
        if (blocked) return -RT_EBUSY;
        result = persist_once(allow_frozen);
        if (result == -RT_EBUSY) continue;
        if (result != RT_EOK) return result;
        /* An update can arrive while the snapshot is being written. Loop until
         * every snapshot visible before this iteration has been committed. */
    }
}

static void preferences_store_worker(void *parameter)
{
    RT_UNUSED(parameter);

    for (;;)
    {
        bool due = false;
        rt_int32_t timeout = RT_WAITING_FOREVER;
        rt_tick_t now;
        rt_tick_t elapsed;
        rt_tick_t quiet_ticks = debounce_ticks();
        rt_uint32_t received;

        rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
        if (s_store.dirty && !s_store.write_in_progress && !s_store.frozen &&
            !s_store.test_suspended)
        {
            now = rt_tick_get();
            elapsed = now - s_store.last_update_tick;
            if (elapsed >= quiet_ticks)
                due = true;
            else
                timeout = (rt_int32_t)(quiet_ticks - elapsed);
        }
        rt_mutex_release(&s_store_lock);

        if (due)
        {
            (void)persist_once(false);
            continue;
        }
        (void)rt_event_recv(&s_store_event, FT_PREF_STORE_EVENT_WAKE,
                            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                            timeout, &received);
    }
}

static int start_worker_if_needed(void)
{
    if (s_store_thread != RT_NULL) return RT_EOK;
    s_store_thread = rt_thread_create("ft_pref", preferences_store_worker,
                                      RT_NULL, FT_PREF_STORE_THREAD_STACK,
                                      FT_PREF_STORE_THREAD_PRIORITY,
                                      FT_PREF_STORE_THREAD_TICK);
    if (s_store_thread == RT_NULL) return -RT_ENOMEM;
    if (rt_thread_startup(s_store_thread) != RT_EOK)
    {
        rt_thread_delete(s_store_thread);
        s_store_thread = RT_NULL;
        return -RT_ERROR;
    }
    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    s_store.worker_started = true;
    rt_mutex_release(&s_store_lock);
    return RT_EOK;
}

int ft_preferences_store_init(const ft_preferences_store_payload_t *defaults,
                              ft_preferences_store_payload_t *loaded)
{
    ft_preferences_store_payload_t slot_payload[2];
    uint32_t slot_generation[2] = {0U, 0U};
    bool slot_valid[2];
    int selected = -1;
    int result;

    if (!ft_preferences_store_payload_valid(defaults)) return -RT_EINVAL;
    if (!s_primitives_ready)
    {
        if (rt_mutex_init(&s_store_lock, "ft_pref", RT_IPC_FLAG_PRIO) != RT_EOK)
            return -RT_ERROR;
        if (rt_event_init(&s_store_event, "ft_pref", RT_IPC_FLAG_PRIO) != RT_EOK)
        {
            rt_mutex_detach(&s_store_lock);
            return -RT_ERROR;
        }
        s_primitives_ready = true;
    }

    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    if (s_store.initialized)
    {
        if (loaded != RT_NULL) *loaded = s_store.payload;
        rt_mutex_release(&s_store_lock);
        return start_worker_if_needed();
    }
    rt_mutex_release(&s_store_lock);

    slot_valid[0] = read_record(FT_PREF_STORE_SLOT_A_PATH, &slot_payload[0],
                                &slot_generation[0]) == RT_EOK;
    slot_valid[1] = read_record(FT_PREF_STORE_SLOT_B_PATH, &slot_payload[1],
                                &slot_generation[1]) == RT_EOK;
    if (slot_valid[0] && slot_valid[1])
        selected = generation_newer(slot_generation[1], slot_generation[0]) ? 1 : 0;
    else if (slot_valid[0])
        selected = 0;
    else if (slot_valid[1])
        selected = 1;

    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    memset(&s_store, 0, sizeof(s_store));
    s_store.initialized = true;
    s_store.active_slot = (int8_t)selected;
    s_store.valid_slots = (uint8_t)((slot_valid[0] ? 1U : 0U) |
                                    (slot_valid[1] ? 2U : 0U));
    s_store.last_update_tick = rt_tick_get();
    if (selected >= 0)
    {
        s_store.payload = slot_payload[selected];
        s_store.generation = slot_generation[selected];
        s_store.loaded_from_storage = true;
    }
    else
    {
        s_store.payload = *defaults;
        s_store.dirty = true;
        s_store.update_serial = 1U;
    }
    if (loaded != RT_NULL) *loaded = s_store.payload;
    rt_mutex_release(&s_store_lock);

    result = start_worker_if_needed();
    if (result == RT_EOK && selected < 0)
        rt_event_send(&s_store_event, FT_PREF_STORE_EVENT_WAKE);
    return result;
}

int ft_preferences_store_update(const ft_preferences_store_payload_t *payload)
{
    if (!ft_preferences_store_payload_valid(payload)) return -RT_EINVAL;
    if (!s_primitives_ready) return -RT_ENOSYS;

    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    if (!s_store.initialized)
    {
        rt_mutex_release(&s_store_lock);
        return -RT_ENOSYS;
    }
    if (s_store.test_suspended)
    {
        s_store.ignored_test_updates++;
        rt_mutex_release(&s_store_lock);
        return RT_EOK;
    }
    if (payload_equal(&s_store.payload, payload))
    {
        rt_mutex_release(&s_store_lock);
        return RT_EOK;
    }
    s_store.payload = *payload;
    s_store.update_serial++;
    s_store.dirty = true;
    s_store.last_update_tick = rt_tick_get();
    rt_mutex_release(&s_store_lock);
    rt_event_send(&s_store_event, FT_PREF_STORE_EVENT_WAKE);
    return RT_EOK;
}

int ft_preferences_store_snapshot(ft_preferences_store_payload_t *payload)
{
    if (payload == RT_NULL) return -RT_EINVAL;
    if (!s_primitives_ready) return -RT_ENOSYS;
    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    if (!s_store.initialized)
    {
        rt_mutex_release(&s_store_lock);
        return -RT_ENOSYS;
    }
    *payload = s_store.payload;
    rt_mutex_release(&s_store_lock);
    return RT_EOK;
}

int ft_preferences_store_flush(void)
{
    if (!s_primitives_ready) return -RT_ENOSYS;
    return flush_internal(false);
}

int ft_preferences_store_freeze(void)
{
    int result;

    if (!s_primitives_ready) return -RT_ENOSYS;
    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    if (!s_store.initialized || s_store.test_suspended)
    {
        result = !s_store.initialized ? -RT_ENOSYS : -RT_EBUSY;
        rt_mutex_release(&s_store_lock);
        return result;
    }
    s_store.frozen = true;
    rt_mutex_release(&s_store_lock);
    result = flush_internal(true);
    return result;
}

void ft_preferences_store_thaw(void)
{
    board_flash_storage_info_t flash_info;
    ft_preferences_store_payload_t slot_payload[2];
    uint32_t slot_generation[2] = {0U, 0U};
    bool slot_valid[2];
    int selected = -1;
    bool wake = false;

    if (!s_primitives_ready) return;
    if (board_flash_storage_get_info(&flash_info) != RT_EOK ||
        !flash_info.mounted || flash_info.exported || flash_info.transitioning)
        return;
    /* /flash may have been formatted by the host or by the local Storage
     * settings while the store was frozen.  Re-scan both slots after the
     * filesystem is mounted again.  The in-memory device preferences remain
     * authoritative; if their newest durable record disappeared or changed,
     * queue a fresh A/B commit instead of silently losing the configuration. */
    slot_valid[0] = read_record(FT_PREF_STORE_SLOT_A_PATH, &slot_payload[0],
                                &slot_generation[0]) == RT_EOK;
    slot_valid[1] = read_record(FT_PREF_STORE_SLOT_B_PATH, &slot_payload[1],
                                &slot_generation[1]) == RT_EOK;
    if (slot_valid[0] && slot_valid[1])
        selected = generation_newer(slot_generation[1], slot_generation[0]) ? 1 : 0;
    else if (slot_valid[0])
        selected = 0;
    else if (slot_valid[1])
        selected = 1;

    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    if (s_store.initialized)
    {
        s_store.frozen = false;
        s_store.valid_slots = (uint8_t)((slot_valid[0] ? 1U : 0U) |
                                        (slot_valid[1] ? 2U : 0U));
        s_store.active_slot = (int8_t)selected;
        s_store.generation = selected >= 0 ? slot_generation[selected] : 0U;
        if (selected < 0 ||
            !payload_equal(&s_store.payload, &slot_payload[selected]))
        {
            s_store.dirty = true;
            s_store.last_update_tick = rt_tick_get();
        }
        wake = s_store.dirty;
    }
    rt_mutex_release(&s_store_lock);
    if (wake) rt_event_send(&s_store_event, FT_PREF_STORE_EVENT_WAKE);
}

int ft_preferences_store_test_suspend(bool suspend)
{
    if (!s_primitives_ready) return -RT_ENOSYS;

    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    if (!s_store.initialized)
    {
        rt_mutex_release(&s_store_lock);
        return -RT_ENOSYS;
    }
    s_store.test_suspended = suspend;
    rt_mutex_release(&s_store_lock);

    if (suspend)
    {
        for (;;)
        {
            bool writing;
            rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
            writing = s_store.write_in_progress;
            rt_mutex_release(&s_store_lock);
            if (!writing) break;
            rt_thread_mdelay(5U);
        }
    }
    else
    {
        rt_event_send(&s_store_event, FT_PREF_STORE_EVENT_WAKE);
    }
    return RT_EOK;
}

int ft_preferences_store_get_status(ft_preferences_store_status_t *status)
{
    if (status == RT_NULL) return -RT_EINVAL;
    memset(status, 0, sizeof(*status));
    status->active_slot = -1;
    if (!s_primitives_ready) return -RT_ENOSYS;

    rt_mutex_take(&s_store_lock, RT_WAITING_FOREVER);
    status->initialized = s_store.initialized;
    status->worker_started = s_store.worker_started;
    status->loaded_from_storage = s_store.loaded_from_storage;
    status->dirty = s_store.dirty;
    status->write_in_progress = s_store.write_in_progress;
    status->frozen = s_store.frozen;
    status->test_suspended = s_store.test_suspended;
    status->active_slot = s_store.active_slot;
    status->valid_slots = s_store.valid_slots;
    status->generation = s_store.generation;
    status->update_serial = s_store.update_serial;
    status->successful_writes = s_store.successful_writes;
    status->failed_writes = s_store.failed_writes;
    status->ignored_test_updates = s_store.ignored_test_updates;
    status->last_error = s_store.last_error;
    rt_mutex_release(&s_store_lock);
    return status->initialized ? RT_EOK : -RT_ENOSYS;
}

#else /* RT_USING_DFS */

bool ft_preferences_store_payload_valid(
    const ft_preferences_store_payload_t *payload)
{
    RT_UNUSED(payload);
    return false;
}

int ft_preferences_store_init(const ft_preferences_store_payload_t *defaults,
                              ft_preferences_store_payload_t *loaded)
{
    RT_UNUSED(defaults);
    RT_UNUSED(loaded);
    return -RT_ENOSYS;
}

int ft_preferences_store_update(const ft_preferences_store_payload_t *payload)
{
    RT_UNUSED(payload);
    return -RT_ENOSYS;
}

int ft_preferences_store_snapshot(ft_preferences_store_payload_t *payload)
{
    RT_UNUSED(payload);
    return -RT_ENOSYS;
}

int ft_preferences_store_flush(void) { return -RT_ENOSYS; }
int ft_preferences_store_freeze(void) { return -RT_ENOSYS; }
void ft_preferences_store_thaw(void) { }

int ft_preferences_store_test_suspend(bool suspend)
{
    RT_UNUSED(suspend);
    return -RT_ENOSYS;
}

int ft_preferences_store_get_status(ft_preferences_store_status_t *status)
{
    if (status != RT_NULL)
    {
        memset(status, 0, sizeof(*status));
        status->active_slot = -1;
    }
    return -RT_ENOSYS;
}

#endif /* RT_USING_DFS */
