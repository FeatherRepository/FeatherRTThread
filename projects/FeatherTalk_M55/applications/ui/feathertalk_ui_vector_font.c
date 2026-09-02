#include <math.h>
#include <rtthread.h>
#include "feathertalk_ui_font.h"
#include "feathertalk_ui_vector_font_data.h"
#include "draw/vg_lite/lv_vg_lite_path.h"

typedef struct
{
    uint16_t pixel_size;
    float scale;
    lv_font_vector_glyph_data_t render_data;
} ft_vector_font_dsc_t;

static lv_vector_path_t **s_glyph_paths;

static const ft_vector_font_glyph_asset_t *glyph_find(uint32_t codepoint,
                                                       uint32_t *index_out)
{
    uint32_t low = 0U;
    uint32_t high = ft_vector_font_glyph_count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2U;
        if (ft_vector_font_glyphs[middle].codepoint < codepoint) low = middle + 1U;
        else high = middle;
    }
    if (low >= ft_vector_font_glyph_count ||
        ft_vector_font_glyphs[low].codepoint != codepoint)
        return RT_NULL;
    if (index_out != RT_NULL) *index_out = low;
    return &ft_vector_font_glyphs[low];
}

static lv_vector_path_t *glyph_path_get(uint32_t index)
{
    const ft_vector_font_glyph_asset_t *asset;
    lv_vector_path_t *path;
    lv_vg_lite_path_t *native_path;
    float bounds[4];

    if (index >= ft_vector_font_glyph_count) return RT_NULL;
    if (s_glyph_paths == RT_NULL)
    {
        s_glyph_paths = lv_malloc_zeroed(sizeof(*s_glyph_paths) * ft_vector_font_glyph_count);
        if (s_glyph_paths == RT_NULL) return RT_NULL;
    }
    if (s_glyph_paths[index] != RT_NULL) return s_glyph_paths[index];

    asset = &ft_vector_font_glyphs[index];
    if (asset->path_word_count == 0U) return RT_NULL;
    path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_HIGH);
    if (path == RT_NULL) return RT_NULL;
    bounds[0] = 0.0f;
    bounds[1] = 0.0f;
    bounds[2] = (float)(asset->x_max - asset->x_min);
    bounds[3] = (float)(asset->y_max - asset->y_min);
    native_path = lv_vg_lite_path_create_static(
        VG_LITE_S16, VG_LITE_HIGH,
        &ft_vector_font_path_data[asset->path_offset],
        (uint32_t)asset->path_word_count * sizeof(int16_t), bounds);
    if (native_path == RT_NULL ||
        !lv_draw_vg_lite_vector_path_attach_native(path, true, native_path, bounds))
    {
        if (native_path != RT_NULL) lv_vg_lite_path_destroy(native_path);
        lv_vector_path_delete(path);
        return RT_NULL;
    }
    lv_vector_path_set_immutable(path);
    s_glyph_paths[index] = path;
    return path;
}

static bool vector_font_get_glyph_dsc(const lv_font_t *font,
                                      lv_font_glyph_dsc_t *out,
                                      uint32_t codepoint,
                                      uint32_t next)
{
    const ft_vector_font_dsc_t *font_dsc = font->dsc;
    const ft_vector_font_glyph_asset_t *asset;
    uint32_t index;
    float scale;
    LV_UNUSED(next);

    asset = glyph_find(codepoint, &index);
    if (asset == RT_NULL) return false;
    scale = font_dsc->scale;
    out->adv_w = (uint16_t)LV_MAX(1, (int32_t)lroundf((float)asset->advance * scale));
    out->box_w = asset->path_word_count == 0U ? 0U :
                 (uint16_t)LV_MAX(1, (int32_t)ceilf((float)(asset->x_max - asset->x_min) * scale));
    out->box_h = asset->path_word_count == 0U ? 0U :
                 (uint16_t)LV_MAX(1, (int32_t)ceilf((float)(asset->y_max - asset->y_min) * scale));
    out->ofs_x = (int16_t)floorf((float)asset->x_min * scale);
    out->ofs_y = (int16_t)floorf((float)asset->y_min * scale);
    out->format = LV_FONT_GLYPH_FORMAT_VECTOR;
    out->is_placeholder = false;
    out->gid.index = index + 1U;
    out->entry = RT_NULL;
    return true;
}

static const void *vector_font_get_glyph_data(lv_font_glyph_dsc_t *glyph,
                                               lv_draw_buf_t *draw_buf)
{
    ft_vector_font_dsc_t *font_dsc = (ft_vector_font_dsc_t *)glyph->resolved_font->dsc;
    LV_UNUSED(draw_buf);
    if (glyph->gid.index == 0U) return RT_NULL;
    font_dsc->render_data.magic = LV_FONT_VECTOR_GLYPH_MAGIC;
    font_dsc->render_data.path = glyph_path_get(glyph->gid.index - 1U);
    font_dsc->render_data.scale = font_dsc->scale;
    return font_dsc->render_data.path != RT_NULL ? &font_dsc->render_data : RT_NULL;
}

#define FT_VECTOR_FONT_DSC(NAME, SIZE) \
    static ft_vector_font_dsc_t NAME##_dsc = { \
        .pixel_size = SIZE, .scale = (float)(SIZE) / 1000.0f \
    }; \
    const lv_font_t NAME = { \
        .get_glyph_dsc = vector_font_get_glyph_dsc, \
        .get_glyph_bitmap = vector_font_get_glyph_data, \
        .release_glyph = RT_NULL, \
        .line_height = (SIZE) + 2, \
        .base_line = ((SIZE) + 5) / 6, \
        .subpx = LV_FONT_SUBPX_NONE, \
        .kerning = LV_FONT_KERNING_NONE, \
        .glyph_dsc_cacheable = 1, \
        .underline_position = -2, \
        .underline_thickness = 1, \
        .dsc = &NAME##_dsc, \
        .fallback = &lv_font_montserrat_##SIZE, \
        .user_data = RT_NULL \
    }

FT_VECTOR_FONT_DSC(feathertalk_vector_font_12, 12);
FT_VECTOR_FONT_DSC(feathertalk_vector_font_14, 14);
FT_VECTOR_FONT_DSC(feathertalk_vector_font_16, 16);
FT_VECTOR_FONT_DSC(feathertalk_vector_font_22, 22);
