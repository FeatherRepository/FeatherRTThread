#ifndef FEATHERTALK_UI_KEYBOARD_H
#define FEATHERTALK_UI_KEYBOARD_H
#include "lvgl.h"

/* Product-wide default: fixed proportional key area, never flex-grow.
 * Parents own positioning, hide/cancel semantics and optional accessory bar. */
lv_obj_t *ft_ui_keyboard_create(lv_obj_t *parent, lv_obj_t *textarea);
int32_t ft_ui_keyboard_toolbar_height(void);
int32_t ft_ui_keyboard_tray_height(void);
#endif
