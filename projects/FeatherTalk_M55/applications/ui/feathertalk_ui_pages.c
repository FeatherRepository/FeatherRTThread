#include <ctype.h>
#include <rtthread.h>
#include <stdint.h>
#include <string.h>
#include <feathertalk/version.h>
#include "feathertalk_ui.h"
#include "feathertalk_ui_internal.h"

#define FT_ACCENT_COUNT      5U
#define FT_OPACITY_COUNT     3U

static lv_obj_t *create_home_page(lv_obj_t *parent);
static lv_obj_t *create_search_page(lv_obj_t *parent);
static lv_obj_t *create_system_page(lv_obj_t *parent);
static lv_obj_t *create_settings_page(lv_obj_t *parent);
static lv_obj_t *create_media_page(lv_obj_t *parent);
static lv_obj_t *create_messages_page(lv_obj_t *parent);
static lv_obj_t *create_files_page(lv_obj_t *parent);
static lv_obj_t *create_about_page(lv_obj_t *parent);

static const ft_app_descriptor_t s_apps[] =
{
    {"System", FT_ICON_SYSTEM, FT_PAGE_SYSTEM, true},
    {"Settings", FT_ICON_SETTINGS, FT_PAGE_SETTINGS, false},
    {"Media", FT_ICON_MEDIA, FT_PAGE_MEDIA, false},
    {"Messages", FT_ICON_MESSAGES, FT_PAGE_MESSAGES, false},
    {"Files", FT_ICON_FILES, FT_PAGE_FILES, true},
    {"About", FT_ICON_ABOUT, FT_PAGE_ABOUT, false},
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
};

static lv_obj_t *s_home_tileview;
static lv_obj_t *s_start_tile;
static lv_obj_t *s_apps_tile;
static lv_obj_t *s_system_tile_status_label;
static lv_obj_t *s_system_status_label;
static lv_obj_t *s_system_metrics_label;
static lv_obj_t *s_start_app_buttons[sizeof(s_apps) / sizeof(s_apps[0])];
static lv_obj_t *s_apps_buttons[sizeof(s_apps) / sizeof(s_apps[0])];
static lv_obj_t *s_accent_buttons[FT_ACCENT_COUNT];
static lv_obj_t *s_opacity_buttons[FT_OPACITY_COUNT];
static lv_obj_t *s_background_buttons[FT_BACKGROUND_COUNT];
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

static lv_obj_t *create_tile(lv_obj_t *parent, const ft_app_descriptor_t *app)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_t *icon;
    lv_obj_t *label;
    lv_obj_set_size(tile, ft_layout_tile_width(app->wide_tile), layout->tile_height);
    lv_obj_set_style_radius(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_add_event_cb(tile, app_clicked_cb, LV_EVENT_CLICKED, (void *)app);
    ft_ui_register_accent(tile, FT_ACCENT_BACKGROUND);
    icon = ft_icon_create(tile, app->icon, ft_layout_icon_size(48U), false);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    label = lv_label_create(tile);
    lv_label_set_text(label, app->name);
    lv_obj_set_style_text_font(label, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (app->page_id == FT_PAGE_SYSTEM)
    {
        track_object(&s_system_tile_status_label, lv_label_create(tile));
        lv_obj_set_width(s_system_tile_status_label,
                         ft_layout_tile_width(true) - ft_layout_px(88));
        lv_label_set_long_mode(s_system_tile_status_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_system_tile_status_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_system_tile_status_label, ft_layout_font(12), LV_PART_MAIN);
        lv_label_set_text(s_system_tile_status_label, "M33 waiting");
        lv_obj_align(s_system_tile_status_label, LV_ALIGN_TOP_RIGHT, 0, ft_layout_px(4));
    }
    return tile;
}

static lv_obj_t *create_start_tile(lv_obj_t *tileview)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *tiles;
    size_t app_count;
    size_t i;
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
    for (i = 0U; i < app_count; i++)
        track_object(&s_start_app_buttons[i], create_tile(tiles, &s_apps[i]));
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
        icon = ft_icon_create(button, s_apps[i].icon, ft_layout_icon_size(32U), true);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, ft_layout_px(8), 0);
        lv_label_set_text(label, s_apps[i].name);
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
    if (s_home_tileview != RT_NULL && lv_obj_is_valid(s_home_tileview))
        lv_tileview_set_tile_by_index(s_home_tileview, 0, 0, LV_ANIM_ON);
}
void ft_pages_show_all_apps(void)
{
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
        if (contains_ignore_case(s_apps[i].name, query))
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
                     create_icon_button(results, s_apps[i].icon, s_apps[i].name,
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
        lv_label_set_text(s_system_status_label, system_text);
    if (tracked_object_is_type(&s_system_metrics_label, &lv_label_class))
        lv_label_set_text(s_system_metrics_label, metrics_text);
}
void ft_pages_live_tile_update(const char *line)
{
    if (!tracked_object_is_type(&s_system_tile_status_label, &lv_label_class)) return;
    lv_label_set_text(s_system_tile_status_label, line);
}

static void accent_clicked_cb(lv_event_t *event)
{ ft_preferences_set_accent((uint32_t)(uintptr_t)lv_event_get_user_data(event)); }
static void opacity_clicked_cb(lv_event_t *event)
{ ft_preferences_set_tile_opa((uint8_t)(uintptr_t)lv_event_get_user_data(event)); }
static void background_clicked_cb(lv_event_t *event)
{ ft_preferences_set_background((ft_background_mode_t)(uintptr_t)lv_event_get_user_data(event)); }

static lv_obj_t *create_settings_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = create_text_page(parent, "Settings", FT_ICON_SETTINGS,
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
    size_t i;
    for (i = 0U; i < sizeof(s_start_app_buttons) / sizeof(s_start_app_buttons[0]); i++)
        if (s_start_app_buttons[i] != RT_NULL && lv_obj_is_valid(s_start_app_buttons[i]))
            lv_obj_set_style_bg_opa(s_start_app_buttons[i], preferences->tile_opa, LV_PART_MAIN);
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
{ LV_UNUSED(event); s_media_playing = !s_media_playing; update_media_labels(); }
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
{ return i < sizeof(s_apps) / sizeof(s_apps[0]) ? s_start_app_buttons[i] : RT_NULL; }
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
    return true;
}
bool ft_pages_test_start_is_active(void)
{ return s_home_tileview != RT_NULL && lv_obj_is_valid(s_home_tileview) && lv_tileview_get_tile_active(s_home_tileview) == s_start_tile; }
bool ft_pages_test_apps_is_active(void)
{ return s_home_tileview != RT_NULL && lv_obj_is_valid(s_home_tileview) && lv_tileview_get_tile_active(s_home_tileview) == s_apps_tile; }
size_t ft_pages_test_accent_count(void) { return FT_ACCENT_COUNT; }
uint32_t ft_pages_test_accent_rgb(size_t i) { return i < FT_ACCENT_COUNT ? s_accent_rgb[i] : 0U; }
#endif
