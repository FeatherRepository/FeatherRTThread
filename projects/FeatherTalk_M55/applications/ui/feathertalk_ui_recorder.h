#ifndef FEATHERTALK_UI_RECORDER_H
#define FEATHERTALK_UI_RECORDER_H

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

lv_obj_t *ft_recorder_page_create(lv_obj_t *parent);
void ft_recorder_page_enter(void);
void ft_recorder_page_leave(void);
void ft_recorder_page_apply_language(void);

#ifdef FEATHERTALK_UI_TEST_MODE
bool ft_recorder_page_test_ready(void);
bool ft_recorder_page_test_slots_clear(void);
lv_obj_t *ft_recorder_page_test_get_device(size_t index);
lv_obj_t *ft_recorder_page_test_get_record_button(void);
size_t ft_recorder_page_test_selected_device(void);
#endif

#endif /* FEATHERTALK_UI_RECORDER_H */
