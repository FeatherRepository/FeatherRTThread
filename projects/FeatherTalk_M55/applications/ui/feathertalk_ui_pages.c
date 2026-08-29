#include <ctype.h>
#include <rtthread.h>
#include <stdint.h>
#include <string.h>
#include <feathertalk/version.h>
#include "ipc/feathertalk_ipc.h"
#include "feathertalk_ui.h"
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_platform.h"

#define FT_ACCENT_COUNT      5U
#define FT_OPACITY_COUNT     3U
#define FT_SETTINGS_COUNT    4U

static lv_obj_t *create_home_page(lv_obj_t *parent);
static lv_obj_t *create_search_page(lv_obj_t *parent);
static lv_obj_t *create_system_page(lv_obj_t *parent);
static lv_obj_t *create_settings_page(lv_obj_t *parent);
static lv_obj_t *create_media_page(lv_obj_t *parent);
static lv_obj_t *create_messages_page(lv_obj_t *parent);
static lv_obj_t *create_files_page(lv_obj_t *parent);
static lv_obj_t *create_about_page(lv_obj_t *parent);
static lv_obj_t *create_settings_display_page(lv_obj_t *parent);
static lv_obj_t *create_settings_wifi_page(lv_obj_t *parent);
static lv_obj_t *create_settings_bluetooth_page(lv_obj_t *parent);
static lv_obj_t *create_settings_personalization_page(lv_obj_t *parent);
static void media_tile_live_content(lv_obj_t *content_host,
                                    uint32_t frame, void *context);
static void messages_tile_live_content(lv_obj_t *content_host,
                                       uint32_t frame, void *context);

static const ft_app_descriptor_t s_apps[] =
{
    {FT_PAGE_SYSTEM,
     {"System", 2U, 1U, 255U, FT_ICON_CELLULAR},
     {FT_ICON_SYSTEM, false, 0U, RT_NULL, RT_NULL}},
    {FT_PAGE_SETTINGS,
     {"Settings", 1U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_SETTINGS, false, 0U, RT_NULL, RT_NULL}},
    {FT_PAGE_MEDIA,
     {"Media", 1U, 1U, 255U, FT_ICON_PLAY},
     {FT_ICON_MEDIA, true, 1600U, media_tile_live_content, RT_NULL}},
    {FT_PAGE_MESSAGES,
     {"Messages", 1U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_MESSAGES, true, 2200U, messages_tile_live_content, RT_NULL}},
    {FT_PAGE_FILES,
     {"Files", 2U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_FILES, false, 0U, RT_NULL, RT_NULL}},
    {FT_PAGE_ABOUT,
     {"About", 1U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_ABOUT, false, 0U, RT_NULL, RT_NULL}},
};

typedef struct
{
    ft_page_id_t page_id;
    ft_icon_id_t icon_id;
    const char *title;
    const char *summary;
    const char *keywords;
} ft_settings_entry_t;

static const ft_settings_entry_t s_settings[FT_SETTINGS_COUNT] =
{
    {FT_PAGE_SETTINGS_DISPLAY, FT_ICON_DISPLAY, "Display & brightness",
     "Backlight level and panel information", "screen pwm panel brightness display"},
    {FT_PAGE_SETTINGS_WIFI, FT_ICON_WIFI, "Wi-Fi",
     "Wireless network state and signal", "wifi wlan wireless network signal"},
    {FT_PAGE_SETTINGS_BLUETOOTH, FT_ICON_BLUETOOTH, "Bluetooth",
     "Radio and connection state", "ble device radio wireless"},
    {FT_PAGE_SETTINGS_PERSONALIZATION, FT_ICON_PERSONALIZATION, "Personalization",
     "Accent, Start Tile opacity and background", "theme color tile appearance"},
};
static const uint32_t s_accent_rgb[FT_ACCENT_COUNT] =
    {0x0078D7UL, 0xE81123UL, 0x107C10UL, 0xFFB900UL, 0x744DA9UL};
static const uint8_t s_opacity_values[FT_OPACITY_COUNT] = {160U, 210U, 255U};
static const char *s_background_names[FT_BACKGROUND_COUNT] = {"Black", "Dark", "Accent"};
static const char *s_tracks[] = {"Feather Intro", "PSoC Skyline", "Metro Pulse"};

static const ft_page_definition_t s_pages[] =
{
    {FT_PAGE_HOME, "Start", create_home_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SEARCH, "Search", create_search_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SYSTEM, "System", create_system_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS, "Settings", create_settings_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_MEDIA, "Media", create_media_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_MESSAGES, "Messages", create_messages_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_FILES, "Files", create_files_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_ABOUT, "About", create_about_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_DISPLAY, "Display & brightness", create_settings_display_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_WIFI, "Wi-Fi", create_settings_wifi_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_BLUETOOTH, "Bluetooth", create_settings_bluetooth_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_PERSONALIZATION, "Personalization", create_settings_personalization_page, RT_NULL, RT_NULL, RT_NULL},
};

static lv_obj_t *s_home_tileview;
static lv_obj_t *s_start_tile;
static lv_obj_t *s_apps_tile;
static lv_obj_t *s_system_status_label;
static lv_obj_t *s_system_metrics_label;
static lv_obj_t *s_apps_buttons[sizeof(s_apps) / sizeof(s_apps[0])];
static lv_obj_t *s_accent_buttons[FT_ACCENT_COUNT];
static lv_obj_t *s_opacity_buttons[FT_OPACITY_COUNT];
static lv_obj_t *s_background_buttons[FT_BACKGROUND_COUNT];
static lv_obj_t *s_settings_search_box;
static lv_obj_t *s_settings_keyboard_tray;
static lv_obj_t *s_settings_keyboard;
static lv_obj_t *s_settings_keyboard_hide;
static lv_obj_t *s_settings_results[FT_SETTINGS_COUNT];
static lv_obj_t *s_settings_brightness_slider;
static lv_obj_t *s_settings_brightness_value;
static lv_obj_t *s_settings_radio_status;
static lv_obj_t *s_settings_radio_button;
static lv_obj_t *s_search_box;
static lv_obj_t *s_search_keyboard_tray;
static lv_obj_t *s_search_keyboard;
static lv_obj_t *s_search_keyboard_hide;
static lv_obj_t *s_search_results[sizeof(s_apps) / sizeof(s_apps[0])];
static lv_obj_t *s_media_prev_button;
static lv_obj_t *s_media_button;
static lv_obj_t *s_media_next_button;
static lv_obj_t *s_media_label;
static lv_obj_t *s_media_state_icon;
static lv_obj_t *s_media_track_label;
static lv_obj_t *s_media_volume;
static bool s_media_playing;
static int32_t s_media_track;
static lv_obj_t *s_messages_button;
static lv_obj_t *s_messages_count_label;
static uint32_t s_message_count;
static lv_obj_t *s_files_refresh_button;
static lv_obj_t *s_files_status_label;
static uint32_t s_files_refresh_count;

static lv_obj_t *tile_live_text_label(lv_obj_t *content_host)
{
    lv_obj_t *label;
    if (content_host == RT_NULL || !lv_obj_is_valid(content_host)) return RT_NULL;
    label = lv_obj_get_child_count(content_host) > 0U ?
            lv_obj_get_child(content_host, 0) : RT_NULL;
    if (label == RT_NULL || !lv_obj_check_type(label, &lv_label_class))
    {
        lv_obj_clean(content_host);
        label = lv_label_create(content_host);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_font(label, ft_layout_font(12), LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
    return label;
}

static void media_tile_live_content(lv_obj_t *content_host,
                                    uint32_t frame, void *context)
{
    size_t track_count = sizeof(s_tracks) / sizeof(s_tracks[0]);
    lv_obj_t *label = tile_live_text_label(content_host);
    LV_UNUSED(context);
    if (label != RT_NULL)
        lv_label_set_text(label,
                          s_tracks[((uint32_t)s_media_track + frame) % track_count]);
}

static void messages_tile_live_content(lv_obj_t *content_host,
                                       uint32_t frame, void *context)
{
    static char text[32];
    lv_obj_t *label = tile_live_text_label(content_host);
    LV_UNUSED(context);
    if (label == RT_NULL) return;
    if ((frame & 1U) != 0U)
        lv_label_set_text(label, "Tap to open inbox");
    else if (s_message_count == 0U)
        lv_label_set_text(label, "No unread messages");
    else
    {
        lv_snprintf(text, sizeof(text), "%lu unread",
                    (unsigned long)s_message_count);
        lv_label_set_text(label, text);
    }
}

static void tracked_object_deleted_cb(lv_event_t *event)
{
    lv_obj_t **slot = (lv_obj_t **)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    if (slot != RT_NULL && *slot == target) *slot = RT_NULL;
}

static lv_obj_t *track_object(lv_obj_t **slot, lv_obj_t *obj)
{
    if (slot == RT_NULL) return obj;
    *slot = obj;
    if (obj != RT_NULL)
        lv_obj_add_event_cb(obj, tracked_object_deleted_cb, LV_EVENT_DELETE, slot);
    return obj;
}

static bool tracked_object_is_type(lv_obj_t **slot, const lv_obj_class_t *class_p)
{
    if (slot == RT_NULL || *slot == RT_NULL) return false;
    if (!lv_obj_is_valid(*slot) || !lv_obj_check_type(*slot, class_p))
    {
        *slot = RT_NULL;
        return false;
    }
    return true;
}

const ft_page_definition_t *ft_pages_find(ft_page_id_t page_id)
{
    size_t i;
    for (i = 0U; i < sizeof(s_pages) / sizeof(s_pages[0]); i++)
        if (s_pages[i].id == page_id) return &s_pages[i];
    return RT_NULL;
}

const ft_app_descriptor_t *ft_apps_get(size_t *count)
{
    if (count != RT_NULL) *count = sizeof(s_apps) / sizeof(s_apps[0]);
    return s_apps;
}

static lv_obj_t *create_text_page(lv_obj_t *parent, const char *title,
                                  ft_icon_id_t icon_id, const char *body)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *header;
    lv_obj_t *description;
    ft_ui_style_page(page);
    lv_obj_set_style_pad_all(page, layout->page_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page, layout->section_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    header = lv_label_create(page);
    lv_label_set_text(header, title);
    lv_obj_set_style_text_font(header, ft_layout_font(22), LV_PART_MAIN);
    ft_ui_register_accent(header, FT_ACCENT_TEXT);
    (void)ft_icon_create(page, icon_id, ft_layout_icon_size(32U), true);
    description = lv_label_create(page);
    lv_label_set_text(description, body);
    lv_obj_set_width(description, lv_pct(100));
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(description, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_line_space(description, ft_layout_px(8), LV_PART_MAIN);
    return page;
}

static lv_obj_t *create_flat_button(lv_obj_t *parent, const char *text,
                                    lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_height(button, ft_layout_get()->control_height);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    ft_ui_register_accent(button, FT_ACCENT_BACKGROUND);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_center(label);
    if (callback != RT_NULL) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    return button;
}

static lv_obj_t *create_icon_button(lv_obj_t *parent, ft_icon_id_t icon_id,
                                    const char *text, lv_event_cb_t callback,
                                    void *user_data, lv_obj_t **label_out,
                                    lv_obj_t **icon_out)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *icon;
    lv_obj_t *label = RT_NULL;
    lv_obj_set_height(button, ft_layout_get()->control_height);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    ft_ui_register_accent(button, FT_ACCENT_BACKGROUND);
    icon = ft_icon_create(button, icon_id, ft_layout_icon_size(24U), false);
    if (text != RT_NULL && text[0] != '\0')
    {
        label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, ft_layout_px(8), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, ft_layout_px(42), 0);
    }
    else
    {
        lv_obj_center(icon);
    }
    if (callback != RT_NULL) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    if (label_out != RT_NULL) *label_out = label;
    if (icon_out != RT_NULL) *icon_out = icon;
    return button;
}

static void app_clicked_cb(lv_event_t *event)
{
    const ft_app_descriptor_t *app = lv_event_get_user_data(event);
    if (app != RT_NULL) (void)ft_router_push(app->page_id);
}

static void style_layout_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void create_home_header(lv_obj_t *page, const char *title, const char *hint_text)
{
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_t *header;
    lv_obj_t *hint;
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_px(34));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    header = lv_label_create(row);
    lv_label_set_text(header, title);
    lv_obj_set_style_text_font(header, ft_layout_font(22), LV_PART_MAIN);
    ft_ui_register_accent(header, FT_ACCENT_TEXT);
    hint = lv_label_create(row);
    lv_label_set_text(hint, hint_text);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, ft_layout_font(12), LV_PART_MAIN);
}

static lv_obj_t *create_start_tile(lv_obj_t *tileview)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *tiles;
    size_t app_count;
    ft_ui_style_page(page);
    ft_ui_register_page_background(page);
    lv_obj_set_style_pad_all(page, layout->home_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page, layout->tile_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    create_home_header(page, "Start", layout->compact ? "Apps  >" : "swipe left for all apps  >");
    tiles = lv_obj_create(page);
    style_layout_container(tiles);
    lv_obj_set_size(tiles, lv_pct(100), 0);
    lv_obj_set_flex_grow(tiles, 1);
    lv_obj_set_style_pad_column(tiles, layout->tile_gap, LV_PART_MAIN);
    lv_obj_set_style_pad_row(tiles, layout->tile_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_add_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tiles, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tiles, LV_SCROLLBAR_MODE_AUTO);
    (void)ft_apps_get(&app_count);
    if (ft_tiles_create(tiles, s_apps, app_count) != RT_EOK)
        rt_kprintf("[FeatherTalk UI] failed to create Start Tile model\n");
    track_object(&s_start_tile, page);
    return page;
}

static lv_obj_t *create_apps_tile(lv_obj_t *tileview)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT);
    lv_obj_t *list;
    size_t app_count;
    size_t i;
    ft_ui_style_page(page);
    ft_ui_register_page_background(page);
    lv_obj_set_style_pad_all(page, layout->home_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page, layout->tile_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    create_home_header(page, "All apps", layout->compact ? "<  Start" : "<  swipe right");
    list = lv_obj_create(page);
    style_layout_container(list);
    lv_obj_set_size(list, lv_pct(100), 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_row(list, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    (void)ft_apps_get(&app_count);
    for (i = 0U; i < app_count; i++)
    {
        lv_obj_t *button = lv_button_create(list);
        lv_obj_t *icon;
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_size(button, lv_pct(100), layout->list_row_height);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x181818), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(button, app_clicked_cb, LV_EVENT_CLICKED, (void *)&s_apps[i]);
        track_object(&s_apps_buttons[i], button);
        icon = ft_icon_create(button, s_apps[i].app.app_icon,
                              ft_layout_icon_size(32U), true);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, ft_layout_px(8), 0);
        lv_label_set_text(label, s_apps[i].tile.name);
        lv_obj_set_style_text_font(label, ft_layout_font(16), LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, ft_layout_px(56), 0);
    }
    track_object(&s_apps_tile, page);
    return page;
}

static lv_obj_t *create_home_page(lv_obj_t *parent)
{
    track_object(&s_home_tileview, lv_tileview_create(parent));
    ft_ui_style_page(s_home_tileview);
    lv_obj_set_scrollbar_mode(s_home_tileview, LV_SCROLLBAR_MODE_OFF);
    (void)create_start_tile(s_home_tileview);
    (void)create_apps_tile(s_home_tileview);
    lv_tileview_set_tile_by_index(s_home_tileview, 0, 0, LV_ANIM_OFF);
    ft_pages_apply_preferences();
    return s_home_tileview;
}

void ft_pages_show_start(void)
{
    ft_tiles_exit_edit();
    if (s_home_tileview != RT_NULL && lv_obj_is_valid(s_home_tileview))
        lv_tileview_set_tile_by_index(s_home_tileview, 0, 0, LV_ANIM_ON);
}
void ft_pages_show_all_apps(void)
{
    ft_tiles_exit_edit();
    if (s_home_tileview != RT_NULL && lv_obj_is_valid(s_home_tileview))
        lv_tileview_set_tile_by_index(s_home_tileview, 1, 0, LV_ANIM_ON);
}
void ft_pages_open_search(void)
{
    ft_router_home();
    (void)ft_router_push(FT_PAGE_SEARCH);
}

static bool contains_ignore_case(const char *text, const char *query)
{
    size_t text_len;
    size_t query_len;
    size_t i;
    size_t j;
    if (query == RT_NULL || query[0] == '\0') return true;
    text_len = strlen(text);
    query_len = strlen(query);
    if (query_len > text_len) return false;
    for (i = 0U; i <= text_len - query_len; i++)
    {
        for (j = 0U; j < query_len; j++)
            if (tolower((unsigned char)text[i + j]) != tolower((unsigned char)query[j])) break;
        if (j == query_len) return true;
    }
    return false;
}

static void search_changed_cb(lv_event_t *event)
{
    const char *query = lv_textarea_get_text(lv_event_get_target(event));
    size_t i;
    for (i = 0U; i < sizeof(s_apps) / sizeof(s_apps[0]); i++)
    {
        if (contains_ignore_case(s_apps[i].tile.name, query))
            lv_obj_remove_flag(s_search_results[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_search_results[i], LV_OBJ_FLAG_HIDDEN);
    }
}
static void search_keyboard_set_visible(bool visible)
{
    if (s_search_keyboard_tray == RT_NULL ||
        !lv_obj_is_valid(s_search_keyboard_tray)) return;
    if (visible)
    {
        lv_obj_remove_flag(s_search_keyboard_tray, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_search_keyboard_tray);
        if (s_search_box != RT_NULL && lv_obj_is_valid(s_search_box))
            lv_obj_scroll_to_view_recursive(s_search_box, LV_ANIM_ON);
    }
    else
    {
        lv_obj_add_flag(s_search_keyboard_tray, LV_OBJ_FLAG_HIDDEN);
    }
}
static void search_focus_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    search_keyboard_set_visible(true);
}
static void keyboard_done_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    search_keyboard_set_visible(false);
}
static void keyboard_hide_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    search_keyboard_set_visible(false);
}

static lv_obj_t *create_search_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_t *page;
    lv_obj_t *hide_button;
    lv_obj_t *spinner;
    lv_obj_t *results;
    size_t i;
    ft_ui_style_page(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    page = create_text_page(root, "Search", FT_ICON_SEARCH,
                                      "Cortana-style local app search");
    spinner = lv_spinner_create(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_align(page, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(spinner, ft_layout_px(44), ft_layout_px(44));
    lv_spinner_set_anim_params(spinner, 900U, 200U);
    lv_obj_set_style_arc_color(spinner, ft_ui_accent_color(), LV_PART_INDICATOR);
    track_object(&s_search_box, lv_textarea_create(page));
    lv_obj_set_size(s_search_box, lv_pct(100), layout->control_height);
    lv_textarea_set_one_line(s_search_box, true);
    lv_textarea_set_placeholder_text(s_search_box, "Search apps");
    lv_obj_add_event_cb(s_search_box, search_changed_cb, LV_EVENT_VALUE_CHANGED, RT_NULL);
    lv_obj_add_event_cb(s_search_box, search_focus_cb, LV_EVENT_FOCUSED, RT_NULL);
    lv_obj_add_event_cb(s_search_box, search_focus_cb, LV_EVENT_CLICKED, RT_NULL);
    results = lv_obj_create(page);
    style_layout_container(results);
    lv_obj_set_width(results, lv_pct(100));
    lv_obj_set_height(results, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(results, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(results, LV_FLEX_FLOW_COLUMN);
    for (i = 0U; i < sizeof(s_apps) / sizeof(s_apps[0]); i++)
    {
        track_object(&s_search_results[i],
                     create_icon_button(results, s_apps[i].app.app_icon, s_apps[i].tile.name,
                                        app_clicked_cb, (void *)&s_apps[i],
                                        RT_NULL, RT_NULL));
        lv_obj_set_width(s_search_results[i], lv_pct(100));
    }
    track_object(&s_search_keyboard_tray, lv_obj_create(root));
    ft_ui_style_panel(s_search_keyboard_tray);
    lv_obj_set_size(s_search_keyboard_tray, lv_pct(100), layout->keyboard_height);
    lv_obj_align(s_search_keyboard_tray, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(s_search_keyboard_tray, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_search_keyboard_tray, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_search_keyboard_tray, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_search_keyboard_tray, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    ft_ui_register_accent(s_search_keyboard_tray, FT_ACCENT_BORDER);
    lv_obj_set_flex_flow(s_search_keyboard_tray, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_search_keyboard_tray, LV_OBJ_FLAG_SCROLLABLE);

    track_object(&s_search_keyboard_hide,
                 create_flat_button(s_search_keyboard_tray,
                                    LV_SYMBOL_DOWN "  Hide keyboard",
                                    keyboard_hide_cb, RT_NULL));
    hide_button = s_search_keyboard_hide;
    lv_obj_set_width(hide_button, lv_pct(100));
    lv_obj_set_height(hide_button, ft_layout_px(36));

    track_object(&s_search_keyboard, lv_keyboard_create(s_search_keyboard_tray));
    lv_obj_set_width(s_search_keyboard, lv_pct(100));
    lv_obj_set_height(s_search_keyboard, 0);
    lv_obj_set_flex_grow(s_search_keyboard, 1);
    lv_obj_set_style_radius(s_search_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_search_keyboard, 0, LV_PART_MAIN);
    lv_keyboard_set_textarea(s_search_keyboard, s_search_box);
    lv_obj_add_event_cb(s_search_keyboard, keyboard_done_cb, LV_EVENT_READY, RT_NULL);
    lv_obj_add_event_cb(s_search_keyboard, keyboard_done_cb, LV_EVENT_CANCEL, RT_NULL);
    lv_obj_add_flag(s_search_keyboard_tray, LV_OBJ_FLAG_HIDDEN);
    return root;
}

static lv_obj_t *create_system_page(lv_obj_t *parent)
{
    lv_obj_t *page = create_text_page(parent, "System", FT_ICON_SYSTEM,
                                      "Dual-core runtime status");
    track_object(&s_system_status_label, lv_label_create(page));
    lv_obj_set_width(s_system_status_label, lv_pct(100));
    lv_label_set_long_mode(s_system_status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_system_status_label, "Waiting for M33 system-status IPC...");
    track_object(&s_system_metrics_label, lv_label_create(page));
    lv_obj_set_width(s_system_metrics_label, lv_pct(100));
    lv_label_set_long_mode(s_system_metrics_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_system_metrics_label, "Performance sample pending...");
    return page;
}
void ft_pages_update_system_status(const char *system_text, const char *metrics_text)
{
    if (tracked_object_is_type(&s_system_status_label, &lv_label_class))
    {
        const char *current = lv_label_get_text(s_system_status_label);
        if (current == RT_NULL || strcmp(current, system_text != RT_NULL ? system_text : "") != 0)
            lv_label_set_text(s_system_status_label, system_text != RT_NULL ? system_text : "");
    }
    if (tracked_object_is_type(&s_system_metrics_label, &lv_label_class))
    {
        const char *current = lv_label_get_text(s_system_metrics_label);
        if (current == RT_NULL || strcmp(current, metrics_text != RT_NULL ? metrics_text : "") != 0)
            lv_label_set_text(s_system_metrics_label, metrics_text != RT_NULL ? metrics_text : "");
    }
}
void ft_pages_live_tile_update(const char *line)
{
    ft_tiles_set_external_text(FT_PAGE_SYSTEM, line);
}

static void accent_clicked_cb(lv_event_t *event)
{ ft_preferences_set_accent((uint32_t)(uintptr_t)lv_event_get_user_data(event)); }
static void opacity_clicked_cb(lv_event_t *event)
{ ft_preferences_set_tile_opa((uint8_t)(uintptr_t)lv_event_get_user_data(event)); }
static void background_clicked_cb(lv_event_t *event)
{ ft_preferences_set_background((ft_background_mode_t)(uintptr_t)lv_event_get_user_data(event)); }

static void settings_keyboard_set_visible(bool visible)
{
    if (s_settings_keyboard_tray == RT_NULL ||
        !lv_obj_is_valid(s_settings_keyboard_tray)) return;
    if (visible)
    {
        lv_obj_remove_flag(s_settings_keyboard_tray, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_settings_keyboard_tray);
        if (s_settings_search_box != RT_NULL && lv_obj_is_valid(s_settings_search_box))
            lv_obj_scroll_to_view_recursive(s_settings_search_box, LV_ANIM_ON);
    }
    else
    {
        lv_obj_add_flag(s_settings_keyboard_tray, LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_keyboard_focus_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    settings_keyboard_set_visible(true);
}

static void settings_keyboard_hide_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    settings_keyboard_set_visible(false);
}

static void settings_category_clicked_cb(lv_event_t *event)
{
    const ft_settings_entry_t *entry = lv_event_get_user_data(event);
    if (entry == RT_NULL) return;
    settings_keyboard_set_visible(false);
    (void)ft_router_push(entry->page_id);
}

static void settings_search_changed_cb(lv_event_t *event)
{
    const char *query = lv_textarea_get_text(lv_event_get_target(event));
    size_t i;
    for (i = 0U; i < FT_SETTINGS_COUNT; i++)
    {
        bool match = contains_ignore_case(s_settings[i].title, query) ||
                     contains_ignore_case(s_settings[i].summary, query) ||
                     contains_ignore_case(s_settings[i].keywords, query);
        if (match)
            lv_obj_remove_flag(s_settings_results[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_settings_results[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *create_settings_entry_button(lv_obj_t *parent,
                                              const ft_settings_entry_t *entry)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *summary;
    lv_obj_t *chevron;
    lv_obj_set_size(button, lv_pct(100), layout->list_row_height);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x151515), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x292929), LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    icon = ft_icon_create(button, entry->icon_id, ft_layout_icon_size(32U), true);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, ft_layout_px(10), 0);
    title = lv_label_create(button);
    lv_label_set_text(title, entry->title);
    lv_obj_set_width(title, lv_pct(70));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, ft_layout_px(58), ft_layout_px(10));
    summary = lv_label_create(button);
    lv_label_set_text(summary, entry->summary);
    lv_obj_set_width(summary, lv_pct(70));
    lv_label_set_long_mode(summary, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(summary, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(summary, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_align(summary, LV_ALIGN_BOTTOM_LEFT, ft_layout_px(58), -ft_layout_px(9));
    chevron = lv_label_create(button);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -ft_layout_px(10), 0);
    lv_obj_add_event_cb(button, settings_category_clicked_cb,
                        LV_EVENT_CLICKED, (void *)entry);
    return button;
}

static lv_obj_t *create_settings_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_t *page;
    lv_obj_t *results;
    size_t i;
    ft_ui_style_page(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    page = create_text_page(root, "Settings", FT_ICON_SETTINGS,
                            "Hardware-aware settings for this Edgi-Talk board");
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_align(page, LV_ALIGN_CENTER, 0, 0);
    track_object(&s_settings_search_box, lv_textarea_create(page));
    lv_obj_set_size(s_settings_search_box, lv_pct(100), layout->control_height);
    lv_textarea_set_one_line(s_settings_search_box, true);
    lv_textarea_set_placeholder_text(s_settings_search_box, "Search settings");
    lv_obj_add_event_cb(s_settings_search_box, settings_search_changed_cb,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    lv_obj_add_event_cb(s_settings_search_box, settings_keyboard_focus_cb,
                        LV_EVENT_FOCUSED, RT_NULL);
    lv_obj_add_event_cb(s_settings_search_box, settings_keyboard_focus_cb,
                        LV_EVENT_CLICKED, RT_NULL);

    results = lv_obj_create(page);
    style_layout_container(results);
    lv_obj_set_width(results, lv_pct(100));
    lv_obj_set_height(results, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(results, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(results, LV_FLEX_FLOW_COLUMN);
    for (i = 0U; i < FT_SETTINGS_COUNT; i++)
        track_object(&s_settings_results[i],
                     create_settings_entry_button(results, &s_settings[i]));

    track_object(&s_settings_keyboard_tray, lv_obj_create(root));
    ft_ui_style_panel(s_settings_keyboard_tray);
    lv_obj_set_size(s_settings_keyboard_tray, lv_pct(100), layout->keyboard_height);
    lv_obj_align(s_settings_keyboard_tray, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(s_settings_keyboard_tray, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_settings_keyboard_tray, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_settings_keyboard_tray, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_settings_keyboard_tray, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    ft_ui_register_accent(s_settings_keyboard_tray, FT_ACCENT_BORDER);
    lv_obj_set_flex_flow(s_settings_keyboard_tray, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_settings_keyboard_tray, LV_OBJ_FLAG_SCROLLABLE);
    track_object(&s_settings_keyboard_hide,
                 create_flat_button(s_settings_keyboard_tray,
                                    LV_SYMBOL_DOWN "  Hide keyboard",
                                    settings_keyboard_hide_cb, RT_NULL));
    lv_obj_set_width(s_settings_keyboard_hide, lv_pct(100));
    lv_obj_set_height(s_settings_keyboard_hide, ft_layout_px(36));
    track_object(&s_settings_keyboard, lv_keyboard_create(s_settings_keyboard_tray));
    lv_obj_set_width(s_settings_keyboard, lv_pct(100));
    lv_obj_set_height(s_settings_keyboard, 0);
    lv_obj_set_flex_grow(s_settings_keyboard, 1);
    lv_obj_set_style_radius(s_settings_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_settings_keyboard, 0, LV_PART_MAIN);
    lv_keyboard_set_textarea(s_settings_keyboard, s_settings_search_box);
    lv_obj_add_event_cb(s_settings_keyboard, settings_keyboard_hide_cb,
                        LV_EVENT_READY, RT_NULL);
    lv_obj_add_event_cb(s_settings_keyboard, settings_keyboard_hide_cb,
                        LV_EVENT_CANCEL, RT_NULL);
    lv_obj_add_flag(s_settings_keyboard_tray, LV_OBJ_FLAG_HIDDEN);
    return root;
}

static void settings_brightness_refresh(void)
{
    char text[32];
    if (s_settings_brightness_value == RT_NULL ||
        !lv_obj_is_valid(s_settings_brightness_value)) return;
    if (!ft_platform_brightness_available())
    {
        lv_label_set_text(s_settings_brightness_value, "Unavailable");
        return;
    }
    lv_snprintf(text, sizeof(text), "%u%%", ft_platform_get_brightness());
    lv_label_set_text(s_settings_brightness_value, text);
}

static void settings_brightness_changed_cb(lv_event_t *event)
{
    uint8_t value = (uint8_t)lv_slider_get_value(lv_event_get_target(event));
    if (ft_platform_set_brightness(value) == RT_EOK)
        settings_brightness_refresh();
}

static lv_obj_t *create_settings_display_page(lv_obj_t *parent)
{
    lv_obj_t *page = create_text_page(parent, "Display & brightness",
                                      FT_ICON_DISPLAY,
                                      "The visible 0-100% range maps to a safe 50-100% panel PWM duty range.");
    lv_obj_t *caption = lv_label_create(page);
    lv_label_set_text(caption, "Brightness");
    track_object(&s_settings_brightness_value, lv_label_create(page));
    track_object(&s_settings_brightness_slider, lv_slider_create(page));
    lv_obj_set_size(s_settings_brightness_slider, lv_pct(100), ft_layout_px(20));
    lv_slider_set_range(s_settings_brightness_slider, 0, 100);
    lv_slider_set_value(s_settings_brightness_slider,
                        ft_platform_get_brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_settings_brightness_slider,
                        settings_brightness_changed_cb,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    if (!ft_platform_brightness_available())
        lv_obj_add_state(s_settings_brightness_slider, LV_STATE_DISABLED);
    settings_brightness_refresh();
    caption = lv_label_create(page);
    lv_label_set_text(caption, "Panel: 480 x 800, portrait\nRotation control: unavailable (driver not enabled)");
    lv_obj_set_width(caption, lv_pct(100));
    lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
    return page;
}

static void settings_radio_toggle_cb(lv_event_t *event)
{
    feathertalk_quick_control_t control =
        (feathertalk_quick_control_t)(uintptr_t)lv_event_get_user_data(event);
    feathertalk_quick_status_t status;
    uint8_t bit = (uint8_t)(1U << control);
    uint8_t target;
    if (feathertalk_ipc_get_quick_status(&status) != RT_EOK ||
        (status.capabilities & bit) == 0U) return;
    target = (status.enabled & bit) != 0U ? 0U : 1U;
    if (feathertalk_ipc_set_quick_control((uint8_t)control, target) == RT_EOK &&
        s_settings_radio_status != RT_NULL && lv_obj_is_valid(s_settings_radio_status))
        lv_label_set_text(s_settings_radio_status, "Request sent to M33; waiting for status update...");
}

static lv_obj_t *create_settings_radio_page(lv_obj_t *parent,
                                            feathertalk_quick_control_t control)
{
    bool wifi = control == FEATHERTALK_QUICK_WIFI;
    const char *title = wifi ? "Wi-Fi" : "Bluetooth";
    ft_icon_id_t icon = wifi ? FT_ICON_WIFI : FT_ICON_BLUETOOTH;
    feathertalk_quick_status_t status;
    uint8_t bit = (uint8_t)(1U << control);
    bool valid = feathertalk_ipc_get_quick_status(&status) == RT_EOK;
    bool available = valid && (status.capabilities & bit) != 0U;
    bool enabled = available && (status.enabled & bit) != 0U;
    bool connected = enabled && (status.connected & bit) != 0U;
    char state[160];
    char action[32];
    lv_obj_t *page = create_text_page(parent, title, icon,
                                      "This board exposes only Wi-Fi and Bluetooth radio categories; cellular settings are intentionally absent.");
    track_object(&s_settings_radio_status, lv_label_create(page));
    lv_obj_set_width(s_settings_radio_status, lv_pct(100));
    lv_label_set_long_mode(s_settings_radio_status, LV_LABEL_LONG_WRAP);
    if (!available)
    {
        lv_snprintf(state, sizeof(state),
                    "Hardware category: supported by the product design\n"
                    "Current service: unavailable\nM33 driver/capability: not enabled");
        lv_snprintf(action, sizeof(action), "Service unavailable");
    }
    else if (wifi)
    {
        if (connected && status.wifi_signal_percent != FEATHERTALK_SYSTEM_VALUE_UNKNOWN)
            lv_snprintf(state, sizeof(state), "Radio: on\nConnection: connected\nSignal: %u%%",
                        status.wifi_signal_percent);
        else
            lv_snprintf(state, sizeof(state), "Radio: %s\nConnection: %s",
                        enabled ? "on" : "off",
                        connected ? "connected" : "not connected");
        lv_snprintf(action, sizeof(action), "Turn Wi-Fi %s", enabled ? "off" : "on");
    }
    else
    {
        lv_snprintf(state, sizeof(state), "Radio: %s\nConnection: %s",
                    enabled ? "on" : "off",
                    connected ? "connected" : "not connected");
        lv_snprintf(action, sizeof(action), "Turn Bluetooth %s", enabled ? "off" : "on");
    }
    lv_label_set_text(s_settings_radio_status, state);
    track_object(&s_settings_radio_button,
                 create_flat_button(page, action, settings_radio_toggle_cb,
                                    (void *)(uintptr_t)control));
    lv_obj_set_width(s_settings_radio_button, lv_pct(100));
    if (!available) lv_obj_add_state(s_settings_radio_button, LV_STATE_DISABLED);
    return page;
}

static lv_obj_t *create_settings_wifi_page(lv_obj_t *parent)
{
    return create_settings_radio_page(parent, FEATHERTALK_QUICK_WIFI);
}

static lv_obj_t *create_settings_bluetooth_page(lv_obj_t *parent)
{
    return create_settings_radio_page(parent, FEATHERTALK_QUICK_BLUETOOTH);
}

static lv_obj_t *create_settings_personalization_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = create_text_page(parent, "Personalization", FT_ICON_PERSONALIZATION,
                                      "Personalization preferences (memory backend)");
    lv_obj_t *caption;
    lv_obj_t *row;
    size_t i;
    caption = lv_label_create(page);
    lv_label_set_text(caption, "Accent color");
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_ACCENT_COUNT; i++)
    {
        lv_obj_t *swatch = lv_button_create(row);
        lv_obj_set_size(swatch, 0, layout->control_height);
        lv_obj_set_flex_grow(swatch, 1);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(s_accent_rgb[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(swatch, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(swatch, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(swatch, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(swatch, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(swatch, accent_clicked_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s_accent_rgb[i]);
        track_object(&s_accent_buttons[i], swatch);
    }
    caption = lv_label_create(page);
    lv_label_set_text(caption, "Start Tile opacity");
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_OPACITY_COUNT; i++)
    {
        char text[16];
        lv_snprintf(text, sizeof(text), "%u%%", (unsigned)(s_opacity_values[i] * 100U / 255U));
        track_object(&s_opacity_buttons[i],
                     create_flat_button(row, text, opacity_clicked_cb,
                                        (void *)(uintptr_t)s_opacity_values[i]));
        lv_obj_set_width(s_opacity_buttons[i], 0);
        lv_obj_set_flex_grow(s_opacity_buttons[i], 1);
    }
    caption = lv_label_create(page);
    lv_label_set_text(caption, "Background");
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_BACKGROUND_COUNT; i++)
    {
        track_object(&s_background_buttons[i],
                     create_flat_button(row, s_background_names[i], background_clicked_cb,
                                        (void *)(uintptr_t)i));
        lv_obj_set_width(s_background_buttons[i], 0);
        lv_obj_set_flex_grow(s_background_buttons[i], 1);
    }
    return page;
}

void ft_pages_apply_preferences(void)
{
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    ft_tiles_apply_opacity(preferences->tile_opa);
}

static void update_media_labels(void)
{
    if (s_media_label != RT_NULL && lv_obj_is_valid(s_media_label))
        lv_label_set_text(s_media_label, s_media_playing ? "Pause" : "Play");
    if (s_media_state_icon != RT_NULL && lv_obj_is_valid(s_media_state_icon))
        ft_icon_set(s_media_state_icon, s_media_playing ? FT_ICON_PAUSE : FT_ICON_PLAY,
                    ft_layout_icon_size(24U));
    if (s_media_track_label != RT_NULL && lv_obj_is_valid(s_media_track_label))
        lv_label_set_text(s_media_track_label, s_tracks[s_media_track]);
}
static void media_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_media_playing = !s_media_playing;
    ft_tiles_set_live_loop(FT_PAGE_MEDIA, s_media_playing);
    update_media_labels();
}
static void media_prev_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_media_track = (s_media_track + (int32_t)(sizeof(s_tracks) / sizeof(s_tracks[0])) - 1) %
                    (int32_t)(sizeof(s_tracks) / sizeof(s_tracks[0]));
    update_media_labels();
}
static void media_next_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_media_track = (s_media_track + 1) % (int32_t)(sizeof(s_tracks) / sizeof(s_tracks[0]));
    update_media_labels();
}

static lv_obj_t *create_media_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = create_text_page(parent, "Media", FT_ICON_MEDIA,
                                      "In-memory playback controller");
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_t *volume_caption;
    s_media_playing = false;
    s_media_track = 0;
    track_object(&s_media_track_label, lv_label_create(page));
    lv_obj_set_style_text_font(s_media_track_label, ft_layout_font(22), LV_PART_MAIN);
    ft_ui_register_accent(s_media_track_label, FT_ACCENT_TEXT);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    track_object(&s_media_prev_button,
                 create_icon_button(row, FT_ICON_PREVIOUS, RT_NULL,
                                    media_prev_cb, RT_NULL, RT_NULL, RT_NULL));
    track_object(&s_media_button,
                 create_icon_button(row, FT_ICON_PLAY, "Play",
                                    media_clicked_cb, RT_NULL,
                                    &s_media_label, &s_media_state_icon));
    track_object(&s_media_label, s_media_label);
    track_object(&s_media_state_icon, s_media_state_icon);
    track_object(&s_media_next_button,
                 create_icon_button(row, FT_ICON_NEXT, RT_NULL,
                                    media_next_cb, RT_NULL, RT_NULL, RT_NULL));
    lv_obj_set_width(s_media_prev_button, 0);
    lv_obj_set_flex_grow(s_media_prev_button, 1);
    lv_obj_set_width(s_media_button, 0);
    lv_obj_set_flex_grow(s_media_button, 2);
    lv_obj_set_width(s_media_next_button, 0);
    lv_obj_set_flex_grow(s_media_next_button, 1);
    volume_caption = lv_label_create(page);
    lv_label_set_text(volume_caption, "Volume");
    track_object(&s_media_volume, lv_slider_create(page));
    lv_obj_set_size(s_media_volume, lv_pct(100), ft_layout_px(20));
    lv_slider_set_range(s_media_volume, 0, 100);
    lv_slider_set_value(s_media_volume, 60, LV_ANIM_OFF);
    update_media_labels();
    return page;
}

static void message_test_cb(lv_event_t *event)
{
    char text[48];
    LV_UNUSED(event);
    s_message_count++;
    lv_snprintf(text, sizeof(text), "Notifications created: %lu", (unsigned long)s_message_count);
    lv_label_set_text(s_messages_count_label, text);
    feathertalk_ui_notify("Messages", "Test notification",
                         "In-memory notification delivery succeeded.");
    feathertalk_ui_alert("Messages", "In-memory notification delivery succeeded.");
}
static lv_obj_t *create_messages_page(lv_obj_t *parent)
{
    lv_obj_t *page = create_text_page(parent, "Messages", FT_ICON_MESSAGES,
                                      "Notification service adapter");
    track_object(&s_messages_count_label, lv_label_create(page));
    lv_label_set_text(s_messages_count_label, "Notifications created: 0");
    track_object(&s_messages_button,
                 create_flat_button(page, "Create test notification", message_test_cb, RT_NULL));
    lv_obj_set_width(s_messages_button, lv_pct(100));
    return page;
}

static void files_refresh_cb(lv_event_t *event)
{
    char text[160];
    LV_UNUSED(event);
    s_files_refresh_count++;
    lv_snprintf(text, sizeof(text),
                "Internal flash: firmware/resource partition\n"
                "External storage: unavailable (driver not enabled)\nRefresh count: %lu",
                (unsigned long)s_files_refresh_count);
    lv_label_set_text(s_files_status_label, text);
}
static lv_obj_t *create_files_page(lv_obj_t *parent)
{
    lv_obj_t *page = create_text_page(parent, "Files", FT_ICON_FILES,
                                      "Storage visibility and resource policy");
    track_object(&s_files_status_label, lv_label_create(page));
    lv_obj_set_width(s_files_status_label, lv_pct(100));
    lv_label_set_long_mode(s_files_status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_files_status_label,
                      "Internal flash: firmware/resource partition\n"
                      "External storage: unavailable (driver not enabled)\nRefresh count: 0");
    track_object(&s_files_refresh_button,
                 create_icon_button(page, FT_ICON_REFRESH, "Refresh",
                                    files_refresh_cb, RT_NULL,
                                    RT_NULL, RT_NULL));
    lv_obj_set_width(s_files_refresh_button, lv_pct(100));
    return page;
}

static lv_obj_t *create_about_page(lv_obj_t *parent)
{
    char text[192];
    lv_snprintf(text, sizeof(text),
                "FeatherTalk %s\nM55 firmware %s\nIPC ABI %u\n"
                "RT-Thread + LVGL 9.2\nBounded router and explicit app registry",
                FEATHERTALK_PRODUCT_VERSION, FEATHERTALK_M55_FIRMWARE_VERSION,
                (unsigned)FEATHERTALK_IPC_ABI_VERSION);
    return create_text_page(parent, "About", FT_ICON_ABOUT, text);
}

#ifdef FEATHERTALK_UI_TEST_MODE
lv_obj_t *ft_pages_test_get_start_button(size_t i)
{ return ft_tiles_test_get_object(i); }
lv_obj_t *ft_pages_test_get_apps_button(size_t i)
{ return i < sizeof(s_apps) / sizeof(s_apps[0]) ? s_apps_buttons[i] : RT_NULL; }
lv_obj_t *ft_pages_test_get_accent_button(size_t i)
{ return i < FT_ACCENT_COUNT ? s_accent_buttons[i] : RT_NULL; }
lv_obj_t *ft_pages_test_get_opacity_button(size_t i)
{ return i < FT_OPACITY_COUNT ? s_opacity_buttons[i] : RT_NULL; }
lv_obj_t *ft_pages_test_get_background_button(size_t i)
{ return i < FT_BACKGROUND_COUNT ? s_background_buttons[i] : RT_NULL; }
size_t ft_pages_test_opacity_count(void) { return FT_OPACITY_COUNT; }
size_t ft_pages_test_background_count(void) { return FT_BACKGROUND_COUNT; }
uint8_t ft_pages_test_opacity_value(size_t i) { return i < FT_OPACITY_COUNT ? s_opacity_values[i] : 0U; }
lv_obj_t *ft_pages_test_get_settings_search_box(void) { return s_settings_search_box; }
lv_obj_t *ft_pages_test_get_settings_keyboard_hide(void) { return s_settings_keyboard_hide; }
bool ft_pages_test_settings_keyboard_visible(void)
{
    return s_settings_keyboard_tray != RT_NULL &&
           lv_obj_is_valid(s_settings_keyboard_tray) &&
           !lv_obj_has_flag(s_settings_keyboard_tray, LV_OBJ_FLAG_HIDDEN);
}
bool ft_pages_test_settings_keyboard_overlay_ok(void)
{
    lv_obj_t *root;
    lv_obj_t *page;
    lv_area_t root_area;
    lv_area_t tray_area;
    if (!ft_pages_test_settings_keyboard_visible() ||
        s_settings_search_box == RT_NULL || !lv_obj_is_valid(s_settings_search_box) ||
        s_settings_keyboard == RT_NULL || !lv_obj_is_valid(s_settings_keyboard) ||
        s_settings_keyboard_hide == RT_NULL || !lv_obj_is_valid(s_settings_keyboard_hide))
        return false;
    root = lv_obj_get_parent(s_settings_keyboard_tray);
    page = lv_obj_get_parent(s_settings_search_box);
    if (root == RT_NULL || page == RT_NULL || root == page ||
        lv_obj_get_parent(page) != root ||
        lv_obj_get_parent(s_settings_keyboard) != s_settings_keyboard_tray ||
        lv_obj_get_parent(s_settings_keyboard_hide) != s_settings_keyboard_tray)
        return false;
    lv_obj_update_layout(root);
    lv_obj_get_coords(root, &root_area);
    lv_obj_get_coords(s_settings_keyboard_tray, &tray_area);
    return tray_area.x1 >= root_area.x1 - 2 &&
           tray_area.x2 <= root_area.x2 + 2 &&
           tray_area.y1 > root_area.y1 &&
           tray_area.y2 >= root_area.y2 - 2 &&
           tray_area.y2 <= root_area.y2 + 2 &&
           lv_obj_get_height(s_settings_keyboard_tray) == ft_layout_get()->keyboard_height;
}
lv_obj_t *ft_pages_test_get_settings_result(size_t i)
{ return i < FT_SETTINGS_COUNT ? s_settings_results[i] : RT_NULL; }
size_t ft_pages_test_settings_count(void) { return FT_SETTINGS_COUNT; }
size_t ft_pages_test_settings_visible_count(void)
{
    size_t count = 0U;
    size_t i;
    for (i = 0U; i < FT_SETTINGS_COUNT; i++)
        if (s_settings_results[i] != RT_NULL && lv_obj_is_valid(s_settings_results[i]) &&
            !lv_obj_has_flag(s_settings_results[i], LV_OBJ_FLAG_HIDDEN)) count++;
    return count;
}
ft_page_id_t ft_pages_test_settings_page_id(size_t i)
{ return i < FT_SETTINGS_COUNT ? s_settings[i].page_id : FT_PAGE_COUNT; }
lv_obj_t *ft_pages_test_get_settings_brightness(void)
{ return s_settings_brightness_slider; }
lv_obj_t *ft_pages_test_get_search_box(void) { return s_search_box; }
lv_obj_t *ft_pages_test_get_search_keyboard(void) { return s_search_keyboard; }
lv_obj_t *ft_pages_test_get_search_keyboard_hide(void) { return s_search_keyboard_hide; }
bool ft_pages_test_search_keyboard_visible(void)
{
    return s_search_keyboard_tray != RT_NULL &&
           lv_obj_is_valid(s_search_keyboard_tray) &&
           !lv_obj_has_flag(s_search_keyboard_tray, LV_OBJ_FLAG_HIDDEN);
}
bool ft_pages_test_search_keyboard_overlay_ok(void)
{
    lv_obj_t *root;
    lv_obj_t *page;
    lv_area_t root_area;
    lv_area_t tray_area;
    if (!ft_pages_test_search_keyboard_visible() ||
        s_search_box == RT_NULL || !lv_obj_is_valid(s_search_box) ||
        s_search_keyboard == RT_NULL || !lv_obj_is_valid(s_search_keyboard) ||
        s_search_keyboard_hide == RT_NULL || !lv_obj_is_valid(s_search_keyboard_hide))
        return false;
    root = lv_obj_get_parent(s_search_keyboard_tray);
    page = lv_obj_get_parent(s_search_box);
    if (root == RT_NULL || page == RT_NULL || root == page ||
        lv_obj_get_parent(page) != root ||
        lv_obj_get_parent(s_search_keyboard) != s_search_keyboard_tray ||
        lv_obj_get_parent(s_search_keyboard_hide) != s_search_keyboard_tray)
        return false;
    lv_obj_update_layout(root);
    lv_obj_get_coords(root, &root_area);
    lv_obj_get_coords(s_search_keyboard_tray, &tray_area);
    return tray_area.x1 >= root_area.x1 - 2 &&
           tray_area.x2 <= root_area.x2 + 2 &&
           tray_area.y1 > root_area.y1 &&
           tray_area.y2 >= root_area.y2 - 2 &&
           tray_area.y2 <= root_area.y2 + 2 &&
           lv_obj_get_height(s_search_keyboard_tray) == ft_layout_get()->keyboard_height;
}
lv_obj_t *ft_pages_test_get_search_result(size_t i)
{ return i < sizeof(s_apps) / sizeof(s_apps[0]) ? s_search_results[i] : RT_NULL; }
size_t ft_pages_test_search_visible_count(void)
{
    size_t count = 0U;
    size_t i;
    for (i = 0U; i < sizeof(s_search_results) / sizeof(s_search_results[0]); i++)
        if (s_search_results[i] != RT_NULL && lv_obj_is_valid(s_search_results[i]) &&
            !lv_obj_has_flag(s_search_results[i], LV_OBJ_FLAG_HIDDEN)) count++;
    return count;
}
lv_obj_t *ft_pages_test_get_media_button(void) { return s_media_button; }
lv_obj_t *ft_pages_test_get_media_prev_button(void) { return s_media_prev_button; }
lv_obj_t *ft_pages_test_get_media_next_button(void) { return s_media_next_button; }
lv_obj_t *ft_pages_test_get_media_volume(void) { return s_media_volume; }
const char *ft_pages_test_get_media_label(void)
{ return s_media_label != RT_NULL && lv_obj_is_valid(s_media_label) ? lv_label_get_text(s_media_label) : RT_NULL; }
bool ft_pages_test_media_is_playing(void) { return s_media_playing; }
int32_t ft_pages_test_media_track(void) { return s_media_track; }
int32_t ft_pages_test_media_volume(void)
{ return s_media_volume != RT_NULL && lv_obj_is_valid(s_media_volume) ? lv_slider_get_value(s_media_volume) : -1; }
lv_obj_t *ft_pages_test_get_messages_button(void) { return s_messages_button; }
lv_obj_t *ft_pages_test_get_files_refresh_button(void) { return s_files_refresh_button; }
uint32_t ft_pages_test_message_count(void) { return s_message_count; }
uint32_t ft_pages_test_files_refresh_count(void) { return s_files_refresh_count; }
bool ft_pages_test_transient_slots_clear(void)
{
    size_t i;
    if (s_system_status_label != RT_NULL || s_system_metrics_label != RT_NULL ||
        s_search_box != RT_NULL || s_search_keyboard_tray != RT_NULL ||
        s_search_keyboard != RT_NULL || s_search_keyboard_hide != RT_NULL ||
        s_settings_search_box != RT_NULL || s_settings_keyboard_tray != RT_NULL ||
        s_settings_keyboard != RT_NULL || s_settings_keyboard_hide != RT_NULL ||
        s_settings_brightness_slider != RT_NULL || s_settings_brightness_value != RT_NULL ||
        s_settings_radio_status != RT_NULL || s_settings_radio_button != RT_NULL ||
        s_media_prev_button != RT_NULL || s_media_button != RT_NULL ||
        s_media_next_button != RT_NULL || s_media_label != RT_NULL ||
        s_media_state_icon != RT_NULL || s_media_track_label != RT_NULL ||
        s_media_volume != RT_NULL || s_messages_button != RT_NULL ||
        s_messages_count_label != RT_NULL || s_files_refresh_button != RT_NULL ||
        s_files_status_label != RT_NULL) return false;
    for (i = 0U; i < FT_ACCENT_COUNT; i++)
        if (s_accent_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_OPACITY_COUNT; i++)
        if (s_opacity_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_BACKGROUND_COUNT; i++)
        if (s_background_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < sizeof(s_search_results) / sizeof(s_search_results[0]); i++)
        if (s_search_results[i] != RT_NULL) return false;
    for (i = 0U; i < FT_SETTINGS_COUNT; i++)
        if (s_settings_results[i] != RT_NULL) return false;
    return true;
}
bool ft_pages_test_tile_editing(void) { return ft_tiles_test_editing(); }
size_t ft_pages_test_tile_selected(void) { return ft_tiles_test_selected(); }
size_t ft_pages_test_tile_handle_count(void) { return ft_tiles_test_handle_count(); }
bool ft_pages_test_tile_handle_geometry(void) { return ft_tiles_test_handle_geometry(); }
bool ft_pages_test_tile_move(size_t app_index, size_t target_index)
{ return ft_tiles_test_move(app_index, target_index); }
bool ft_pages_test_tile_move_nearest(size_t app_index)
{ return ft_tiles_test_move_nearest(app_index); }
bool ft_pages_test_tile_move_scrolled(size_t app_index)
{ return ft_tiles_test_move_scrolled(app_index); }
bool ft_pages_test_tile_layout_settled(void)
{ return ft_tiles_test_layout_settled(); }
bool ft_pages_test_tile_resize(size_t app_index, uint8_t columns, uint8_t rows)
{ return ft_tiles_test_resize(app_index, columns, rows); }
bool ft_pages_test_tile_resize_collision(void)
{ return ft_tiles_test_resize_collision(); }
bool ft_pages_test_tile_resize_boundary(void) { return ft_tiles_test_resize_boundary(); }
bool ft_pages_test_tile_resize_anchors(size_t app_index)
{ return ft_tiles_test_resize_anchors(app_index); }
size_t ft_pages_test_tile_order(size_t app_index)
{ return ft_tiles_test_order(app_index); }
uint8_t ft_pages_test_tile_columns(size_t app_index)
{ return ft_tiles_test_columns(app_index); }
uint8_t ft_pages_test_tile_rows(size_t app_index)
{ return ft_tiles_test_rows(app_index); }
bool ft_pages_test_tile_layout_valid(void) { return ft_tiles_test_layout_valid(); }
bool ft_pages_test_tile_restore_layout(void) { return ft_tiles_test_restore_layout(); }
bool ft_pages_test_tile_set_common(size_t app_index, const char *name,
                                   uint8_t opacity, ft_icon_id_t pattern_icon)
{ return ft_tiles_test_set_common(app_index, name, opacity, pattern_icon); }
const char *ft_pages_test_tile_name(size_t app_index)
{ return ft_tiles_test_name(app_index); }
uint8_t ft_pages_test_tile_opacity(size_t app_index)
{ return ft_tiles_test_opacity(app_index); }
ft_icon_id_t ft_pages_test_tile_pattern(size_t app_index)
{ return ft_tiles_test_pattern(app_index); }
const char *ft_pages_test_tile_live_text(size_t app_index)
{ return ft_tiles_test_live_text(app_index); }
bool ft_pages_test_tile_live_advance(size_t app_index)
{ return ft_tiles_test_live_advance(app_index); }
bool ft_pages_test_tile_live_enabled(size_t app_index)
{ return ft_tiles_test_live_enabled(app_index); }
void ft_pages_test_tile_exit_edit(void) { ft_tiles_exit_edit(); }
bool ft_pages_test_start_is_active(void)
{ return s_home_tileview != RT_NULL && lv_obj_is_valid(s_home_tileview) && lv_tileview_get_tile_active(s_home_tileview) == s_start_tile; }
bool ft_pages_test_apps_is_active(void)
{ return s_home_tileview != RT_NULL && lv_obj_is_valid(s_home_tileview) && lv_tileview_get_tile_active(s_home_tileview) == s_apps_tile; }
size_t ft_pages_test_accent_count(void) { return FT_ACCENT_COUNT; }
uint32_t ft_pages_test_accent_rgb(size_t i) { return i < FT_ACCENT_COUNT ? s_accent_rgb[i] : 0U; }
#endif
