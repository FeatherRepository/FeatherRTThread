#include <rtthread.h>
#include "feathertalk_ui_layout.h"
#include "feathertalk_ui_font.h"

#define FT_LAYOUT_DESIGN_WIDTH   480
#define FT_LAYOUT_DESIGN_HEIGHT  800
#define FT_LAYOUT_SCALE_MIN       65
#define FT_LAYOUT_SCALE_MAX      150

static ft_ui_layout_t s_layout;

static int32_t clamp_i32(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int32_t scaled_px(int32_t design_px, int32_t scale_percent)
{
    if (design_px <= 0) return 0;
    return (design_px * scale_percent + 50) / 100;
}

static bool calculate_layout(int32_t width, int32_t height, ft_ui_layout_t *layout)
{
    int32_t width_scale;
    int32_t height_scale;
    int32_t content_width;
    int32_t gaps_width;
    int32_t icon_small;

    if (layout == RT_NULL || width < 200 || height < 240) return false;
    width_scale = width * 100 / FT_LAYOUT_DESIGN_WIDTH;
    height_scale = height * 100 / FT_LAYOUT_DESIGN_HEIGHT;
    layout->scale_percent = clamp_i32(width_scale < height_scale ? width_scale : height_scale,
                                      FT_LAYOUT_SCALE_MIN, FT_LAYOUT_SCALE_MAX);
    layout->screen_width = width;
    layout->screen_height = height;
    layout->compact = width < 360 || height < 560;
    layout->landscape = width > height;
    layout->page_padding = scaled_px(24, layout->scale_percent);
    layout->home_padding = scaled_px(16, layout->scale_percent);
    layout->section_gap = scaled_px(18, layout->scale_percent);
    layout->tile_gap = clamp_i32(scaled_px(8, layout->scale_percent), 4, 16);
    layout->control_height = clamp_i32(scaled_px(52, layout->scale_percent), 36, 78);
    layout->list_row_height = clamp_i32(scaled_px(68, layout->scale_percent), 46, 102);
    layout->tile_height = clamp_i32(scaled_px(116, layout->scale_percent), 72, 174);
    icon_small = layout->scale_percent <= 116 ? 24 : 32;
    layout->status_bar_height = clamp_i32(scaled_px(36, layout->scale_percent),
                                          icon_small + 4, 54);
    layout->nav_bar_height = clamp_i32(scaled_px(64, layout->scale_percent), 48, 96);
    layout->notification_height = clamp_i32(scaled_px(460, layout->scale_percent),
                                             180, height - layout->status_bar_height -
                                                  layout->nav_bar_height - scaled_px(16, layout->scale_percent));
    layout->keyboard_height = clamp_i32(scaled_px(235, layout->scale_percent),
                                         140, (height - layout->status_bar_height -
                                               layout->nav_bar_height) / 2);
    if (width < 360)
        layout->tile_columns = 2U;
    else if (width < 640)
        layout->tile_columns = 3U;
    else
        layout->tile_columns = 4U;
    content_width = width - 2 * layout->home_padding;
    gaps_width = ((int32_t)layout->tile_columns - 1) * layout->tile_gap;
    layout->tile_column_width = (content_width - gaps_width) / layout->tile_columns;
    return layout->tile_column_width >= 72 &&
           layout->status_bar_height + layout->nav_bar_height < height &&
           layout->keyboard_height > 0;
}

void ft_layout_init(lv_display_t *display)
{
    int32_t width;
    int32_t height;
    if (display == RT_NULL) return;
    width = lv_display_get_horizontal_resolution(display);
    height = lv_display_get_vertical_resolution(display);
    if (!calculate_layout(width, height, &s_layout))
    {
        rt_kprintf("[FeatherTalk UI] unsupported display geometry: %ldx%ld\n",
                   (long)width, (long)height);
        RT_ASSERT(0);
    }
    rt_kprintf("[FeatherTalk UI] responsive layout: %ldx%ld scale=%ld%% compact=%d "
               "landscape=%d tiles=%u column=%ld\n",
               (long)s_layout.screen_width, (long)s_layout.screen_height,
               (long)s_layout.scale_percent, s_layout.compact ? 1 : 0,
               s_layout.landscape ? 1 : 0, s_layout.tile_columns,
               (long)s_layout.tile_column_width);
}

const ft_ui_layout_t *ft_layout_get(void)
{
    return &s_layout;
}

int32_t ft_layout_px(int32_t design_px)
{
    return scaled_px(design_px, s_layout.scale_percent);
}

uint16_t ft_layout_icon_size(uint16_t design_size)
{
    int32_t scaled = ft_layout_px((int32_t)design_size);
    if (scaled <= 28) return 24U;
    if (scaled <= 40) return 32U;
    return 48U;
}

const lv_font_t *ft_layout_font(int32_t design_size)
{
    int32_t size = ft_layout_px(design_size);
    if (size <= 13) return &feathertalk_vector_font_12;
    if (size <= 15) return &feathertalk_vector_font_14;
    if (size <= 18) return &feathertalk_vector_font_16;
    return &feathertalk_vector_font_22;
}

int32_t ft_layout_tile_width(bool wide)
{
    if (!wide) return s_layout.tile_column_width;
    return s_layout.tile_column_width * 2 + s_layout.tile_gap;
}

bool ft_layout_profiles_self_test(void)
{
    static const int32_t profiles[][2] =
    {
        {240, 320}, {320, 480}, {480, 800}, {720, 1280}, {800, 480}
    };
    ft_ui_layout_t layout;
    size_t i;
    for (i = 0U; i < sizeof(profiles) / sizeof(profiles[0]); i++)
    {
        if (!calculate_layout(profiles[i][0], profiles[i][1], &layout)) return false;
        if (layout.tile_column_width * layout.tile_columns +
            ((int32_t)layout.tile_columns - 1) * layout.tile_gap >
            layout.screen_width - 2 * layout.home_padding) return false;
    }
    return true;
}

bool ft_layout_control_fits(lv_obj_t *control)
{
    lv_obj_t *parent;
    int32_t width;
    int32_t height;
    int32_t parent_width;
    if (control == RT_NULL || !lv_obj_is_valid(control)) return false;
    parent = lv_obj_get_parent(control);
    if (parent == RT_NULL || !lv_obj_is_valid(parent)) return false;
    lv_obj_update_layout(control);
    width = lv_obj_get_width(control);
    height = lv_obj_get_height(control);
    parent_width = lv_obj_get_content_width(parent);
    return width > 0 && height > 0 && parent_width > 0 && width <= parent_width + 2;
}
