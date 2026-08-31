#include <rtthread.h>
#include <string.h>
#include "feathertalk_audio.h"
#include "feathertalk_storage.h"
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_preferences_store.h"

#define FT_DEFAULT_ACCENT_RGB 0x0078D7UL
#define FT_DEFAULT_TILE_OPA   255U
#define FT_DEFAULT_TIMEZONE_MINUTES 480
#define FT_DEFAULT_AUDIO_OUTPUT_VOLUME 70U
#define FT_DEFAULT_AUDIO_INPUT_GAIN    40U
#define FT_DEFAULT_AUDIO_OUTPUT_SAMPLE_RATE 16000U
#define FT_DEFAULT_AUDIO_OUTPUT_SAMPLE_BITS 16U
#define FT_DEFAULT_AUDIO_OUTPUT_CHANNELS    2U

static ft_ui_preferences_t s_preferences;
static ft_ui_preferences_t s_test_snapshot;
static bool s_test_active;
static bool s_wallpaper_media_known;
static bool s_wallpaper_media_ready;

static void preferences_defaults(ft_ui_preferences_t *preferences)
{
    memset(preferences, 0, sizeof(*preferences));
    preferences->accent_rgb = FT_DEFAULT_ACCENT_RGB;
    preferences->tile_opa = FT_DEFAULT_TILE_OPA;
    preferences->background = FT_BACKGROUND_BLACK;
    preferences->use_24_hour = true;
    preferences->timezone_offset_minutes = FT_DEFAULT_TIMEZONE_MINUTES;
    preferences->language = FT_LANGUAGE_ZH_CN;
    preferences->audio_output_volume = FT_DEFAULT_AUDIO_OUTPUT_VOLUME;
    preferences->audio_input_gain = FT_DEFAULT_AUDIO_INPUT_GAIN;
    preferences->audio_output_sample_rate = FT_DEFAULT_AUDIO_OUTPUT_SAMPLE_RATE;
    preferences->audio_output_sample_bits = FT_DEFAULT_AUDIO_OUTPUT_SAMPLE_BITS;
    preferences->audio_output_channels = FT_DEFAULT_AUDIO_OUTPUT_CHANNELS;
    preferences->revision = 1U;
}

static void preferences_to_payload(const ft_ui_preferences_t *preferences,
                                   ft_preferences_store_payload_t *payload)
{
    memset(payload, 0, sizeof(*payload));
    payload->accent_rgb = preferences->accent_rgb;
    payload->tile_opa = preferences->tile_opa;
    payload->background = (uint8_t)preferences->background;
    payload->use_24_hour = preferences->use_24_hour;
    payload->timezone_offset_minutes = preferences->timezone_offset_minutes;
    payload->language = (uint8_t)preferences->language;
    payload->audio_output_volume = preferences->audio_output_volume;
    payload->audio_input_gain = preferences->audio_input_gain;
    payload->audio_output_sample_rate = preferences->audio_output_sample_rate;
    payload->audio_output_sample_bits = preferences->audio_output_sample_bits;
    payload->audio_output_channels = preferences->audio_output_channels;
    rt_strncpy(payload->wallpaper_path, preferences->wallpaper_path,
               sizeof(payload->wallpaper_path) - 1U);
}

static void preferences_from_payload(const ft_preferences_store_payload_t *payload,
                                     ft_ui_preferences_t *preferences)
{
    memset(preferences, 0, sizeof(*preferences));
    preferences->accent_rgb = payload->accent_rgb;
    preferences->tile_opa = payload->tile_opa;
    preferences->background = (ft_background_mode_t)payload->background;
    preferences->use_24_hour = payload->use_24_hour;
    preferences->timezone_offset_minutes = payload->timezone_offset_minutes;
    preferences->language = (ft_language_t)payload->language;
    preferences->audio_output_volume = payload->audio_output_volume;
    preferences->audio_input_gain = payload->audio_input_gain;
    preferences->audio_output_sample_rate = payload->audio_output_sample_rate;
    preferences->audio_output_sample_bits = payload->audio_output_sample_bits;
    preferences->audio_output_channels = payload->audio_output_channels;
    rt_strncpy(preferences->wallpaper_path, payload->wallpaper_path,
               sizeof(preferences->wallpaper_path) - 1U);
    preferences->revision = 1U;
}

static void persist_preferences(void)
{
    ft_preferences_store_payload_t payload;
    preferences_to_payload(&s_preferences, &payload);
    if (ft_preferences_store_update(&payload) != RT_EOK)
        rt_kprintf("[FeatherTalk UI] preference update was not queued\n");
}

static bool wallpaper_media_ready(const char *path)
{
    ft_storage_device_info_t info;
    int result;
    if (path == RT_NULL || path[0] == '\0') return false;
    result = strncmp(path, "/flash/", 7U) == 0 ?
             ft_storage_get_flash_info(&info) :
             strncmp(path, "/sdcard/", 8U) == 0 ?
             ft_storage_get_device_info(&info) : -RT_EINVAL;
    return result == RT_EOK && info.present && info.mounted &&
           !info.usb_exported && !info.busy;
}

static uint32_t background_rgb(void)
{
    uint32_t rgb;
    uint32_t red;
    uint32_t green;
    uint32_t blue;

    if (s_preferences.background == FT_BACKGROUND_DARK)
    {
        return 0x101820UL;
    }
    if (s_preferences.background != FT_BACKGROUND_ACCENT)
    {
        return 0x000000UL;
    }

    rgb = s_preferences.accent_rgb;
    red = ((rgb >> 16) & 0xFFU) / 5U;
    green = ((rgb >> 8) & 0xFFU) / 5U;
    blue = (rgb & 0xFFU) / 5U;
    return (red << 16) | (green << 8) | blue;
}

static void apply_preferences(void)
{
    bool custom = s_preferences.background == FT_BACKGROUND_CUSTOM &&
                  s_preferences.wallpaper_path[0] != '\0';
    ft_ui_set_accent(s_preferences.accent_rgb);
    ft_ui_set_page_background(background_rgb());
    s_wallpaper_media_ready = custom &&
        wallpaper_media_ready(s_preferences.wallpaper_path);
    s_wallpaper_media_known = true;
    ft_ui_set_page_wallpaper(s_wallpaper_media_ready ?
                             s_preferences.wallpaper_path : RT_NULL);
    (void)ft_audio_set_output_format(s_preferences.audio_output_sample_rate,
                                     s_preferences.audio_output_sample_bits,
                                     s_preferences.audio_output_channels);
    (void)ft_audio_set_output_volume(s_preferences.audio_output_volume);
    (void)ft_audio_set_input_gain(s_preferences.audio_input_gain);
    ft_pages_apply_preferences();
}

void ft_preferences_init(void)
{
    ft_ui_preferences_t defaults;
    ft_preferences_store_payload_t default_payload;
    ft_preferences_store_payload_t loaded_payload;
    int result;

    preferences_defaults(&defaults);
    preferences_to_payload(&defaults, &default_payload);
    loaded_payload = default_payload;
    result = ft_preferences_store_init(&default_payload, &loaded_payload);
    if (result == RT_EOK)
    {
        preferences_from_payload(&loaded_payload, &s_preferences);
    }
    else
    {
        s_preferences = defaults;
        rt_kprintf("[FeatherTalk UI] preference store unavailable: %d\n", result);
    }
    apply_preferences();
}

const ft_ui_preferences_t *ft_preferences_get(void)
{
    return &s_preferences;
}

void ft_preferences_set_accent(uint32_t rgb)
{
    rgb &= 0xFFFFFFUL;
    if (s_preferences.accent_rgb == rgb) return;
    s_preferences.accent_rgb = rgb;
    s_preferences.revision++;
    apply_preferences();
    persist_preferences();
}

void ft_preferences_set_tile_opa(uint8_t opa)
{
    if (opa < FT_PREFERENCES_STORE_TILE_OPA_MIN)
        opa = FT_PREFERENCES_STORE_TILE_OPA_MIN;
    if (s_preferences.tile_opa == opa) return;
    s_preferences.tile_opa = opa;
    s_preferences.revision++;
    ft_pages_apply_preferences();
    persist_preferences();
}

void ft_preferences_set_background(ft_background_mode_t background)
{
    if (background >= FT_BACKGROUND_COUNT)
    {
        return;
    }
    if (s_preferences.background == background) return;
    s_preferences.background = background;
    s_preferences.revision++;
    apply_preferences();
    persist_preferences();
}

void ft_preferences_set_24_hour(bool enabled)
{
    if (s_preferences.use_24_hour == enabled) return;
    s_preferences.use_24_hour = enabled;
    s_preferences.revision++;
    ft_pages_apply_preferences();
    ft_ui_preferences_changed();
    persist_preferences();
}

void ft_preferences_set_timezone(int16_t offset_minutes)
{
    if (offset_minutes < -720 || offset_minutes > 840 ||
        s_preferences.timezone_offset_minutes == offset_minutes) return;
    s_preferences.timezone_offset_minutes = offset_minutes;
    s_preferences.revision++;
    ft_pages_apply_preferences();
    ft_ui_preferences_changed();
    persist_preferences();
}

void ft_preferences_set_language(ft_language_t language)
{
    if (language >= FT_LANGUAGE_COUNT || s_preferences.language == language) return;
    s_preferences.language = language;
    s_preferences.revision++;
    ft_pages_apply_preferences();
    ft_ui_preferences_changed();
    persist_preferences();
}

int ft_preferences_set_audio_output_volume(uint8_t volume)
{
    int result;

    if (volume > FT_PREFERENCES_STORE_AUDIO_OUTPUT_VOLUME_MAX)
        volume = FT_PREFERENCES_STORE_AUDIO_OUTPUT_VOLUME_MAX;
    result = ft_audio_set_output_volume(volume);
    if (result != RT_EOK) return result;
    if (s_preferences.audio_output_volume == volume) return RT_EOK;
    s_preferences.audio_output_volume = volume;
    s_preferences.revision++;
    persist_preferences();
    return RT_EOK;
}

int ft_preferences_set_audio_input_gain(uint8_t gain)
{
    int result;

    if (gain > FT_PREFERENCES_STORE_AUDIO_INPUT_GAIN_MAX)
        gain = FT_PREFERENCES_STORE_AUDIO_INPUT_GAIN_MAX;
    result = ft_audio_set_input_gain(gain);
    if (result != RT_EOK) return result;
    if (s_preferences.audio_input_gain == gain) return RT_EOK;
    s_preferences.audio_input_gain = gain;
    s_preferences.revision++;
    persist_preferences();
    return RT_EOK;
}

int ft_preferences_set_audio_output_format(uint32_t sample_rate,
                                           uint8_t sample_bits,
                                           uint8_t channels)
{
    int result;

    if (!ft_audio_output_format_supported(sample_rate, sample_bits, channels))
        return -RT_EINVAL;
    result = ft_audio_set_output_format(sample_rate, sample_bits, channels);
    if (result != RT_EOK) return result;
    if (s_preferences.audio_output_sample_rate == sample_rate &&
        s_preferences.audio_output_sample_bits == sample_bits &&
        s_preferences.audio_output_channels == channels)
        return RT_EOK;
    s_preferences.audio_output_sample_rate = sample_rate;
    s_preferences.audio_output_sample_bits = sample_bits;
    s_preferences.audio_output_channels = channels;
    s_preferences.revision++;
    persist_preferences();
    return RT_EOK;
}

int ft_preferences_sync_audio_output_format(uint32_t sample_rate,
                                            uint8_t sample_bits,
                                            uint8_t channels)
{
    if (!ft_audio_output_format_supported(sample_rate, sample_bits, channels))
        return -RT_EINVAL;
    if (s_preferences.audio_output_sample_rate == sample_rate &&
        s_preferences.audio_output_sample_bits == sample_bits &&
        s_preferences.audio_output_channels == channels)
        return RT_EOK;
    s_preferences.audio_output_sample_rate = sample_rate;
    s_preferences.audio_output_sample_bits = sample_bits;
    s_preferences.audio_output_channels = channels;
    s_preferences.revision++;
    persist_preferences();
    return RT_EOK;
}

void ft_preferences_set_wallpaper_file(const char *path)
{
    ft_ui_preferences_t candidate = s_preferences;
    ft_preferences_store_payload_t payload;

    if (path == RT_NULL || path[0] == '\0' ||
        (strncmp(path, "/flash/", 7U) != 0 &&
         strncmp(path, "/sdcard/", 8U) != 0))
        return;
    rt_strncpy(candidate.wallpaper_path, path,
               sizeof(candidate.wallpaper_path) - 1U);
    candidate.wallpaper_path[sizeof(candidate.wallpaper_path) - 1U] = '\0';
    candidate.background = FT_BACKGROUND_CUSTOM;
    preferences_to_payload(&candidate, &payload);
    if (!ft_preferences_store_payload_valid(&payload)) return;
    if (s_preferences.background == FT_BACKGROUND_CUSTOM &&
        strcmp(s_preferences.wallpaper_path, candidate.wallpaper_path) == 0)
        return;
    s_preferences = candidate;
    s_preferences.revision++;
    apply_preferences();
    persist_preferences();
}

bool ft_preferences_wallpaper_available(void)
{
    return s_preferences.wallpaper_path[0] != '\0';
}

void ft_preferences_refresh_wallpaper(void)
{
    bool custom = s_preferences.background == FT_BACKGROUND_CUSTOM &&
                  s_preferences.wallpaper_path[0] != '\0';
    bool ready = custom && wallpaper_media_ready(s_preferences.wallpaper_path);
    if (!s_wallpaper_media_known || ready != s_wallpaper_media_ready)
    {
        s_wallpaper_media_known = true;
        s_wallpaper_media_ready = ready;
        ft_ui_set_page_wallpaper(ready ? s_preferences.wallpaper_path : RT_NULL);
    }
}

int ft_preferences_flush(void)
{
    return ft_preferences_store_flush();
}

const char *ft_preferences_text(const char *zh_cn, const char *en_us)
{
    return s_preferences.language == FT_LANGUAGE_ZH_CN ? zh_cn : en_us;
}

void ft_preferences_format_clock(uint32_t seconds, bool utc_time,
                                 char *buffer, size_t buffer_size)
{
    int64_t local_seconds = seconds;
    uint32_t day_seconds;
    uint32_t hour;
    uint32_t minute;
    uint32_t hour12;
    const char *period;

    if (buffer == RT_NULL || buffer_size == 0U) return;
    if (utc_time)
        local_seconds += (int64_t)s_preferences.timezone_offset_minutes * 60LL;
    local_seconds %= 86400LL;
    if (local_seconds < 0) local_seconds += 86400LL;
    day_seconds = (uint32_t)local_seconds;
    hour = day_seconds / 3600U;
    minute = (day_seconds / 60U) % 60U;
    if (s_preferences.use_24_hour)
    {
        rt_snprintf(buffer, buffer_size, "%02lu:%02lu",
                    (unsigned long)hour, (unsigned long)minute);
        return;
    }
    hour12 = hour % 12U;
    if (hour12 == 0U) hour12 = 12U;
    period = hour < 12U ? "AM" : "PM";
    rt_snprintf(buffer, buffer_size, "%lu:%02lu %s",
                (unsigned long)hour12, (unsigned long)minute, period);
}

void ft_preferences_reset(void)
{
    uint32_t revision = s_preferences.revision + 1U;
    preferences_defaults(&s_preferences);
    s_preferences.revision = revision;
    apply_preferences();
    ft_ui_preferences_changed();
    persist_preferences();
}

void ft_preferences_test_begin(void)
{
    if (s_test_active) return;
    (void)ft_preferences_store_flush();
    s_test_snapshot = s_preferences;
    if (ft_preferences_store_test_suspend(true) == RT_EOK)
        s_test_active = true;
}

void ft_preferences_test_end(void)
{
    if (!s_test_active) return;
    s_preferences = s_test_snapshot;
    s_preferences.revision++;
    apply_preferences();
    ft_ui_preferences_changed();
    (void)ft_preferences_store_test_suspend(false);
    persist_preferences();
    s_test_active = false;
}
