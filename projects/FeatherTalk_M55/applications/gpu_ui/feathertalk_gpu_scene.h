#ifndef FEATHERTALK_GPU_SCENE_H
#define FEATHERTALK_GPU_SCENE_H

#include <stdbool.h>
#include <stdint.h>
#include "feather_ui.h"

typedef enum
{
    FT_GPU_PAGE_HOME = 0,
    FT_GPU_PAGE_SEARCH,
    FT_GPU_PAGE_SYSTEM,
    FT_GPU_PAGE_SETTINGS,
    FT_GPU_PAGE_MEDIA,
    FT_GPU_PAGE_RECORDER,
    FT_GPU_PAGE_GALLERY,
    FT_GPU_PAGE_FILES,
    FT_GPU_PAGE_ABOUT,
    FT_GPU_PAGE_SETTINGS_DISPLAY,
    FT_GPU_PAGE_SETTINGS_AUDIO,
    FT_GPU_PAGE_SETTINGS_WIFI,
    FT_GPU_PAGE_SETTINGS_BLUETOOTH,
    FT_GPU_PAGE_SETTINGS_STORAGE,
    FT_GPU_PAGE_SETTINGS_USB,
    FT_GPU_PAGE_SETTINGS_TIME_LANGUAGE,
    FT_GPU_PAGE_SETTINGS_PERSONALIZATION,
    FT_GPU_PAGE_COUNT
} ft_gpu_page_t;

int ft_gpu_scene_init(uint16_t screen_width, uint16_t screen_height);
void ft_gpu_scene_collect(fui_painter_t *painter, void *user_data);
bool ft_gpu_scene_event(const fui_event_t *event, void *user_data);
ft_gpu_page_t ft_gpu_scene_current_page(void);
uint8_t ft_gpu_scene_route_depth(void);
bool ft_gpu_scene_select_visible(void);
bool ft_gpu_scene_dialog_visible(void);
int ft_gpu_scene_open(ft_gpu_page_t page);
int ft_gpu_scene_run_test(void);

#endif /* FEATHERTALK_GPU_SCENE_H */
