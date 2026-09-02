#include <rtthread.h>
#include "feathertalk_ui_font.h"
#include "feathertalk_ui_vector_font_data.h"
#include "draw/vg_lite/lv_vg_lite_path.h"

typedef struct
{
    uint16_t pixel_size;
    float scale;
    lv_font_vector_glyph_data_t render_data;
    lv_font_t font;
    bool initialized;
} ft_vector_font_dsc_t;

#define FT_VECTOR_FONT_MIN_SIZE 8U
#define FT_VECTOR_FONT_MAX_SIZE 48U

static ft_vector_font_dsc_t s_fonts[FT_VECTOR_FONT_MAX_SIZE -
                                     FT_VECTOR_FONT_MIN_SIZE + 1U];

static lv_vector_path_t **s_glyph_paths;

typedef struct
{
    int32_t x_min;
    int32_t y_min;
    int32_t x_max;
    int32_t y_max;
} ft_vector_font_pixel_bounds_t;

static int32_t floor_div_i32(int32_t numerator, int32_t denominator)
{
    int32_t quotient = numerator / denominator;
    int32_t remainder = numerator % denominator;
    if (remainder != 0 && numerator < 0) quotient--;
    return quotient;
}

static int32_t ceil_div_i32(int32_t numerator, int32_t denominator)
{
    int32_t quotient = numerator / denominator;
    int32_t remainder = numerator % denominator;
    if (remainder != 0 && numerator > 0) quotient++;
    return quotient;
}

static void glyph_pixel_bounds(const ft_vector_font_glyph_asset_t *asset,
                               uint16_t pixel_size,
                               ft_vector_font_pixel_bounds_t *bounds)
{
    int32_t units = (int32_t)ft_vector_font_units_per_em;
    bounds->x_min = floor_div_i32((int32_t)asset->x_min * pixel_size, units);
    bounds->y_min = floor_div_i32((int32_t)asset->y_min * pixel_size, units);
    bounds->x_max = ceil_div_i32((int32_t)asset->x_max * pixel_size, units);
    bounds->y_max = ceil_div_i32((int32_t)asset->y_max * pixel_size, units);
}

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
        !lv_draw_vg_lite_vector_path_attach_native(path, true, false,
                                                   native_path, bounds))
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
    ft_vector_font_pixel_bounds_t bounds;
    uint32_t index;
    LV_UNUSED(next);

    asset = glyph_find(codepoint, &index);
    if (asset == RT_NULL) return false;
    glyph_pixel_bounds(asset, font_dsc->pixel_size, &bounds);
    out->adv_w = (uint16_t)LV_MAX(1, ((int32_t)asset->advance *
                                      font_dsc->pixel_size +
                                      ft_vector_font_units_per_em / 2U) /
                                     ft_vector_font_units_per_em);
    out->box_w = asset->path_word_count == 0U ? 0U :
                 (uint16_t)LV_MAX(1, bounds.x_max - bounds.x_min);
    out->box_h = asset->path_word_count == 0U ? 0U :
                 (uint16_t)LV_MAX(1, bounds.y_max - bounds.y_min);
    out->ofs_x = (int16_t)bounds.x_min;
    out->ofs_y = (int16_t)bounds.y_min;
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

static const lv_font_t *vector_font_fallback(uint16_t size)
{
    if (size <= 13U) return &lv_font_montserrat_12;
    if (size <= 15U) return &lv_font_montserrat_14;
    if (size <= 18U) return &lv_font_montserrat_16;
    if (size <= 21U) return &lv_font_montserrat_20;
    if (size <= 23U) return &lv_font_montserrat_22;
    return &lv_font_montserrat_24;
}

static void vector_font_initialize(ft_vector_font_dsc_t *descriptor,
                                   uint16_t pixel_size)
{
    int32_t em_height = (int32_t)ft_vector_font_typo_ascender -
                        (int32_t)ft_vector_font_typo_descender;
    int32_t leading = ((int32_t)pixel_size + 7) / 8;
    int32_t line_height = (em_height * pixel_size +
                           ft_vector_font_units_per_em - 1U) /
                          ft_vector_font_units_per_em + leading;
    int32_t baseline = ((-(int32_t)ft_vector_font_typo_descender * pixel_size +
                         ft_vector_font_units_per_em / 2U) /
                        ft_vector_font_units_per_em) + leading / 2;

    descriptor->pixel_size = pixel_size;
    descriptor->scale = (float)pixel_size /
                        (float)ft_vector_font_units_per_em;
    descriptor->font = (lv_font_t) {
        .get_glyph_dsc = vector_font_get_glyph_dsc,
        .get_glyph_bitmap = vector_font_get_glyph_data,
        .release_glyph = RT_NULL,
        .line_height = (uint16_t)line_height,
        .base_line = (int16_t)baseline,
        .subpx = LV_FONT_SUBPX_NONE,
        .kerning = LV_FONT_KERNING_NONE,
        .glyph_dsc_cacheable = 1,
        .underline_position = -2,
        .underline_thickness = 1,
        .dsc = descriptor,
        .fallback = vector_font_fallback(pixel_size),
        .user_data = RT_NULL
    };
    descriptor->initialized = true;
}

const lv_font_t *ft_vector_font_get(uint16_t pixel_size)
{
    ft_vector_font_dsc_t *descriptor;
    if (pixel_size < FT_VECTOR_FONT_MIN_SIZE) pixel_size = FT_VECTOR_FONT_MIN_SIZE;
    if (pixel_size > FT_VECTOR_FONT_MAX_SIZE) pixel_size = FT_VECTOR_FONT_MAX_SIZE;
    descriptor = &s_fonts[pixel_size - FT_VECTOR_FONT_MIN_SIZE];
    if (!descriptor->initialized)
        vector_font_initialize(descriptor, pixel_size);
    return &descriptor->font;
}

bool ft_vector_font_metrics_self_test(void)
{
    uint16_t pixel_size;
    uint32_t index;

    if (ft_vector_font_units_per_em == 0U) return false;
    for (pixel_size = FT_VECTOR_FONT_MIN_SIZE;
         pixel_size <= FT_VECTOR_FONT_MAX_SIZE; pixel_size++)
    {
        const lv_font_t *font = ft_vector_font_get(pixel_size);
        for (index = 0U; index < ft_vector_font_glyph_count; index++)
        {
            const ft_vector_font_glyph_asset_t *asset = &ft_vector_font_glyphs[index];
            ft_vector_font_pixel_bounds_t bounds;
            lv_font_glyph_dsc_t glyph = {0};

            glyph_pixel_bounds(asset, pixel_size, &bounds);
            if (!vector_font_get_glyph_dsc(font, &glyph, asset->codepoint, 0U))
                return false;
            if (glyph.ofs_x != bounds.x_min || glyph.ofs_y != bounds.y_min)
                return false;
            if (asset->path_word_count == 0U)
            {
                if (glyph.box_w != 0U || glyph.box_h != 0U) return false;
                continue;
            }
            if ((int32_t)glyph.ofs_x + glyph.box_w != bounds.x_max ||
                (int32_t)glyph.ofs_y + glyph.box_h != bounds.y_max ||
                glyph.box_w == 0U || glyph.box_h == 0U)
                return false;
            /* Re-evaluate LVGL's actual vertical placement equation.  The
             * visible box must start at baseline-yMax and end immediately
             * after baseline-yMin for every glyph, regardless of its shape. */
            {
                int32_t baseline_y = (int32_t)font->line_height - font->base_line;
                int32_t top = baseline_y - glyph.box_h - glyph.ofs_y;
                int32_t bottom_exclusive = top + glyph.box_h;
                if (top != baseline_y - bounds.y_max ||
                    bottom_exclusive != baseline_y - bounds.y_min)
                    return false;
            }
        }
    }
    return true;
}

#ifdef RT_USING_FINSH
static int feather_font_metrics_test(void)
{
    bool passed = ft_vector_font_metrics_self_test();
    rt_kprintf("FeatherTalk vector-font metrics: %s (%lu glyphs, %u..%u px)\n",
               passed ? "PASS" : "FAIL",
               (unsigned long)ft_vector_font_glyph_count,
               FT_VECTOR_FONT_MIN_SIZE, FT_VECTOR_FONT_MAX_SIZE);
    return passed ? 0 : -RT_ERROR;
}
MSH_CMD_EXPORT(feather_font_metrics_test,
               Validate every vector glyph metric at every supported size.);
#endif
