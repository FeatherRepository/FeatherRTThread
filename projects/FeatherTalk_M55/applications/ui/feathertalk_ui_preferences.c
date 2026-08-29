#include <rtthread.h>
#include "feathertalk_ui_internal.h"

#define FT_DEFAULT_ACCENT_RGB 0x0078D7UL
#define FT_DEFAULT_TILE_OPA   255U

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

void ft_preferences_reset(void)
{
    ft_preferences_init();
}
