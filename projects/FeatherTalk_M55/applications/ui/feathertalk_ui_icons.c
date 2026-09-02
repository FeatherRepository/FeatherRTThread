#include <rtthread.h>
#include <string.h>
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_icons.h"
#include "feathertalk_icon_assets.h"
#include "feathertalk_icon_vector_assets.h"
#include "draw/vg_lite/lv_vg_lite_path.h"

typedef struct
{
    const lv_image_dsc_t *size_24;
    const lv_image_dsc_t *size_32;
    const lv_image_dsc_t *size_48;
    const ft_icon_vector_asset_t *vector;
} ft_icon_sources_t;

#define FT_ICON_SOURCES(name) \
    {&ft_icon_asset_##name##_24, &ft_icon_asset_##name##_32, \
     &ft_icon_asset_##name##_48, &ft_icon_vector_##name}

typedef struct
{
    lv_vector_path_t *fill;
    lv_vector_path_t *stroke;
    bool initialized;
} ft_icon_vector_cache_t;

static ft_icon_vector_cache_t s_vector_cache[FT_ICON_COUNT];
static bool s_vector_icons_enabled = true;

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

static bool icon_uses_vector(ft_icon_id_t icon_id)
{
    /* Every shipped SVG has a generated geometry asset.  Keep the A8 images
     * only as a defensive fallback for an invalid or unavailable vector path;
     * normal UI rendering is fully vector based. */
    return s_vector_icons_enabled && icon_id >= 0 && icon_id < FT_ICON_COUNT;
}

static int feather_ui_icon_renderer(int argc, char **argv)
{
    bool enable;
    lv_obj_t *screen;

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "status") == 0))
    {
        rt_kprintf("FeatherTalk icon renderer: %s\n",
                   s_vector_icons_enabled ? "vector" : "a8");
        return RT_EOK;
    }
    if (argc != 2)
    {
        rt_kprintf("usage: feather_ui_icon_renderer vector|a8|status\n");
        return -RT_EINVAL;
    }
    if (strcmp(argv[1], "vector") == 0)
        enable = true;
    else if (strcmp(argv[1], "a8") == 0)
        enable = false;
    else
    {
        rt_kprintf("usage: feather_ui_icon_renderer vector|a8|status\n");
        return -RT_EINVAL;
    }

    lv_lock();
    s_vector_icons_enabled = enable;
    screen = lv_screen_active();
    if (screen != RT_NULL) lv_obj_invalidate(screen);
    lv_unlock();
    rt_kprintf("FeatherTalk icon renderer: %s (screen invalidated)\n",
               enable ? "vector" : "a8");
    return RT_EOK;
}
MSH_CMD_EXPORT(feather_ui_icon_renderer,
               Switch SVG icons between vector and A8 reference rendering.);

static uint16_t icon_render_size(uint16_t size)
{
    if (size <= 24U) return 24U;
    if (size <= 32U) return 32U;
    return 48U;
}

static ft_icon_vector_cache_t *vector_cache_get(ft_icon_id_t icon_id)
{
    const ft_icon_vector_asset_t *asset;
    ft_icon_vector_cache_t *cache;
    lv_vg_lite_path_t *native_path;
    float bounds[4];

    if (!icon_uses_vector(icon_id)) return RT_NULL;
    asset = s_icon_sources[icon_id].vector;
    cache = &s_vector_cache[icon_id];
    if (cache->initialized) return cache;

    if (asset->fill_path != RT_NULL && asset->fill_path_bytes != 0U)
    {
        bounds[0] = asset->fill_min_x;
        bounds[1] = asset->fill_min_y;
        bounds[2] = asset->fill_max_x;
        bounds[3] = asset->fill_max_y;
        cache->fill = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
        native_path = lv_vg_lite_path_create_static(
            VG_LITE_FP32, VG_LITE_MEDIUM, asset->fill_path,
            asset->fill_path_bytes, bounds);
        if (cache->fill == RT_NULL || native_path == RT_NULL ||
            !lv_draw_vg_lite_vector_path_attach_native(cache->fill, true,
                                                       native_path, bounds))
        {
            if (native_path != RT_NULL) lv_vg_lite_path_destroy(native_path);
            if (cache->fill != RT_NULL) lv_vector_path_delete(cache->fill);
            cache->fill = RT_NULL;
        }
        else
        {
            lv_vector_path_set_immutable(cache->fill);
        }
    }
    if (asset->stroke_path != RT_NULL && asset->stroke_path_bytes != 0U)
    {
        bounds[0] = asset->stroke_min_x;
        bounds[1] = asset->stroke_min_y;
        bounds[2] = asset->stroke_max_x;
        bounds[3] = asset->stroke_max_y;
        cache->stroke = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
        native_path = lv_vg_lite_path_create_static(
            VG_LITE_FP32, VG_LITE_MEDIUM, asset->stroke_path,
            asset->stroke_path_bytes, bounds);
        if (cache->stroke == RT_NULL || native_path == RT_NULL ||
            !lv_draw_vg_lite_vector_path_attach_native(cache->stroke, false,
                                                       native_path, bounds))
        {
            if (native_path != RT_NULL) lv_vg_lite_path_destroy(native_path);
            if (cache->stroke != RT_NULL) lv_vector_path_delete(cache->stroke);
            cache->stroke = RT_NULL;
        }
        else
        {
            lv_vector_path_set_immutable(cache->stroke);
        }
    }
    cache->initialized = true;
    return cache;
}

static bool draw_vector_icon(lv_layer_t *layer, lv_obj_t *obj, ft_icon_id_t icon_id)
{
    const ft_icon_vector_asset_t *asset;
    ft_icon_vector_cache_t *cache;
    lv_draw_image_dsc_t style;
    lv_vector_dsc_t *vector;
    lv_area_t coords;
    float scale_x;
    float scale_y;

    cache = vector_cache_get(icon_id);
    if (cache == RT_NULL ||
        (cache->fill == RT_NULL && cache->stroke == RT_NULL))
        return false;
    asset = s_icon_sources[icon_id].vector;
    if (asset->view_w <= 0.0f || asset->view_h <= 0.0f) return false;

    lv_draw_image_dsc_init(&style);
    lv_obj_init_draw_image_dsc(obj, LV_PART_MAIN, &style);
    lv_obj_get_coords(obj, &coords);
    vector = lv_vector_dsc_create(layer);
    scale_x = (float)lv_obj_get_width(obj) / asset->view_w;
    scale_y = (float)lv_obj_get_height(obj) / asset->view_h;
    lv_vector_dsc_identity(vector);
    lv_vector_dsc_translate(vector,
                            (float)coords.x1 - asset->view_x * scale_x,
                            (float)coords.y1 - asset->view_y * scale_y);
    lv_vector_dsc_scale(vector, scale_x, scale_y);

    if (cache->fill != RT_NULL)
    {
        lv_vector_dsc_set_fill_color(vector, style.recolor);
        lv_vector_dsc_set_fill_opa(vector, style.opa);
        lv_vector_dsc_set_stroke_opa(vector, LV_OPA_0);
        lv_vector_dsc_add_path_static(vector, cache->fill);
    }
    if (cache->stroke != RT_NULL)
    {
        /* Preserve the SVG centerline and let VG-Lite generate the exact
         * stroke contour.  Manually unioning segment rectangles and round
         * dots under NONZERO fill created opposite-winding overlaps and
         * visible holes at joins. */
        lv_vector_dsc_set_fill_opa(vector, LV_OPA_0);
        lv_vector_dsc_set_stroke_color(vector, style.recolor);
        lv_vector_dsc_set_stroke_opa(vector, style.opa);
        lv_vector_dsc_set_stroke_width(vector, asset->stroke_width);
        lv_vector_dsc_set_stroke_cap(vector,
            (lv_vector_stroke_cap_t)asset->stroke_cap);
        lv_vector_dsc_set_stroke_join(vector,
            (lv_vector_stroke_join_t)asset->stroke_join);
        lv_vector_dsc_add_path_static(vector, cache->stroke);
    }
    lv_draw_vector(vector);
    lv_vector_dsc_delete(vector);
    return true;
}

static uintptr_t icon_state_pack(ft_icon_id_t icon_id, uint16_t size)
{
    return ((uintptr_t)size << 8U) | ((uintptr_t)icon_id + 1U);
}

static bool icon_state_unpack(lv_obj_t *obj, ft_icon_id_t *icon_id, uint16_t *size)
{
    uintptr_t state = (uintptr_t)lv_obj_get_user_data(obj);
    if ((state & 0xffU) == 0U) return false;
    *icon_id = (ft_icon_id_t)((state & 0xffU) - 1U);
    *size = (uint16_t)(state >> 8U);
    return *icon_id >= 0 && *icon_id < FT_ICON_COUNT;
}

static void icon_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *obj;
    lv_layer_t *layer;
    ft_icon_id_t icon_id;
    uint16_t size;
    lv_area_t coords;
    lv_draw_image_dsc_t draw_dsc;
    const lv_image_dsc_t *source;

    obj = lv_event_get_current_target(event);
    if (!icon_state_unpack(obj, &icon_id, &size)) return;
    layer = lv_event_get_layer(event);
    if (draw_vector_icon(layer, obj, icon_id)) return;

    source = ft_icon_source(icon_id, size);
    if (source == RT_NULL) return;
    lv_draw_image_dsc_init(&draw_dsc);
    lv_obj_init_draw_image_dsc(obj, LV_PART_MAIN, &draw_dsc);
    draw_dsc.src = source;
    lv_obj_get_coords(obj, &coords);
    lv_draw_image(layer, &draw_dsc, &coords);
}

void ft_icon_set(lv_obj_t *image, ft_icon_id_t icon_id, uint16_t size)
{
    uint16_t render_size;
    if (image == RT_NULL || !lv_obj_is_valid(image)) return;
    if (icon_id < 0 || icon_id >= FT_ICON_COUNT) return;
    render_size = icon_render_size(size);
    lv_obj_set_user_data(image, (void *)icon_state_pack(icon_id, render_size));
    lv_obj_set_size(image, render_size, render_size);
    lv_obj_invalidate(image);
}

lv_obj_t *ft_icon_create(lv_obj_t *parent, ft_icon_id_t icon_id,
                         uint16_t size, bool use_accent)
{
    lv_obj_t *image = lv_obj_create(parent);
    lv_obj_remove_style_all(image);
    lv_obj_add_event_cb(image, icon_draw_event_cb, LV_EVENT_DRAW_MAIN, RT_NULL);
    ft_icon_set(image, icon_id, size);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN);
    if (use_accent)
        ft_ui_register_accent(image, FT_ACCENT_IMAGE);
    else
        lv_obj_set_style_image_recolor(image, lv_color_white(), LV_PART_MAIN);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return image;
}
