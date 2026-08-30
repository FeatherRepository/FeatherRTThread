#include <rtthread.h>
#include "feathertalk_ui_internal.h"

#define FT_DEFAULT_ACCENT_RGB 0x0078D7UL
#define FT_DEFAULT_TILE_OPA   255U
#define FT_DEFAULT_TIMEZONE_MINUTES 480

static ft_ui_preferences_t s_preferences;

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
    ft_ui_set_accent(s_preferences.accent_rgb);
    ft_ui_set_page_background(background_rgb());
    ft_pages_apply_preferences();
}

void ft_preferences_init(void)
{
    s_preferences.accent_rgb = FT_DEFAULT_ACCENT_RGB;
    s_preferences.tile_opa = FT_DEFAULT_TILE_OPA;
    s_preferences.background = FT_BACKGROUND_BLACK;
    s_preferences.use_24_hour = true;
    s_preferences.timezone_offset_minutes = FT_DEFAULT_TIMEZONE_MINUTES;
    s_preferences.language = FT_LANGUAGE_ZH_CN;
    s_preferences.revision = 1U;
    apply_preferences();
}

const ft_ui_preferences_t *ft_preferences_get(void)
{
    return &s_preferences;
}

void ft_preferences_set_accent(uint32_t rgb)
{
    s_preferences.accent_rgb = rgb & 0xFFFFFFUL;
    s_preferences.revision++;
    apply_preferences();
}

void ft_preferences_set_tile_opa(uint8_t opa)
{
    s_preferences.tile_opa = opa;
    s_preferences.revision++;
    ft_pages_apply_preferences();
}

void ft_preferences_set_background(ft_background_mode_t background)
{
    if (background >= FT_BACKGROUND_COUNT)
    {
        return;
    }
    s_preferences.background = background;
    s_preferences.revision++;
    apply_preferences();
}

void ft_preferences_set_24_hour(bool enabled)
{
    if (s_preferences.use_24_hour == enabled) return;
    s_preferences.use_24_hour = enabled;
    s_preferences.revision++;
    ft_pages_apply_preferences();
    ft_ui_preferences_changed();
}

void ft_preferences_set_timezone(int16_t offset_minutes)
{
    if (offset_minutes < -720 || offset_minutes > 840 ||
        s_preferences.timezone_offset_minutes == offset_minutes) return;
    s_preferences.timezone_offset_minutes = offset_minutes;
    s_preferences.revision++;
    ft_pages_apply_preferences();
    ft_ui_preferences_changed();
}

void ft_preferences_set_language(ft_language_t language)
{
    if (language >= FT_LANGUAGE_COUNT || s_preferences.language == language) return;
    s_preferences.language = language;
    s_preferences.revision++;
    ft_pages_apply_preferences();
    ft_ui_preferences_changed();
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
    ft_preferences_init();
}
