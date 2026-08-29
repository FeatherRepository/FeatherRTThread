#ifndef FEATHERTALK_UI_ICONS_H
#define FEATHERTALK_UI_ICONS_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef enum
{
    FT_ICON_SYSTEM = 0,
    FT_ICON_SETTINGS,
    FT_ICON_MEDIA,
    FT_ICON_MESSAGES,
    FT_ICON_FILES,
    FT_ICON_ABOUT,
    FT_ICON_BACK,
    FT_ICON_HOME,
    FT_ICON_SEARCH,
    FT_ICON_WIFI,
    FT_ICON_WIFI_OFF,
    FT_ICON_WIFI_WEAK,
    FT_ICON_WIFI_MEDIUM,
    FT_ICON_BATTERY,
    FT_ICON_CELLULAR,
    FT_ICON_BLUETOOTH,
    FT_ICON_PLAY,
    FT_ICON_PAUSE,
    FT_ICON_PREVIOUS,
    FT_ICON_NEXT,
    FT_ICON_REFRESH,
    FT_ICON_AIRPLANE,
    FT_ICON_LOCATION,
    FT_ICON_BRIGHTNESS,
    FT_ICON_ROTATION,
    FT_ICON_COUNT
} ft_icon_id_t;

const lv_image_dsc_t *ft_icon_source(ft_icon_id_t icon_id, uint16_t size);
lv_obj_t *ft_icon_create(lv_obj_t *parent, ft_icon_id_t icon_id,
                         uint16_t size, bool use_accent);
void ft_icon_set(lv_obj_t *image, ft_icon_id_t icon_id, uint16_t size);

#endif /* FEATHERTALK_UI_ICONS_H */
