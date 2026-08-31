#include <rtthread.h>
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_icons.h"
#include "feathertalk_icon_assets.h"

typedef struct
{
    const lv_image_dsc_t *size_24;
    const lv_image_dsc_t *size_32;
    const lv_image_dsc_t *size_48;
} ft_icon_sources_t;

#define FT_ICON_SOURCES(name) \
    {&ft_icon_asset_##name##_24, &ft_icon_asset_##name##_32, &ft_icon_asset_##name##_48}

static const ft_icon_sources_t s_icon_sources[FT_ICON_COUNT] =
{
    FT_ICON_SOURCES(system),
    FT_ICON_SOURCES(settings),
    FT_ICON_SOURCES(media),
    FT_ICON_SOURCES(gallery),
    FT_ICON_SOURCES(files),
    FT_ICON_SOURCES(about),
    FT_ICON_SOURCES(back),
    FT_ICON_SOURCES(home),
    FT_ICON_SOURCES(search),
    FT_ICON_SOURCES(wifi),
    FT_ICON_SOURCES(wifi_off),
    FT_ICON_SOURCES(wifi_weak),
    FT_ICON_SOURCES(wifi_medium),
    FT_ICON_SOURCES(battery),
    FT_ICON_SOURCES(cellular),
    FT_ICON_SOURCES(bluetooth),
    FT_ICON_SOURCES(play),
    FT_ICON_SOURCES(pause),
    FT_ICON_SOURCES(previous),
    FT_ICON_SOURCES(next),
    FT_ICON_SOURCES(refresh),
    FT_ICON_SOURCES(airplane),
    FT_ICON_SOURCES(location),
    FT_ICON_SOURCES(brightness),
    FT_ICON_SOURCES(rotation),
    FT_ICON_SOURCES(display),
    FT_ICON_SOURCES(personalization),
    FT_ICON_SOURCES(wifi_settings),
    FT_ICON_SOURCES(bluetooth_settings),
    FT_ICON_SOURCES(time_language),
    FT_ICON_SOURCES(usb),
    FT_ICON_SOURCES(sd_storage),
    FT_ICON_SOURCES(storage),
    FT_ICON_SOURCES(external_memory),
    FT_ICON_SOURCES(onchip_memory),
    FT_ICON_SOURCES(processor),
    FT_ICON_SOURCES(media_pattern),
    FT_ICON_SOURCES(tile_pattern),
    FT_ICON_SOURCES(flash_device),
    FT_ICON_SOURCES(sd_device),
    FT_ICON_SOURCES(wallpaper),
    FT_ICON_SOURCES(audio_settings),
    FT_ICON_SOURCES(speaker_device),
    FT_ICON_SOURCES(pdm_mic_device),
    FT_ICON_SOURCES(analog_mic_device),
    FT_ICON_SOURCES(recorder),
    FT_ICON_SOURCES(record_action),
    FT_ICON_SOURCES(record_stop),
    FT_ICON_SOURCES(recorder_pdm_source),
    FT_ICON_SOURCES(recorder_analog_source),
};

const lv_image_dsc_t *ft_icon_source(ft_icon_id_t icon_id, uint16_t size)
{
    if ((icon_id < 0) || (icon_id >= FT_ICON_COUNT)) return RT_NULL;
    if (size <= 24U) return s_icon_sources[icon_id].size_24;
    if (size <= 32U) return s_icon_sources[icon_id].size_32;
    return s_icon_sources[icon_id].size_48;
}

void ft_icon_set(lv_obj_t *image, ft_icon_id_t icon_id, uint16_t size)
{
    const lv_image_dsc_t *source;
    if (image == RT_NULL || !lv_obj_is_valid(image)) return;
    source = ft_icon_source(icon_id, size);
    if (source != RT_NULL) lv_image_set_src(image, source);
}

lv_obj_t *ft_icon_create(lv_obj_t *parent, ft_icon_id_t icon_id,
                         uint16_t size, bool use_accent)
{
    lv_obj_t *image = lv_image_create(parent);
    ft_icon_set(image, icon_id, size);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN);
    if (use_accent)
        ft_ui_register_accent(image, FT_ACCENT_IMAGE);
    else
        lv_obj_set_style_image_recolor(image, lv_color_white(), LV_PART_MAIN);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}
