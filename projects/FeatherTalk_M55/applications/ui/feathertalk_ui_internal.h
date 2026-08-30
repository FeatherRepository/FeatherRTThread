#ifndef FEATHERTALK_UI_INTERNAL_H
#define FEATHERTALK_UI_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <feathertalk/ipc_protocol.h>
#include "lvgl.h"
#include "feathertalk_ui_icons.h"
#include "feathertalk_ui_layout.h"
#include "feathertalk_usb.h"

typedef enum
{
    FT_PAGE_HOME = 0,
    FT_PAGE_SEARCH,
    FT_PAGE_SYSTEM,
    FT_PAGE_SETTINGS,
    FT_PAGE_MEDIA,
    FT_PAGE_MESSAGES,
    FT_PAGE_FILES,
    FT_PAGE_ABOUT,
    FT_PAGE_SETTINGS_DISPLAY,
    FT_PAGE_SETTINGS_WIFI,
    FT_PAGE_SETTINGS_BLUETOOTH,
    FT_PAGE_SETTINGS_STORAGE,
    FT_PAGE_SETTINGS_USB,
    FT_PAGE_SETTINGS_TIME_LANGUAGE,
    FT_PAGE_SETTINGS_PERSONALIZATION,
    FT_PAGE_COUNT
} ft_page_id_t;

typedef struct
{
    ft_page_id_t id;
    const char *title;
    lv_obj_t *(*create)(lv_obj_t *parent);
    void (*on_enter)(void);
    bool (*on_back)(void);
    void (*on_leave)(void);
} ft_page_definition_t;

typedef struct
{
    const char *name;
    uint8_t column_span;
    uint8_t row_span;
    uint8_t opacity;
    ft_icon_id_t pattern_icon;
} ft_tile_common_properties_t;

typedef void (*ft_tile_live_content_cb_t)(lv_obj_t *content_host,
                                          uint32_t frame, void *context);

typedef struct
{
    ft_icon_id_t app_icon;
    bool loop_enabled;
    uint32_t loop_period_ms;
    ft_tile_live_content_cb_t live_content;
    void *live_context;
} ft_tile_private_properties_t;

typedef struct
{
    ft_page_id_t page_id;
    ft_tile_common_properties_t tile;
    ft_tile_private_properties_t app;
} ft_app_descriptor_t;

typedef enum
{
    FT_ACCENT_BACKGROUND = 0,
    FT_ACCENT_TEXT,
    FT_ACCENT_IMAGE,
    FT_ACCENT_BORDER
} ft_accent_target_t;

typedef enum
{
    FT_NAV_BACK = 0,
    FT_NAV_HOME,
    FT_NAV_SEARCH,
    FT_NAV_COUNT
} ft_nav_button_id_t;

typedef enum
{
    FT_BACKGROUND_BLACK = 0,
    FT_BACKGROUND_DARK,
    FT_BACKGROUND_ACCENT,
    FT_BACKGROUND_COUNT
} ft_background_mode_t;

typedef enum
{
    FT_LANGUAGE_ZH_CN = 0,
    FT_LANGUAGE_EN_US,
    FT_LANGUAGE_COUNT
} ft_language_t;

typedef struct
{
    uint32_t accent_rgb;
    uint8_t tile_opa;
    ft_background_mode_t background;
    bool use_24_hour;
    int16_t timezone_offset_minutes;
    ft_language_t language;
    uint32_t revision;
} ft_ui_preferences_t;

typedef struct
{
    uint32_t fps;
    uint32_t refresh_fps;
    uint32_t refresh_count;
    uint32_t render_count;
    uint32_t flush_count;
    uint32_t flushes_per_second;
    uint32_t flushed_pixels;
    uint32_t flushed_pixels_per_second;
    uint32_t render_time_last_ms;
    uint32_t render_time_max_ms;
    uint32_t heap_total;
    uint32_t heap_used;
    uint32_t heap_max_used;
    uint32_t peak_ui_objects;
    int32_t last_route_object_delta;
    int32_t last_route_heap_delta;
} ft_ui_metrics_t;

int ft_router_init(lv_obj_t *host);
int ft_router_push(ft_page_id_t page_id);
bool ft_router_back(void);
void ft_router_home(void);
int ft_router_refresh_all(void);
size_t ft_router_depth(void);
ft_page_id_t ft_router_current_page(void);

const ft_page_definition_t *ft_pages_find(ft_page_id_t page_id);
const ft_app_descriptor_t *ft_apps_get(size_t *count);
void ft_pages_show_start(void);
void ft_pages_show_all_apps(void);
void ft_pages_open_search(void);
void ft_pages_apply_preferences(void);
void ft_pages_apply_language(void);
void ft_pages_update_system_status(const char *system_text, const char *metrics_text);
void ft_pages_live_tile_update(const char *line);

int ft_tiles_create(lv_obj_t *container, const ft_app_descriptor_t *apps, size_t count);
void ft_tiles_exit_edit(void);
void ft_tiles_apply_opacity(uint8_t global_opacity);
void ft_tiles_set_external_text(ft_page_id_t page_id, const char *text);
void ft_tiles_set_localized_name(ft_page_id_t page_id,
                                 const char *english_name,
                                 const char *chinese_name,
                                 const char *display_name);
void ft_tiles_set_live_loop(ft_page_id_t page_id, bool enabled);
bool ft_tiles_editing(void);
size_t ft_tiles_selected(void);

void ft_preferences_init(void);
const ft_ui_preferences_t *ft_preferences_get(void);
void ft_preferences_set_accent(uint32_t rgb);
void ft_preferences_set_tile_opa(uint8_t opa);
void ft_preferences_set_background(ft_background_mode_t background);
void ft_preferences_set_24_hour(bool enabled);
void ft_preferences_set_timezone(int16_t offset_minutes);
void ft_preferences_set_language(ft_language_t language);
const char *ft_preferences_text(const char *zh_cn, const char *en_us);
void ft_preferences_format_clock(uint32_t seconds, bool utc_time,
                                 char *buffer, size_t buffer_size);
void ft_preferences_reset(void);

void ft_metrics_init(lv_display_t *display, lv_obj_t *root);
void ft_metrics_get(ft_ui_metrics_t *metrics);
void ft_metrics_route_baseline(void);
void ft_metrics_route_check(void);
void ft_metrics_print_status(void);

#ifdef FEATHERTALK_UI_TEST_MODE
lv_obj_t *ft_tiles_test_get_object(size_t index);
bool ft_tiles_test_editing(void);
size_t ft_tiles_test_selected(void);
size_t ft_tiles_test_handle_count(void);
bool ft_tiles_test_handle_geometry(void);
bool ft_tiles_test_move(size_t app_index, size_t target_index);
bool ft_tiles_test_move_nearest(size_t app_index);
bool ft_tiles_test_move_scrolled(size_t app_index);
bool ft_tiles_test_move_edge_autoscroll(size_t app_index);
bool ft_tiles_test_layout_settled(void);
bool ft_tiles_test_resize(size_t app_index, uint8_t columns, uint8_t rows);
bool ft_tiles_test_resize_collision(void);
bool ft_tiles_test_resize_boundary(void);
bool ft_tiles_test_resize_edge_autoscroll(size_t app_index);
bool ft_tiles_test_resize_anchors(size_t app_index);
size_t ft_tiles_test_order(size_t app_index);
uint8_t ft_tiles_test_columns(size_t app_index);
uint8_t ft_tiles_test_rows(size_t app_index);
bool ft_tiles_test_layout_valid(void);
bool ft_tiles_test_restore_layout(void);
bool ft_tiles_test_set_common(size_t app_index, const char *name,
                              uint8_t opacity, ft_icon_id_t pattern_icon);
const char *ft_tiles_test_name(size_t app_index);
uint8_t ft_tiles_test_opacity(size_t app_index);
ft_icon_id_t ft_tiles_test_pattern(size_t app_index);
const char *ft_tiles_test_live_text(size_t app_index);
bool ft_tiles_test_live_advance(size_t app_index);
bool ft_tiles_test_live_enabled(size_t app_index);

lv_obj_t *ft_ui_test_get_nav_button(ft_nav_button_id_t button_id);
lv_obj_t *ft_ui_test_get_status_bar(void);
bool ft_ui_test_status_monitor_visible(void);
lv_obj_t *ft_ui_test_get_notification_panel(void);
int32_t ft_ui_test_notification_y(void);
void ft_ui_test_notification_drag_begin(int32_t pointer_y);
void ft_ui_test_notification_drag_move(int32_t pointer_y);
void ft_ui_test_notification_drag_end(void);
void ft_ui_test_notification_fling(int32_t start_y, int32_t end_y,
                                   uint32_t duration_ms, uint32_t release_delay_ms);
bool ft_ui_test_notification_mask_visible(void);
lv_obj_t *ft_ui_test_get_notification_mask(void);
lv_obj_t *ft_ui_test_get_notification_clear(void);
lv_obj_t *ft_ui_test_get_quick_button(feathertalk_quick_control_t control);
lv_obj_t *ft_ui_test_get_brightness_slider(void);
bool ft_ui_test_quick_available(feathertalk_quick_control_t control);
bool ft_ui_test_quick_enabled(feathertalk_quick_control_t control);
bool ft_ui_test_quick_connected(feathertalk_quick_control_t control);
uint8_t ft_ui_test_quick_signal(void);
bool ft_ui_test_status_radio_icons_present(void);
bool ft_ui_test_language_surface(ft_language_t language);
ft_icon_id_t ft_ui_test_wifi_signal_icon(bool connected, uint8_t signal_percent);
uint8_t ft_ui_test_brightness(void);
size_t ft_ui_test_notification_count(void);
size_t ft_ui_test_notification_unread(void);
bool ft_ui_test_notification_remove(size_t index);
void ft_ui_test_notification_reset(void);
uint32_t ft_ui_test_notification_drag_applied(void);
uint32_t ft_ui_test_notification_drag_skipped(void);
uint32_t ft_ui_test_notification_mask_applied(void);
uint32_t ft_ui_test_notification_render_count(void);
lv_obj_t *ft_ui_test_get_alert_button(void);
bool ft_ui_test_notification_is_visible(void);
lv_obj_t *ft_pages_test_get_start_button(size_t app_index);
bool ft_pages_test_icon_assignments_unique(void);
lv_obj_t *ft_pages_test_get_apps_button(size_t app_index);
lv_obj_t *ft_pages_test_get_accent_button(size_t color_index);
lv_obj_t *ft_pages_test_get_settings_search_box(void);
lv_obj_t *ft_pages_test_get_settings_keyboard_hide(void);
bool ft_pages_test_settings_keyboard_visible(void);
bool ft_pages_test_settings_keyboard_overlay_ok(void);
bool ft_pages_test_system_info_complete(void);
bool ft_pages_test_language_surface(ft_language_t language);
lv_obj_t *ft_pages_test_get_settings_result(size_t index);
size_t ft_pages_test_settings_count(void);
size_t ft_pages_test_settings_visible_count(void);
ft_page_id_t ft_pages_test_settings_page_id(size_t index);
lv_obj_t *ft_pages_test_get_settings_brightness(void);
lv_obj_t *ft_pages_test_get_usb_role_button(ft_usb_role_t role);
lv_obj_t *ft_pages_test_get_usb_function_button(ft_usb_function_t function);
lv_obj_t *ft_pages_test_get_usb_stop_button(void);
bool ft_pages_test_usb_state_valid(void);
lv_obj_t *ft_pages_test_get_storage_format_button(void);
lv_obj_t *ft_pages_test_get_storage_browse_button(void);
lv_obj_t *ft_pages_test_get_storage_device_button(size_t index);
size_t ft_pages_test_storage_device_count(void);
size_t ft_pages_test_storage_selected_device(void);
size_t ft_pages_test_storage_action_target(void);
lv_obj_t *ft_pages_test_get_storage_capacity_track(void);
bool ft_pages_test_storage_visual_valid(void);
lv_obj_t *ft_pages_test_get_storage_confirm_cancel(void);
lv_obj_t *ft_pages_test_get_storage_confirm_continue(void);
uint8_t ft_pages_test_storage_confirm_stage(void);
bool ft_pages_test_storage_state_valid(void);
lv_obj_t *ft_pages_test_get_time_format_button(size_t index);
lv_obj_t *ft_pages_test_get_timezone_dropdown(void);
lv_obj_t *ft_pages_test_get_language_button(size_t index);
size_t ft_pages_test_timezone_count(void);
int16_t ft_pages_test_timezone_offset(size_t index);
lv_obj_t *ft_pages_test_get_media_button(void);
const char *ft_pages_test_get_media_label(void);
bool ft_pages_test_media_is_playing(void);
bool ft_pages_test_start_is_active(void);
bool ft_pages_test_apps_is_active(void);
bool ft_pages_test_home_swipe_ready(void);
size_t ft_pages_test_accent_count(void);
uint32_t ft_pages_test_accent_rgb(size_t color_index);
lv_obj_t *ft_pages_test_get_opacity_button(size_t index);
lv_obj_t *ft_pages_test_get_background_button(size_t index);
size_t ft_pages_test_opacity_count(void);
size_t ft_pages_test_background_count(void);
uint8_t ft_pages_test_opacity_value(size_t index);
lv_obj_t *ft_pages_test_get_search_box(void);
lv_obj_t *ft_pages_test_get_search_keyboard(void);
lv_obj_t *ft_pages_test_get_search_keyboard_hide(void);
bool ft_pages_test_search_keyboard_visible(void);
bool ft_pages_test_search_keyboard_overlay_ok(void);
lv_obj_t *ft_pages_test_get_search_result(size_t app_index);
size_t ft_pages_test_search_visible_count(void);
lv_obj_t *ft_pages_test_get_media_prev_button(void);
lv_obj_t *ft_pages_test_get_media_next_button(void);
lv_obj_t *ft_pages_test_get_media_volume(void);
int32_t ft_pages_test_media_track(void);
int32_t ft_pages_test_media_volume(void);
lv_obj_t *ft_pages_test_get_messages_button(void);
lv_obj_t *ft_pages_test_get_files_refresh_button(void);
uint32_t ft_pages_test_message_count(void);
uint32_t ft_pages_test_files_refresh_count(void);
bool ft_pages_test_files_browser_ready(void);
bool ft_pages_test_files_at_root(void);
bool ft_pages_test_files_mounted(void);
size_t ft_pages_test_files_entry_count(void);
bool ft_pages_test_transient_slots_clear(void);
bool ft_pages_test_tile_editing(void);
size_t ft_pages_test_tile_selected(void);
size_t ft_pages_test_tile_handle_count(void);
bool ft_pages_test_tile_handle_geometry(void);
bool ft_pages_test_tile_move(size_t app_index, size_t target_index);
bool ft_pages_test_tile_move_nearest(size_t app_index);
bool ft_pages_test_tile_move_scrolled(size_t app_index);
bool ft_pages_test_tile_move_edge_autoscroll(size_t app_index);
bool ft_pages_test_tile_layout_settled(void);
bool ft_pages_test_tile_resize(size_t app_index, uint8_t columns, uint8_t rows);
bool ft_pages_test_tile_resize_collision(void);
bool ft_pages_test_tile_resize_boundary(void);
bool ft_pages_test_tile_resize_edge_autoscroll(size_t app_index);
bool ft_pages_test_tile_resize_anchors(size_t app_index);
size_t ft_pages_test_tile_order(size_t app_index);
uint8_t ft_pages_test_tile_columns(size_t app_index);
uint8_t ft_pages_test_tile_rows(size_t app_index);
bool ft_pages_test_tile_layout_valid(void);
bool ft_pages_test_tile_restore_layout(void);
bool ft_pages_test_tile_set_common(size_t app_index, const char *name,
                                   uint8_t opacity, ft_icon_id_t pattern_icon);
const char *ft_pages_test_tile_name(size_t app_index);
uint8_t ft_pages_test_tile_opacity(size_t app_index);
ft_icon_id_t ft_pages_test_tile_pattern(size_t app_index);
const char *ft_pages_test_tile_live_text(size_t app_index);
bool ft_pages_test_tile_live_advance(size_t app_index);
bool ft_pages_test_tile_live_enabled(size_t app_index);
void ft_pages_test_tile_exit_edit(void);

void ft_ui_test_start(void);
void ft_ui_test_print_status(void);
#endif

lv_color_t ft_ui_accent_color(void);
void ft_ui_set_accent(uint32_t rgb);
void ft_ui_set_page_background(uint32_t rgb);
void ft_ui_register_page_background(lv_obj_t *obj);
void ft_ui_register_accent(lv_obj_t *obj, ft_accent_target_t target);
size_t ft_ui_accent_object_count(void);
void ft_ui_notification_toggle(void);
bool ft_ui_notification_visible(void);
void ft_ui_preferences_changed(void);
void ft_ui_apply_language(void);

void ft_ui_style_panel(lv_obj_t *obj);
void ft_ui_style_page(lv_obj_t *obj);

#endif /* FEATHERTALK_UI_INTERNAL_H */
