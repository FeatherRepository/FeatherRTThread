#ifndef FEATHERTALK_GPU_IMAGE_H
#define FEATHERTALK_GPU_IMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "feather_ui.h"

typedef enum
{
    FT_GPU_IMAGE_GALLERY = 0,
    FT_GPU_IMAGE_WALLPAPER,
    FT_GPU_IMAGE_SLOT_COUNT
} ft_gpu_image_slot_t;

typedef enum
{
    FT_GPU_IMAGE_EMPTY = 0,
    FT_GPU_IMAGE_LOADING,
    FT_GPU_IMAGE_READY,
    FT_GPU_IMAGE_ERROR
} ft_gpu_image_state_t;

typedef struct
{
    ft_gpu_image_state_t state;
    fui_image_rgb565_t image;
    uint16_t source_width;
    uint16_t source_height;
    uint32_t decode_ms;
    char path[256];
    char error[40];
} ft_gpu_image_info_t;

int ft_gpu_image_init(void);
int ft_gpu_image_request(ft_gpu_image_slot_t slot, const char *path,
                         uint16_t maximum_width, uint16_t maximum_height);
void ft_gpu_image_clear(ft_gpu_image_slot_t slot);
bool ft_gpu_image_get(ft_gpu_image_slot_t slot, ft_gpu_image_info_t *info);

#endif /* FEATHERTALK_GPU_IMAGE_H */
