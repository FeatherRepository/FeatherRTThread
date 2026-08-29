#ifndef FEATHERTALK_UI_LAYOUT_H
#define FEATHERTALK_UI_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef struct
{
    int32_t screen_width;
    int32_t screen_height;
    int32_t scale_percent;
    int32_t status_bar_height;
    int32_t nav_bar_height;
    int32_t notification_height;
    int32_t page_padding;
    int32_t home_padding;
    int32_t section_gap;
    int32_t control_height;
    int32_t list_row_height;
    int32_t keyboard_height;
    int32_t tile_gap;
    int32_t tile_height;
    int32_t tile_column_width;
    uint8_t tile_columns;
    bool compact;
    bool landscape;
} ft_ui_layout_t;

void ft_layout_init(lv_display_t *display);
const ft_ui_layout_t *ft_layout_get(void);
int32_t ft_layout_px(int32_t design_px);
uint16_t ft_layout_icon_size(uint16_t design_size);
const lv_font_t *ft_layout_font(int32_t design_size);
int32_t ft_layout_tile_width(bool wide);
bool ft_layout_profiles_self_test(void);
bool ft_layout_control_fits(lv_obj_t *control);

#endif /* FEATHERTALK_UI_LAYOUT_H */
