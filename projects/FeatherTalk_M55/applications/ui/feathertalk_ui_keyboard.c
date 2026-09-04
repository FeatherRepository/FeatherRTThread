#include "feathertalk_ui_keyboard.h"
#include "feathertalk_ui_layout.h"

lv_obj_t *ft_ui_keyboard_create(lv_obj_t *parent, lv_obj_t *textarea)
{
    lv_obj_t *keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(keyboard, lv_pct(100), ft_layout_get()->keyboard_height);
    lv_obj_set_flex_grow(keyboard, 0);
    lv_obj_set_style_radius(keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
    lv_keyboard_set_textarea(keyboard, textarea);
    return keyboard;
}

int32_t ft_ui_keyboard_toolbar_height(void) { return ft_layout_px(36); }
int32_t ft_ui_keyboard_tray_height(void)
{
    /* Optional collapse toolbar and its one-pixel top separator sit outside
     * the key area, so keys retain exactly the same height on every page. */
    return ft_layout_get()->keyboard_height + ft_ui_keyboard_toolbar_height() + 1;
}
