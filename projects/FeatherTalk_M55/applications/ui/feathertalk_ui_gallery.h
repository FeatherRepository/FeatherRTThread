#ifndef FEATHERTALK_UI_GALLERY_H
#define FEATHERTALK_UI_GALLERY_H

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FT_GALLERY_SOURCE_FLASH = 0,
    FT_GALLERY_SOURCE_SD,
    FT_GALLERY_SOURCE_COUNT
} ft_gallery_source_t;

/* A filesystem image decoded once into a display-native RGB565 buffer.  The
 * buffer owns no filesystem handle, so repainting it never reopens Flash/SD
 * and it can be rendered by the accelerated image path. */
typedef struct
{
    lv_draw_buf_t *draw_buf;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t non_black_pixels;
    uint32_t checksum;
} ft_gallery_rendered_image_t;

/* Validate one image anywhere below /flash or /sdcard and return its
 * normalized LVGL file path.  The decoder is actually opened and probed. */
bool ft_gallery_validate_image_path(const char *native_path,
                                    ft_gallery_source_t *source_out,
                                    char *lv_path, size_t lv_path_size,
                                    lv_image_header_t *verified_header);

bool ft_gallery_render_image_path(const char *native_path,
                                  uint32_t maximum_width,
                                  uint32_t maximum_height,
                                  ft_gallery_rendered_image_t *rendered);
void ft_gallery_release_rendered_image(ft_gallery_rendered_image_t *rendered);
bool ft_gallery_can_open_file(const char *native_path);
bool ft_gallery_request_open_file(const char *native_path);

lv_obj_t *ft_gallery_create_page(lv_obj_t *parent);
void ft_gallery_page_enter(void);
bool ft_gallery_page_back(void);
void ft_gallery_page_leave(void);
void ft_gallery_apply_language(void);

#ifdef FEATHERTALK_UI_TEST_MODE
lv_obj_t *ft_gallery_test_get_source_button(size_t index);
lv_obj_t *ft_gallery_test_get_refresh_button(void);
lv_obj_t *ft_gallery_test_get_entry(size_t index);
lv_obj_t *ft_gallery_test_get_first_image(void);
lv_obj_t *ft_gallery_test_get_previous_button(void);
lv_obj_t *ft_gallery_test_get_next_button(void);
lv_obj_t *ft_gallery_test_get_close_button(void);
lv_obj_t *ft_gallery_test_get_wallpaper_button(void);
lv_obj_t *ft_gallery_test_get_delete_button(void);
lv_obj_t *ft_gallery_test_get_delete_cancel(void);
size_t ft_gallery_test_entry_count(void);
size_t ft_gallery_test_image_count(void);
size_t ft_gallery_test_selected_source(void);
const char *ft_gallery_test_current_path(void);
const char *ft_gallery_test_current_file(void);
const char *ft_gallery_test_decoder_path(void);
bool ft_gallery_test_browser_visible(void);
bool ft_gallery_test_viewer_visible(void);
bool ft_gallery_test_preview_loading(void);
bool ft_gallery_test_entry_hit_target(size_t index);
bool ft_gallery_test_current_image_valid(void);
bool ft_gallery_test_current_image_verified(void);
bool ft_gallery_test_current_image_cached(void);
uint32_t ft_gallery_test_current_image_non_black_pixels(void);
uint32_t ft_gallery_test_current_image_checksum(void);
bool ft_gallery_test_source_available(size_t index);
bool ft_gallery_test_path_safe(void);
bool ft_gallery_test_uses_dedicated_collection(void);
bool ft_gallery_test_entries_decodable(void);
bool ft_gallery_test_thumbnails_ready(void);
bool ft_gallery_test_delete_confirmation_visible(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_UI_GALLERY_H */
