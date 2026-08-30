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
#define FT_SETTINGS_COUNT    7U
#define FT_TIME_FORMAT_COUNT 2U
#define FT_TIMEZONE_COUNT    7U
#define FT_SYSTEM_SUMMARY_COUNT 4U
#define FT_SYSTEM_SECTION_COUNT 4U

typedef enum
{
    FT_SYSTEM_FIELD_SOC = 0,
    FT_SYSTEM_FIELD_PROCESSORS,
    FT_SYSTEM_FIELD_ACCELERATORS,
    FT_SYSTEM_FIELD_SOFTWARE,
    FT_SYSTEM_FIELD_DISPLAY,
    FT_SYSTEM_FIELD_TOUCH,
    FT_SYSTEM_FIELD_FLASH,
    FT_SYSTEM_FIELD_M55_IMAGE,
    FT_SYSTEM_FIELD_HYPERRAM,
    FT_SYSTEM_FIELD_EXTERNAL_HEAP,
    FT_SYSTEM_FIELD_ONCHIP_RAM,
    FT_SYSTEM_FIELD_INTERNAL_HEAP,
    FT_SYSTEM_FIELD_RRAM,
    FT_SYSTEM_FIELD_GFX_MEMORY,
    FT_SYSTEM_FIELD_FLASH_BUS,
    FT_SYSTEM_FIELD_RAM_BUS,
    FT_SYSTEM_FIELD_DISPLAY_LINK,
    FT_SYSTEM_FIELD_CONSOLE,
    FT_SYSTEM_FIELD_I2C,
    FT_SYSTEM_FIELD_IPC,
    FT_SYSTEM_FIELD_BACKLIGHT,
    FT_SYSTEM_FIELD_DEVICES,
    FT_SYSTEM_FIELD_UNAVAILABLE,
    FT_SYSTEM_FIELD_COUNT
} ft_system_field_t;

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
static lv_obj_t *create_settings_time_language_page(lv_obj_t *parent);
static lv_obj_t *create_settings_personalization_page(lv_obj_t *parent);
static void settings_time_language_refresh(void);
static void language_refresh_async_cb(void *user_data);
static void media_tile_live_content(lv_obj_t *content_host,
                                    uint32_t frame, void *context);
static void messages_tile_live_content(lv_obj_t *content_host,
                                       uint32_t frame, void *context);

static const ft_app_descriptor_t s_apps[] =
{
    {FT_PAGE_SETTINGS,
     {"Settings", 2U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_SETTINGS, false, 0U, RT_NULL, RT_NULL}},
    {FT_PAGE_MEDIA,
     {"Media", 1U, 1U, 255U, FT_ICON_MEDIA_PATTERN},
     {FT_ICON_MEDIA, true, 1600U, media_tile_live_content, RT_NULL}},
    {FT_PAGE_MESSAGES,
     {"Messages", 1U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_MESSAGES, true, 2200U, messages_tile_live_content, RT_NULL}},
    {FT_PAGE_FILES,
     {"Files", 2U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_FILES, false, 0U, RT_NULL, RT_NULL}},
};

static const char *s_app_names_zh[] = {"设置", "媒体", "消息", "文件"};

typedef struct
{
    ft_page_id_t page_id;
    ft_icon_id_t icon_id;
    const char *title;
    const char *summary;
    const char *keywords;
    const char *title_zh;
    const char *summary_zh;
    const char *keywords_zh;
} ft_settings_entry_t;

typedef struct
{
    int16_t offset_minutes;
    const char *label;
} ft_timezone_entry_t;

static const ft_settings_entry_t s_settings[FT_SETTINGS_COUNT] =
{
    {FT_PAGE_SETTINGS_DISPLAY, FT_ICON_DISPLAY, "Display & brightness",
     "Backlight level and panel information", "screen pwm panel brightness display",
     "显示和亮度", "背光亮度与显示面板信息", "屏幕 背光 亮度 显示"},
    {FT_PAGE_SETTINGS_WIFI, FT_ICON_WIFI_SETTINGS, "Wi-Fi",
     "Wireless network state and signal", "wifi wlan wireless network signal",
     "Wi-Fi", "无线网络状态与信号", "无线 网络 信号"},
    {FT_PAGE_SETTINGS_BLUETOOTH, FT_ICON_BLUETOOTH_SETTINGS, "Bluetooth",
     "Radio and connection state", "ble device radio wireless",
     "蓝牙", "蓝牙开关与连接状态", "蓝牙 设备 无线 连接"},
    {FT_PAGE_SETTINGS_TIME_LANGUAGE, FT_ICON_TIME_LANGUAGE, "Time & language",
     "Clock format, time zone and display language", "time clock timezone language locale",
     "时间和语言", "时间格式、时区和界面语言", "时间 时钟 时区 语言"},
    {FT_PAGE_SETTINGS_PERSONALIZATION, FT_ICON_PERSONALIZATION, "Personalization",
     "Accent, Start Tile opacity and background", "theme color tile appearance",
     "个性化", "强调色、开始标签透明度和背景", "主题 颜色 标签 外观 背景"},
    {FT_PAGE_SYSTEM, FT_ICON_SYSTEM, "System information",
     "Processor, memory, storage and interfaces", "soc cpu ram flash clocks devices system",
     "系统信息", "处理器、内存、存储和接口", "系统 处理器 内存 存储 时钟 外设"},
    {FT_PAGE_ABOUT, FT_ICON_ABOUT, "About FeatherTalk",
     "Product, firmware and IPC versions", "about version firmware product ipc",
     "关于 FeatherTalk", "产品、固件与 IPC 版本", "关于 版本 固件 产品"},
};

static const ft_timezone_entry_t s_timezones[FT_TIMEZONE_COUNT] =
{
    {-480, "UTC-08:00"},
    {-300, "UTC-05:00"},
    {   0, "UTC+00:00"},
    {  60, "UTC+01:00"},
    { 330, "UTC+05:30"},
    { 480, "UTC+08:00"},
    { 540, "UTC+09:00"},
};

static const ft_icon_id_t s_system_summary_icons[FT_SYSTEM_SUMMARY_COUNT] =
{
    FT_ICON_STORAGE,
    FT_ICON_EXTERNAL_MEMORY,
    FT_ICON_ONCHIP_MEMORY,
    FT_ICON_PROCESSOR,
};
static const uint32_t s_accent_rgb[FT_ACCENT_COUNT] =
    {0x0078D7UL, 0xE81123UL, 0x107C10UL, 0xFFB900UL, 0x744DA9UL};
static const uint8_t s_opacity_values[FT_OPACITY_COUNT] = {160U, 210U, 255U};
static const char *s_background_names_en[FT_BACKGROUND_COUNT] =
    {"Black", "Dark", "Accent"};
static const char *s_background_names_zh[FT_BACKGROUND_COUNT] =
    {"纯黑", "深色", "强调色"};
static const char *s_tracks_en[] = {"Feather Intro", "PSoC Skyline", "Metro Pulse"};
/* Keep product, platform and protocol keywords intact across languages.
 * "Feather" is a product-family term here, not the noun "羽翼". */
static const char *s_tracks_zh[] = {"Feather 序曲", "PSoC 天际线", "都市脉冲"};

static const ft_page_definition_t s_pages[] =
{
    {FT_PAGE_HOME, "Start", create_home_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SEARCH, "Search", create_search_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SYSTEM, "System information", create_system_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS, "Settings", create_settings_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_MEDIA, "Media", create_media_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_MESSAGES, "Messages", create_messages_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_FILES, "Files", create_files_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_ABOUT, "About", create_about_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_DISPLAY, "Display & brightness", create_settings_display_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_WIFI, "Wi-Fi", create_settings_wifi_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_BLUETOOTH, "Bluetooth", create_settings_bluetooth_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_TIME_LANGUAGE, "Time & language", create_settings_time_language_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_PERSONALIZATION, "Personalization", create_settings_personalization_page, RT_NULL, RT_NULL, RT_NULL},
};

static lv_obj_t *s_home_tileview;
static lv_obj_t *s_start_tile;
static lv_obj_t *s_apps_tile;
static lv_obj_t *s_system_status_label;
static lv_obj_t *s_system_metrics_label;
static lv_obj_t *s_system_summary_values[FT_SYSTEM_SUMMARY_COUNT];
static lv_obj_t *s_system_summary_notes[FT_SYSTEM_SUMMARY_COUNT];
static lv_obj_t *s_system_section_headers[FT_SYSTEM_SECTION_COUNT];
static lv_obj_t *s_system_section_contents[FT_SYSTEM_SECTION_COUNT];
static lv_obj_t *s_system_section_chevrons[FT_SYSTEM_SECTION_COUNT];
static lv_obj_t *s_system_fields[FT_SYSTEM_FIELD_COUNT];
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
static lv_obj_t *s_time_format_buttons[FT_TIME_FORMAT_COUNT];
static lv_obj_t *s_timezone_dropdown;
static lv_obj_t *s_language_buttons[FT_LANGUAGE_COUNT];
static lv_obj_t *s_time_preview;
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
static bool s_language_refresh_scheduled;

static const char *app_display_name(size_t index)
{
    if (index >= sizeof(s_apps) / sizeof(s_apps[0])) return "";
    return ft_preferences_text(s_app_names_zh[index], s_apps[index].tile.name);
}

static const char *track_display_name(size_t index)
{
    if (index >= sizeof(s_tracks_en) / sizeof(s_tracks_en[0])) return "";
    return ft_preferences_text(s_tracks_zh[index], s_tracks_en[index]);
}

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
    size_t track_count = sizeof(s_tracks_en) / sizeof(s_tracks_en[0]);
    lv_obj_t *label = tile_live_text_label(content_host);
    LV_UNUSED(context);
    if (label != RT_NULL)
        lv_label_set_text(label,
                          track_display_name(((uint32_t)s_media_track + frame) % track_count));
}

static void messages_tile_live_content(lv_obj_t *content_host,
                                       uint32_t frame, void *context)
{
    static char text[32];
    lv_obj_t *label = tile_live_text_label(content_host);
    LV_UNUSED(context);
    if (label == RT_NULL) return;
    if ((frame & 1U) != 0U)
        lv_label_set_text(label, ft_preferences_text("点按打开收件箱",
                                                     "Tap to open inbox"));
    else if (s_message_count == 0U)
        lv_label_set_text(label, ft_preferences_text("没有未读消息",
                                                     "No unread messages"));
    else
    {
        lv_snprintf(text, sizeof(text),
                    ft_preferences_text("%lu 条未读", "%lu unread"),
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
    create_home_header(page,
                       ft_preferences_text("开始", "Start"),
                       layout->compact ?
                       ft_preferences_text("应用  >", "Apps  >") :
                       ft_preferences_text("向左滑动查看全部应用  >",
                                           "swipe left for all apps  >"));
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
    create_home_header(page,
                       ft_preferences_text("全部应用", "All apps"),
                       layout->compact ?
                       ft_preferences_text("<  开始", "<  Start") :
                       ft_preferences_text("<  向右滑动", "<  swipe right"));
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
        lv_label_set_text(label, app_display_name(i));
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
        if (contains_ignore_case(s_apps[i].tile.name, query) ||
            contains_ignore_case(s_app_names_zh[i], query))
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
    page = create_text_page(root, ft_preferences_text("搜索", "Search"), FT_ICON_SEARCH,
                            ft_preferences_text("在设备上搜索应用", "Local application search"));
    spinner = lv_spinner_create(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_align(page, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(spinner, ft_layout_px(44), ft_layout_px(44));
    lv_spinner_set_anim_params(spinner, 900U, 200U);
    lv_obj_set_style_arc_color(spinner, ft_ui_accent_color(), LV_PART_INDICATOR);
    track_object(&s_search_box, lv_textarea_create(page));
    lv_obj_set_size(s_search_box, lv_pct(100), layout->control_height);
    lv_textarea_set_one_line(s_search_box, true);
    lv_textarea_set_placeholder_text(s_search_box,
                                     ft_preferences_text("搜索应用", "Search apps"));
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
                     create_icon_button(results, s_apps[i].app.app_icon, app_display_name(i),
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
                                    ft_preferences_text(LV_SYMBOL_DOWN "  收起键盘",
                                                        LV_SYMBOL_DOWN "  Hide keyboard"),
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

static void system_label_set_text(lv_obj_t **slot, const char *text)
{
    if (tracked_object_is_type(slot, &lv_label_class))
    {
        const char *current = lv_label_get_text(*slot);
        if (current == RT_NULL || strcmp(current, text != RT_NULL ? text : "") != 0)
            lv_label_set_text(*slot, text != RT_NULL ? text : "");
    }
}

static void system_summary_set(size_t index, const char *value, const char *note)
{
    if (index >= FT_SYSTEM_SUMMARY_COUNT) return;
    system_label_set_text(&s_system_summary_values[index], value);
    system_label_set_text(&s_system_summary_notes[index], note);
}

static void system_section_set_open(size_t index, bool open)
{
    lv_obj_t *header;
    lv_obj_t *content;
    lv_obj_t *chevron;
    if (index >= FT_SYSTEM_SECTION_COUNT) return;
    header = s_system_section_headers[index];
    content = s_system_section_contents[index];
    chevron = s_system_section_chevrons[index];
    if (header == RT_NULL || content == RT_NULL || chevron == RT_NULL ||
        !lv_obj_is_valid(header) || !lv_obj_is_valid(content) ||
        !lv_obj_is_valid(chevron)) return;
    if (open)
        lv_obj_remove_flag(content, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(chevron, open ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    lv_obj_set_style_border_side(header,
                                 open ? LV_BORDER_SIDE_BOTTOM : LV_BORDER_SIDE_NONE,
                                 LV_PART_MAIN);
}

static void system_section_clicked_cb(lv_event_t *event)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *content;
    if (index >= FT_SYSTEM_SECTION_COUNT) return;
    content = s_system_section_contents[index];
    if (content == RT_NULL || !lv_obj_is_valid(content)) return;
    system_section_set_open(index,
                            lv_obj_has_flag(content, LV_OBJ_FLAG_HIDDEN));
}

static lv_obj_t *create_system_section(lv_obj_t *page, size_t index,
                                       const char *title, bool open)
{
    lv_obj_t *section;
    lv_obj_t *header;
    lv_obj_t *title_label;
    lv_obj_t *content;
    lv_obj_t *chevron;
    RT_ASSERT(index < FT_SYSTEM_SECTION_COUNT);

    section = lv_obj_create(page);
    ft_ui_style_panel(section);
    lv_obj_set_width(section, lv_pct(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(section, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_set_style_border_width(section, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(section, lv_color_hex(0x343434), LV_PART_MAIN);
    lv_obj_set_style_pad_all(section, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(section, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    header = track_object(&s_system_section_headers[index], lv_obj_create(section));
    style_layout_container(header);
    lv_obj_set_size(header, lv_pct(100), ft_layout_get()->control_height);
    lv_obj_set_style_pad_left(header, ft_layout_px(14), LV_PART_MAIN);
    lv_obj_set_style_pad_right(header, ft_layout_px(14), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
    ft_ui_register_accent(header, FT_ACCENT_BORDER);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, system_section_clicked_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);
    title_label = lv_label_create(header);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);
    chevron = track_object(&s_system_section_chevrons[index],
                           lv_label_create(header));
    lv_obj_set_style_text_color(chevron, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(chevron, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);

    content = track_object(&s_system_section_contents[index],
                           lv_obj_create(section));
    style_layout_container(content);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content, ft_layout_px(14), LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, ft_layout_px(7), LV_PART_MAIN);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    system_section_set_open(index, open);
    return content;
}

static lv_obj_t *create_system_value_row(lv_obj_t *content, const char *key,
                                         lv_obj_t **value_slot)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_t *key_label;
    lv_obj_t *value;
    style_layout_container(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_bottom(row, ft_layout_px(5), LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    key_label = lv_label_create(row);
    lv_label_set_text(key_label, key);
    lv_obj_set_width(key_label, layout->compact ? ft_layout_px(100) :
                                               ft_layout_px(126));
    lv_obj_set_style_text_color(key_label, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(key_label, ft_layout_font(13), LV_PART_MAIN);
    value = track_object(value_slot, lv_label_create(row));
    lv_obj_set_width(value, 0);
    lv_obj_set_flex_grow(value, 1);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(value, ft_layout_font(14), LV_PART_MAIN);
    return value;
}

static lv_obj_t *create_system_field_row(lv_obj_t *content,
                                         ft_system_field_t field,
                                         const char *key)
{
    RT_ASSERT(field < FT_SYSTEM_FIELD_COUNT);
    return create_system_value_row(content, key, &s_system_fields[field]);
}

static lv_obj_t *create_system_summary_card(lv_obj_t *grid, size_t index,
                                            int32_t width, int32_t height,
                                            ft_icon_id_t icon_id,
                                            const char *title)
{
    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_t *title_row;
    lv_obj_t *title_label;
    lv_obj_t *icon;
    lv_obj_t *value;
    lv_obj_t *note;
    ft_ui_style_panel(card);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x343434), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, ft_layout_px(5), LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    title_row = lv_obj_create(card);
    style_layout_container(title_row);
    lv_obj_set_size(title_row, lv_pct(100), ft_layout_px(24));
    lv_obj_set_style_pad_column(title_row, ft_layout_px(7), LV_PART_MAIN);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    icon = ft_icon_create(title_row, icon_id, ft_layout_icon_size(24U), true);
    LV_UNUSED(icon);
    title_label = lv_label_create(title_row);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, ft_layout_font(13), LV_PART_MAIN);

    value = track_object(&s_system_summary_values[index], lv_label_create(card));
    lv_label_set_text(value, "--");
    lv_obj_set_width(value, lv_pct(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(value, ft_layout_font(20), LV_PART_MAIN);
    note = track_object(&s_system_summary_notes[index], lv_label_create(card));
    lv_label_set_text(note, ft_preferences_text("正在采集…", "Collecting..."));
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(note, lv_color_hex(0x9A9A9A), LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ft_layout_font(12), LV_PART_MAIN);
    return card;
}

static void create_system_summary_grid(lv_obj_t *page)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    static const char *titles_en[FT_SYSTEM_SUMMARY_COUNT] =
        {"Storage", "External RAM", "On-chip RAM", "Processor"};
    static const char *titles_zh[FT_SYSTEM_SUMMARY_COUNT] =
        {"存储", "片外内存", "片上内存", "处理器"};
    lv_obj_t *grid = lv_obj_create(page);
    int32_t columns = (layout->screen_width >= 700 ||
                       (layout->landscape && layout->screen_width >= 600)) ? 4 : 2;
    int32_t gap = layout->tile_gap;
    int32_t available = layout->screen_width - 2 * layout->page_padding;
    int32_t card_width = (available - (columns - 1) * gap) / columns;
    int32_t card_height = ft_layout_px(124);
    size_t i;
    if (card_height < 96) card_height = 96;
    style_layout_container(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(grid, gap, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    for (i = 0U; i < FT_SYSTEM_SUMMARY_COUNT; i++)
        (void)create_system_summary_card(grid, i, card_width, card_height,
                                         s_system_summary_icons[i],
                                         ft_preferences_text(titles_zh[i], titles_en[i]));
}

static void refresh_system_hardware(void)
{
    ft_platform_system_info_t info;
    char text[384];
    char note[128];
    uint32_t xip_percent;
    uint32_t heap_percent;
    uint32_t hyperram_percent;

    ft_platform_get_system_info(&info);
    xip_percent = info.firmware_capacity_bytes != 0U ?
                  (uint32_t)(((uint64_t)info.firmware_used_bytes * 100U) /
                             info.firmware_capacity_bytes) : 0U;
    heap_percent = info.internal_heap_total != 0U ?
                   (uint32_t)(((uint64_t)info.internal_heap_used * 100U) /
                              info.internal_heap_total) : 0U;
    hyperram_percent = info.external_heap_total != 0U ?
                       (uint32_t)(((uint64_t)info.external_heap_used * 100U) /
                                  info.external_heap_total) : 0U;

    lv_snprintf(text, sizeof(text), "%lu MiB",
                (unsigned long)(info.external_flash_bytes / (1024U * 1024U)));
    lv_snprintf(note, sizeof(note),
                ft_preferences_text("M55 镜像 %lu KiB / 8 MiB",
                                    "M55 image %lu KiB / 8 MiB"),
                (unsigned long)(info.firmware_used_bytes / 1024U));
    system_summary_set(0U, text, note);
    lv_snprintf(text, sizeof(text), "%lu MiB",
                (unsigned long)(info.external_hyperram_bytes / (1024U * 1024U)));
    lv_snprintf(note, sizeof(note),
                ft_preferences_text("堆已用 %lu KiB / %lu KiB",
                                    "Heap %lu KiB used / %lu KiB"),
                (unsigned long)(info.external_heap_used / 1024U),
                (unsigned long)(info.external_heap_total / 1024U));
    system_summary_set(1U, text, note);
    lv_snprintf(text, sizeof(text), "%lu.%02lu MiB",
                (unsigned long)(info.onchip_ram_bytes / (1024U * 1024U)),
                (unsigned long)(((info.onchip_ram_bytes % (1024U * 1024U)) * 100U) /
                                (1024U * 1024U)));
    lv_snprintf(note, sizeof(note),
                ft_preferences_text("堆已用 %lu KiB / %lu KiB",
                                    "Heap %lu KiB used / %lu KiB"),
                (unsigned long)(info.internal_heap_used / 1024U),
                (unsigned long)(info.internal_heap_total / 1024U));
    system_summary_set(2U, text, note);
    lv_snprintf(note, sizeof(note), "M55 %lu MHz | M33 %lu MHz",
                (unsigned long)(info.m55_core_hz / 1000000UL),
                (unsigned long)(info.m33_domain_hz / 1000000UL));
    system_summary_set(3U, "PSoC Edge E84", note);

    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_SOC],
                          "PSE846GPS2DBZC4A");
    lv_snprintf(text, sizeof(text),
                ft_preferences_text(
                "Cortex-M55 %lu MHz（指令缓存%s，数据缓存%s）+ Cortex-M33 %lu MHz",
                "Cortex-M55 %lu MHz (I-cache %s, D-cache %s) + Cortex-M33 %lu MHz"),
                (unsigned long)(info.m55_core_hz / 1000000UL),
                info.instruction_cache_enabled ? ft_preferences_text("开启", "on") :
                                                 ft_preferences_text("关闭", "off"),
                info.data_cache_enabled ? ft_preferences_text("开启", "on") :
                                          ft_preferences_text("关闭", "off"),
                (unsigned long)(info.m33_domain_hz / 1000000UL));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_PROCESSORS], text);
    lv_snprintf(text, sizeof(text), "Ethos-U55 NPU %lu MHz; GFXSS %lu MHz",
                (unsigned long)(info.npu_hz / 1000000UL),
                (unsigned long)(info.gfx_hz / 1000000UL));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_ACCELERATORS], text);
    lv_snprintf(text, sizeof(text), "RT-Thread, %u Hz tick; LVGL 9.2",
                RT_TICK_PER_SECOND);
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_SOFTWARE], text);
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_DISPLAY],
                          ft_preferences_text("480 x 800 RGB565；扫描步长 512 像素",
                                              "480 x 800 RGB565; 512-pixel scanout stride"));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_TOUCH],
                          ft_preferences_text("ST7102/ST7123 电容触控；长按 500 ms",
                                              "ST7102/ST7123 capacitive touch; 500 ms long press"));

    lv_snprintf(text, sizeof(text),
                ft_preferences_text(
                "S25FS128S QSPI NOR，物理容量 %lu MiB；2.25 MiB 未分配",
                "S25FS128S QSPI NOR, %lu MiB physical; 2.25 MiB unassigned"),
                (unsigned long)(info.external_flash_bytes / (1024U * 1024U)));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_FLASH], text);
    lv_snprintf(text, sizeof(text),
                ft_preferences_text("%lu / %lu KiB（%lu%%），8 MiB XIP 分区",
                                    "%lu / %lu KiB (%lu%%), 8 MiB XIP slot"),
                (unsigned long)(info.firmware_used_bytes / 1024U),
                (unsigned long)(info.firmware_capacity_bytes / 1024U),
                (unsigned long)xip_percent);
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_M55_IMAGE], text);
    lv_snprintf(text, sizeof(text),
                ft_preferences_text(
                "S70KS1283，%lu MiB；M33 2 MiB + M55 2 MiB + 共享 12 MiB",
                "S70KS1283, %lu MiB; M33 2 MiB + M55 2 MiB + shared 12 MiB"),
                (unsigned long)(info.external_hyperram_bytes / (1024U * 1024U)));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_HYPERRAM], text);
    lv_snprintf(text, sizeof(text),
                ft_preferences_text("%lu / %lu KiB（%lu%%），峰值 %lu KiB",
                                    "%lu / %lu KiB (%lu%%), peak %lu KiB"),
                (unsigned long)(info.external_heap_used / 1024U),
                (unsigned long)(info.external_heap_total / 1024U),
                (unsigned long)hyperram_percent,
                (unsigned long)(info.external_heap_peak / 1024U));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_EXTERNAL_HEAP], text);
    lv_snprintf(text, sizeof(text),
                ft_preferences_text(
                "共 %lu.%02lu MiB：512 KiB M55 TCM + 1 MiB M33 SRAM + 5 MiB SoC 内存",
                "%lu.%02lu MiB total: 512 KiB M55 TCM + 1 MiB M33 SRAM + 5 MiB SoC memory"),
                (unsigned long)(info.onchip_ram_bytes / (1024U * 1024U)),
                (unsigned long)(((info.onchip_ram_bytes % (1024U * 1024U)) * 100U) /
                                (1024U * 1024U)));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_ONCHIP_RAM], text);
    lv_snprintf(text, sizeof(text),
                ft_preferences_text("%lu / %lu KiB（%lu%%），峰值 %lu KiB",
                                    "%lu / %lu KiB (%lu%%), peak %lu KiB"),
                (unsigned long)(info.internal_heap_used / 1024U),
                (unsigned long)(info.internal_heap_total / 1024U),
                (unsigned long)heap_percent,
                (unsigned long)(info.internal_heap_peak / 1024U));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_INTERNAL_HEAP], text);
    lv_snprintf(text, sizeof(text),
                ft_preferences_text(
                "物理容量 %lu KiB；用户可寻址 328 KiB；固件从 SMIF 运行",
                "%lu KiB physical; 328 KiB user-addressable; firmware runs from SMIF"),
                (unsigned long)(info.onchip_rram_bytes / 1024U));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_RRAM], text);
    lv_snprintf(text, sizeof(text), "%lu / %lu KiB; DTCM static %lu / %lu KiB",
                (unsigned long)(info.gfx_used_bytes / 1024U),
                (unsigned long)(info.gfx_capacity_bytes / 1024U),
                (unsigned long)(info.dtcm_static_bytes / 1024U),
                (unsigned long)(info.dtcm_capacity_bytes / 1024U));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_GFX_MEMORY], text);

    lv_snprintf(text, sizeof(text), "SMIF0 %lu MHz, x4 Quad-SDR XIP",
                (unsigned long)(info.flash_smif_hz / 1000000UL));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_FLASH_BUS], text);
    lv_snprintf(text, sizeof(text), "SMIF1 %lu MHz, x8 DDR HyperBus",
                (unsigned long)(info.hyperram_smif_hz / 1000000UL));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_RAM_BUS], text);
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_DISPLAY_LINK],
                          ft_preferences_text(
                          "MIPI-DSI 2 x 900 Mb/s；像素时钟 33.984 MHz；AXI-DMA >= 8 KiB",
                          "MIPI-DSI 2 x 900 Mb/s; 33.984 MHz pixel clock; AXI-DMA >= 8 KiB"));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_CONSOLE],
                          ft_preferences_text("UART2，115200 波特率，8-N-1，MSH 已启用",
                                              "UART2, 115200 baud, 8-N-1, MSH enabled"));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_I2C],
                          ft_preferences_text("I2C0 100 kHz；触控使用软件 I2C1",
                                              "I2C0 100 kHz; software I2C1 for touch"));
    lv_snprintf(text, sizeof(text),
                ft_preferences_text("M33/M55 片上管道，ABI %u，16 字节帧",
                                    "M33/M55 on-chip pipe, ABI %u, 16-byte frames"),
                (unsigned)FEATHERTALK_IPC_ABI_VERSION);
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_IPC], text);
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_BACKLIGHT],
                          ft_preferences_text("PWM18 5 kHz；UI 0-100% 映射至 50-100% 占空比",
                                              "PWM18 5 kHz; UI 0-100% maps to 50-100% duty"));
    lv_snprintf(text, sizeof(text),
                ft_preferences_text("已注册 %u 个：%s", "%u registered: %s"),
                (unsigned)info.registered_device_count,
                info.registered_devices[0] != '\0' ? info.registered_devices :
                                                     ft_preferences_text("无", "none"));
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_DEVICES], text);
    system_label_set_text(&s_system_fields[FT_SYSTEM_FIELD_UNAVAILABLE],
                          ft_preferences_text(
                          "Wi-Fi/蓝牙、音频、SDHC、USB、CAN-FD、I3C、PDM 和 TDM 驱动",
                          "Wi-Fi/Bluetooth, audio, SDHC, USB, CAN-FD, I3C, PDM and TDM drivers"));
}

static lv_obj_t *create_system_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *breadcrumb;
    lv_obj_t *subtitle;
    lv_obj_t *content;
    ft_ui_style_page(page);
    lv_obj_set_style_pad_all(page, layout->page_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page, layout->section_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);

    breadcrumb = lv_label_create(page);
    lv_label_set_text(breadcrumb,
                      ft_preferences_text("设置  >  系统信息",
                                          "Settings  >  System information"));
    lv_obj_set_width(breadcrumb, lv_pct(100));
    lv_label_set_long_mode(breadcrumb, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(breadcrumb, ft_layout_font(22), LV_PART_MAIN);
    ft_ui_register_accent(breadcrumb, FT_ACCENT_TEXT);
    subtitle = lv_label_create(page);
    lv_label_set_text(subtitle, "FeatherTalk | PSE846GPS2DBZC4A");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(subtitle, ft_layout_font(14), LV_PART_MAIN);

    create_system_summary_grid(page);
    content = create_system_section(page, 0U,
                                    ft_preferences_text("设备规格", "Device specifications"),
                                    true);
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_SOC, "SoC");
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_PROCESSORS,
                                  ft_preferences_text("处理器", "Processors"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_ACCELERATORS,
                                  ft_preferences_text("加速器", "Accelerators"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_SOFTWARE,
                                  ft_preferences_text("系统", "System"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_DISPLAY,
                                  ft_preferences_text("显示", "Display"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_TOUCH,
                                  ft_preferences_text("触控", "Touch"));

    content = create_system_section(page, 1U,
                                    ft_preferences_text("内存和存储", "Memory & storage"),
                                    false);
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_FLASH,
                                  ft_preferences_text("存储", "Storage"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_M55_IMAGE,
                                  ft_preferences_text("M55 镜像", "M55 image"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_HYPERRAM,
                                  ft_preferences_text("片外内存", "External RAM"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_EXTERNAL_HEAP,
                                  ft_preferences_text("片外堆", "External heap"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_ONCHIP_RAM,
                                  ft_preferences_text("片上内存", "On-chip RAM"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_INTERNAL_HEAP,
                                  ft_preferences_text("片内堆", "Internal heap"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_RRAM, "RRAM");
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_GFX_MEMORY, "GFX / DTCM");

    content = create_system_section(page, 2U,
                                    ft_preferences_text("接口和外设", "Interfaces & peripherals"),
                                    false);
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_FLASH_BUS,
                                  ft_preferences_text("Flash 总线", "Flash bus"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_RAM_BUS,
                                  ft_preferences_text("内存总线", "RAM bus"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_DISPLAY_LINK,
                                  ft_preferences_text("显示链路", "Display link"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_CONSOLE,
                                  ft_preferences_text("控制台", "Console"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_I2C, "I2C");
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_IPC,
                                  ft_preferences_text("双核 IPC", "Dual-core IPC"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_BACKLIGHT,
                                  ft_preferences_text("背光", "Backlight"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_DEVICES,
                                  ft_preferences_text("RT 设备", "RT devices"));
    (void)create_system_field_row(content, FT_SYSTEM_FIELD_UNAVAILABLE,
                                  ft_preferences_text("未启用", "Unavailable"));

    content = create_system_section(page, 3U,
                                    ft_preferences_text("运行状态", "Runtime status"),
                                    false);
    (void)create_system_value_row(content,
                                  ft_preferences_text("M33 运行状态", "M33 runtime"),
                                  &s_system_status_label);
    lv_label_set_text(s_system_status_label,
                      ft_preferences_text("正在等待 M33 系统状态 IPC…",
                                          "Waiting for M33 system-status IPC..."));
    (void)create_system_value_row(content,
                                  ft_preferences_text("UI 运行状态", "UI runtime"),
                                  &s_system_metrics_label);
    lv_label_set_text(s_system_metrics_label,
                      ft_preferences_text("等待性能采样…",
                                          "Performance sample pending..."));
    refresh_system_hardware();
    return page;
}
void ft_pages_update_system_status(const char *system_text, const char *metrics_text)
{
    system_label_set_text(&s_system_status_label, system_text);
    system_label_set_text(&s_system_metrics_label, metrics_text);
    if (s_system_summary_values[0] != RT_NULL) refresh_system_hardware();
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
                     contains_ignore_case(s_settings[i].keywords, query) ||
                     contains_ignore_case(s_settings[i].title_zh, query) ||
                     contains_ignore_case(s_settings[i].summary_zh, query) ||
                     contains_ignore_case(s_settings[i].keywords_zh, query);
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
    lv_label_set_text(title, ft_preferences_text(entry->title_zh, entry->title));
    lv_obj_set_width(title, lv_pct(70));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, ft_layout_px(58), ft_layout_px(10));
    summary = lv_label_create(button);
    lv_label_set_text(summary, ft_preferences_text(entry->summary_zh, entry->summary));
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
    page = create_text_page(root,
                            ft_preferences_text("设置", "Settings"), FT_ICON_SETTINGS,
                            ft_preferences_text("仅显示这款 Edgi-Talk 开发板支持的设置",
                                                "Hardware-aware settings for this Edgi-Talk board"));
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_align(page, LV_ALIGN_CENTER, 0, 0);
    track_object(&s_settings_search_box, lv_textarea_create(page));
    lv_obj_set_size(s_settings_search_box, lv_pct(100), layout->control_height);
    lv_textarea_set_one_line(s_settings_search_box, true);
    lv_textarea_set_placeholder_text(s_settings_search_box,
                                     ft_preferences_text("搜索设置", "Search settings"));
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
                                    ft_preferences_text(LV_SYMBOL_DOWN "  收起键盘",
                                                        LV_SYMBOL_DOWN "  Hide keyboard"),
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
        lv_label_set_text(s_settings_brightness_value,
                          ft_preferences_text("不可用", "Unavailable"));
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
    lv_obj_t *page = create_text_page(parent,
                                      ft_preferences_text("显示和亮度",
                                                          "Display & brightness"),
                                      FT_ICON_DISPLAY,
                                      ft_preferences_text(
                                      "界面中的 0-100% 亮度映射为安全的 50-100% 面板 PWM 占空比。",
                                      "The visible 0-100% range maps to a safe 50-100% panel PWM duty range."));
    lv_obj_t *caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("亮度", "Brightness"));
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
    lv_label_set_text(caption, ft_preferences_text(
                      "面板：480 x 800，竖屏\n自动旋转：不可用（驱动未启用）",
                      "Panel: 480 x 800, portrait\nAuto-rotation: unavailable (driver not enabled)"));
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
        lv_label_set_text(s_settings_radio_status,
                          ft_preferences_text("请求已发送到 M33，正在等待状态更新…",
                                              "Request sent to M33; waiting for status update..."));
}

static lv_obj_t *create_settings_radio_page(lv_obj_t *parent,
                                            feathertalk_quick_control_t control)
{
    bool wifi = control == FEATHERTALK_QUICK_WIFI;
    const char *title = wifi ? "Wi-Fi" : ft_preferences_text("蓝牙", "Bluetooth");
    ft_icon_id_t icon = wifi ? FT_ICON_WIFI_SETTINGS : FT_ICON_BLUETOOTH_SETTINGS;
    feathertalk_quick_status_t status;
    uint8_t bit = (uint8_t)(1U << control);
    bool valid = feathertalk_ipc_get_quick_status(&status) == RT_EOK;
    bool available = valid && (status.capabilities & bit) != 0U;
    bool enabled = available && (status.enabled & bit) != 0U;
    bool connected = enabled && (status.connected & bit) != 0U;
    char state[160];
    char action[32];
    lv_obj_t *page = create_text_page(parent, title, icon,
                                      ft_preferences_text(
                                      "此开发板仅提供 Wi-Fi 和蓝牙无线设置，不显示不存在的蜂窝网络选项。",
                                      "This board exposes only Wi-Fi and Bluetooth radio categories; cellular settings are intentionally absent."));
    track_object(&s_settings_radio_status, lv_label_create(page));
    lv_obj_set_width(s_settings_radio_status, lv_pct(100));
    lv_label_set_long_mode(s_settings_radio_status, LV_LABEL_LONG_WRAP);
    if (!available)
    {
        lv_snprintf(state, sizeof(state), ft_preferences_text(
                    "硬件类别：产品设计支持\n当前服务：不可用\nM33 驱动/能力：未启用",
                    "Hardware category: supported by the product design\n"
                    "Current service: unavailable\nM33 driver/capability: not enabled"));
        lv_snprintf(action, sizeof(action), "%s",
                    ft_preferences_text("服务不可用", "Service unavailable"));
    }
    else if (wifi)
    {
        if (connected && status.wifi_signal_percent != FEATHERTALK_SYSTEM_VALUE_UNKNOWN)
            lv_snprintf(state, sizeof(state), ft_preferences_text(
                        "无线：开启\n连接：已连接\n信号：%u%%",
                        "Radio: on\nConnection: connected\nSignal: %u%%"),
                        status.wifi_signal_percent);
        else
            lv_snprintf(state, sizeof(state),
                        ft_preferences_text("无线：%s\n连接：%s",
                                            "Radio: %s\nConnection: %s"),
                        enabled ? ft_preferences_text("开启", "on") :
                                  ft_preferences_text("关闭", "off"),
                        connected ? ft_preferences_text("已连接", "connected") :
                                    ft_preferences_text("未连接", "not connected"));
        lv_snprintf(action, sizeof(action),
                    ft_preferences_text("%s Wi-Fi", "Turn Wi-Fi %s"),
                    enabled ? ft_preferences_text("关闭", "off") :
                              ft_preferences_text("开启", "on"));
    }
    else
    {
        lv_snprintf(state, sizeof(state),
                    ft_preferences_text("无线：%s\n连接：%s",
                                        "Radio: %s\nConnection: %s"),
                    enabled ? ft_preferences_text("开启", "on") :
                              ft_preferences_text("关闭", "off"),
                    connected ? ft_preferences_text("已连接", "connected") :
                                ft_preferences_text("未连接", "not connected"));
        lv_snprintf(action, sizeof(action),
                    ft_preferences_text("%s蓝牙", "Turn Bluetooth %s"),
                    enabled ? ft_preferences_text("关闭", "off") :
                              ft_preferences_text("开启", "on"));
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

static void time_format_clicked_cb(lv_event_t *event)
{
    bool use_24_hour = (uintptr_t)lv_event_get_user_data(event) == 0U;
    ft_preferences_set_24_hour(use_24_hour);
}

static void language_refresh_async_cb(void *user_data)
{
    LV_UNUSED(user_data);
    s_language_refresh_scheduled = false;
    ft_ui_apply_language();
    (void)ft_router_refresh_all();
}

static void timezone_changed_cb(lv_event_t *event)
{
    uint32_t selected = lv_dropdown_get_selected(lv_event_get_target(event));
    if (selected < FT_TIMEZONE_COUNT)
        ft_preferences_set_timezone(s_timezones[selected].offset_minutes);
}

static void language_clicked_cb(lv_event_t *event)
{
    ft_preferences_set_language((ft_language_t)(uintptr_t)lv_event_get_user_data(event));
    if (!s_language_refresh_scheduled)
    {
        s_language_refresh_scheduled = true;
        lv_async_call(language_refresh_async_cb, RT_NULL);
    }
}

static void settings_choice_refresh(lv_obj_t *button, bool selected)
{
    if (button == RT_NULL || !lv_obj_is_valid(button)) return;
    lv_obj_set_style_bg_opa(button, selected ? LV_OPA_COVER : LV_OPA_50,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(button, selected ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_white(), LV_PART_MAIN);
}

static void settings_time_language_refresh(void)
{
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    feathertalk_system_status_t status;
    char clock[20];
    char preview[192];
    size_t i;
    size_t timezone_index = 0U;
    bool rtc_valid = false;

    settings_choice_refresh(s_time_format_buttons[0], preferences->use_24_hour);
    settings_choice_refresh(s_time_format_buttons[1], !preferences->use_24_hour);
    for (i = 0U; i < FT_LANGUAGE_COUNT; i++)
        settings_choice_refresh(s_language_buttons[i],
                                preferences->language == (ft_language_t)i);
    for (i = 0U; i < FT_TIMEZONE_COUNT; i++)
        if (s_timezones[i].offset_minutes == preferences->timezone_offset_minutes)
            timezone_index = i;
    if (s_timezone_dropdown != RT_NULL && lv_obj_is_valid(s_timezone_dropdown))
        lv_dropdown_set_selected(s_timezone_dropdown, (uint32_t)timezone_index);

    if (s_time_preview == RT_NULL || !lv_obj_is_valid(s_time_preview)) return;
    if (feathertalk_ipc_get_system_status(&status) == RT_EOK &&
        (status.flags & FEATHERTALK_SYSTEM_TIME_VALID) != 0U)
    {
        rtc_valid = true;
        ft_preferences_format_clock(status.unix_time, true, clock, sizeof(clock));
    }
    else
    {
        uint32_t uptime = rt_tick_get_millisecond() / 1000U;
        ft_preferences_format_clock(uptime, false, clock, sizeof(clock));
    }
    lv_snprintf(preview, sizeof(preview),
                ft_preferences_text(
                    "当前时间：%s\n时间来源：%s\n时区采用固定 UTC 偏移；暂不自动处理夏令时。",
                    "Current time: %s\nTime source: %s\nTime zones use fixed UTC offsets; automatic daylight saving is not available."),
                clock,
                rtc_valid ? ft_preferences_text("M33 RTC", "M33 RTC") :
                            ft_preferences_text("启动计时（RTC 未就绪）",
                                                "uptime fallback (RTC not ready)"));
    lv_label_set_text(s_time_preview, preview);
}

static lv_obj_t *create_settings_time_language_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = create_text_page(
        parent,
        ft_preferences_text("时间和语言", "Time & language"),
        FT_ICON_TIME_LANGUAGE,
        ft_preferences_text("设置状态栏时间格式、固定时区和界面语言。",
                            "Configure status-bar time, fixed time zone and display language."));
    lv_obj_t *caption;
    lv_obj_t *row;
    char timezone_options[128] = "";
    size_t i;

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("时间格式", "Time format"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    track_object(&s_time_format_buttons[0],
                 create_flat_button(row, "24 小时 / 24-hour",
                                    time_format_clicked_cb, (void *)(uintptr_t)0U));
    track_object(&s_time_format_buttons[1],
                 create_flat_button(row, "12 小时 / 12-hour",
                                    time_format_clicked_cb, (void *)(uintptr_t)1U));
    for (i = 0U; i < FT_TIME_FORMAT_COUNT; i++)
    {
        lv_obj_set_width(s_time_format_buttons[i], 0);
        lv_obj_set_flex_grow(s_time_format_buttons[i], 1);
    }

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("时区", "Time zone"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    for (i = 0U; i < FT_TIMEZONE_COUNT; i++)
    {
        if (i > 0U) strncat(timezone_options, "\n",
                            sizeof(timezone_options) - strlen(timezone_options) - 1U);
        strncat(timezone_options, s_timezones[i].label,
                sizeof(timezone_options) - strlen(timezone_options) - 1U);
    }
    track_object(&s_timezone_dropdown, lv_dropdown_create(page));
    lv_obj_set_size(s_timezone_dropdown, lv_pct(100), layout->control_height);
    lv_obj_set_style_text_font(s_timezone_dropdown, ft_layout_font(14), LV_PART_MAIN);
    lv_dropdown_set_options(s_timezone_dropdown, timezone_options);
    lv_obj_add_event_cb(s_timezone_dropdown, timezone_changed_cb,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("界面语言", "Display language"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    track_object(&s_language_buttons[FT_LANGUAGE_ZH_CN],
                 create_flat_button(row, "简体中文", language_clicked_cb,
                                    (void *)(uintptr_t)FT_LANGUAGE_ZH_CN));
    track_object(&s_language_buttons[FT_LANGUAGE_EN_US],
                 create_flat_button(row, "English", language_clicked_cb,
                                    (void *)(uintptr_t)FT_LANGUAGE_EN_US));
    for (i = 0U; i < FT_LANGUAGE_COUNT; i++)
    {
        lv_obj_set_width(s_language_buttons[i], 0);
        lv_obj_set_flex_grow(s_language_buttons[i], 1);
    }

    track_object(&s_time_preview, lv_label_create(page));
    lv_obj_set_width(s_time_preview, lv_pct(100));
    lv_label_set_long_mode(s_time_preview, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_time_preview, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_time_preview, ft_layout_font(14), LV_PART_MAIN);
    settings_time_language_refresh();
    return page;
}

static lv_obj_t *create_settings_personalization_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = create_text_page(parent,
                                      ft_preferences_text("个性化", "Personalization"),
                                      FT_ICON_PERSONALIZATION,
                                      ft_preferences_text("个性化偏好（当前保存在内存中）",
                                                          "Personalization preferences (memory backend)"));
    lv_obj_t *caption;
    lv_obj_t *row;
    size_t i;
    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("强调色", "Accent color"));
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
    lv_label_set_text(caption, ft_preferences_text("开始标签透明度",
                                                  "Start Tile opacity"));
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
    lv_label_set_text(caption, ft_preferences_text("背景", "Background"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_BACKGROUND_COUNT; i++)
    {
        track_object(&s_background_buttons[i],
                     create_flat_button(row,
                                        ft_preferences_text(s_background_names_zh[i],
                                                            s_background_names_en[i]),
                                        background_clicked_cb,
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
    ft_pages_apply_language();
    settings_time_language_refresh();
}

void ft_pages_apply_language(void)
{
    size_t i;
    for (i = 0U; i < sizeof(s_apps) / sizeof(s_apps[0]); i++)
        ft_tiles_set_localized_name(s_apps[i].page_id,
                                    s_apps[i].tile.name,
                                    s_app_names_zh[i],
                                    app_display_name(i));
}

static void update_media_labels(void)
{
    if (s_media_label != RT_NULL && lv_obj_is_valid(s_media_label))
        lv_label_set_text(s_media_label,
                          s_media_playing ? ft_preferences_text("暂停", "Pause") :
                                            ft_preferences_text("播放", "Play"));
    if (s_media_state_icon != RT_NULL && lv_obj_is_valid(s_media_state_icon))
        ft_icon_set(s_media_state_icon, s_media_playing ? FT_ICON_PAUSE : FT_ICON_PLAY,
                    ft_layout_icon_size(24U));
    if (s_media_track_label != RT_NULL && lv_obj_is_valid(s_media_track_label))
        lv_label_set_text(s_media_track_label,
                          track_display_name((size_t)s_media_track));
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
    s_media_track = (s_media_track +
                     (int32_t)(sizeof(s_tracks_en) / sizeof(s_tracks_en[0])) - 1) %
                    (int32_t)(sizeof(s_tracks_en) / sizeof(s_tracks_en[0]));
    update_media_labels();
}
static void media_next_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_media_track = (s_media_track + 1) %
                    (int32_t)(sizeof(s_tracks_en) / sizeof(s_tracks_en[0]));
    update_media_labels();
}

static lv_obj_t *create_media_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = create_text_page(parent,
                                      ft_preferences_text("媒体", "Media"), FT_ICON_MEDIA,
                                      ft_preferences_text("内存中的播放控制器",
                                                          "In-memory playback controller"));
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
                 create_icon_button(row, FT_ICON_PLAY,
                                    ft_preferences_text("播放", "Play"),
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
    lv_label_set_text(volume_caption, ft_preferences_text("音量", "Volume"));
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
    lv_snprintf(text, sizeof(text),
                ft_preferences_text("已创建通知：%lu",
                                    "Notifications created: %lu"),
                (unsigned long)s_message_count);
    lv_label_set_text(s_messages_count_label, text);
    feathertalk_ui_notify(ft_preferences_text("消息", "Messages"),
                         ft_preferences_text("测试通知", "Test notification"),
                         ft_preferences_text("内存通知投递成功。",
                                             "In-memory notification delivery succeeded."));
    feathertalk_ui_alert(ft_preferences_text("消息", "Messages"),
                        ft_preferences_text("内存通知投递成功。",
                                            "In-memory notification delivery succeeded."));
}
static lv_obj_t *create_messages_page(lv_obj_t *parent)
{
    lv_obj_t *page = create_text_page(parent,
                                      ft_preferences_text("消息", "Messages"),
                                      FT_ICON_MESSAGES,
                                      ft_preferences_text("通知服务适配器",
                                                          "Notification service adapter"));
    track_object(&s_messages_count_label, lv_label_create(page));
    lv_label_set_text(s_messages_count_label,
                      ft_preferences_text("已创建通知：0",
                                          "Notifications created: 0"));
    track_object(&s_messages_button,
                 create_flat_button(page,
                                    ft_preferences_text("创建测试通知",
                                                        "Create test notification"),
                                    message_test_cb, RT_NULL));
    lv_obj_set_width(s_messages_button, lv_pct(100));
    return page;
}

static void files_refresh_cb(lv_event_t *event)
{
    char text[160];
    LV_UNUSED(event);
    s_files_refresh_count++;
    lv_snprintf(text, sizeof(text), ft_preferences_text(
                "内部 Flash：固件/资源分区\n"
                "外部存储：不可用（驱动未启用）\n刷新次数：%lu",
                "Internal flash: firmware/resource partition\n"
                "External storage: unavailable (driver not enabled)\nRefresh count: %lu"),
                (unsigned long)s_files_refresh_count);
    lv_label_set_text(s_files_status_label, text);
}
static lv_obj_t *create_files_page(lv_obj_t *parent)
{
    lv_obj_t *page = create_text_page(parent,
                                      ft_preferences_text("文件", "Files"), FT_ICON_FILES,
                                      ft_preferences_text("存储可见性和资源策略",
                                                          "Storage visibility and resource policy"));
    track_object(&s_files_status_label, lv_label_create(page));
    lv_obj_set_width(s_files_status_label, lv_pct(100));
    lv_label_set_long_mode(s_files_status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_files_status_label, ft_preferences_text(
                      "内部 Flash：固件/资源分区\n"
                      "外部存储：不可用（驱动未启用）\n刷新次数：0",
                      "Internal flash: firmware/resource partition\n"
                      "External storage: unavailable (driver not enabled)\nRefresh count: 0"));
    track_object(&s_files_refresh_button,
                 create_icon_button(page, FT_ICON_REFRESH,
                                    ft_preferences_text("刷新", "Refresh"),
                                    files_refresh_cb, RT_NULL,
                                    RT_NULL, RT_NULL));
    lv_obj_set_width(s_files_refresh_button, lv_pct(100));
    return page;
}

static lv_obj_t *create_about_page(lv_obj_t *parent)
{
    char text[192];
    lv_snprintf(text, sizeof(text), ft_preferences_text(
                "FeatherTalk %s\nM55 固件 %s\nIPC ABI %u\n"
                "RT-Thread + LVGL 9.2\n有限深度路由与显式应用注册表",
                "FeatherTalk %s\nM55 firmware %s\nIPC ABI %u\n"
                "RT-Thread + LVGL 9.2\nBounded router and explicit app registry"),
                FEATHERTALK_PRODUCT_VERSION, FEATHERTALK_M55_FIRMWARE_VERSION,
                (unsigned)FEATHERTALK_IPC_ABI_VERSION);
    return create_text_page(parent,
                            ft_preferences_text("设置  >  关于 FeatherTalk",
                                                "Settings  >  About FeatherTalk"),
                            FT_ICON_ABOUT, text);
}

#ifdef FEATHERTALK_UI_TEST_MODE
bool ft_pages_test_icon_assignments_unique(void)
{
    bool used[FT_ICON_COUNT] = {false};
    size_t i;
    for (i = 0U; i < sizeof(s_apps) / sizeof(s_apps[0]); i++)
    {
        ft_icon_id_t icon = s_apps[i].app.app_icon;
        ft_icon_id_t pattern = s_apps[i].tile.pattern_icon;
        if (icon >= FT_ICON_COUNT || used[icon]) return false;
        used[icon] = true;
        if (pattern < FT_ICON_COUNT)
        {
            if (used[pattern]) return false;
            used[pattern] = true;
        }
    }
    for (i = 0U; i < FT_SETTINGS_COUNT; i++)
    {
        ft_icon_id_t icon = s_settings[i].icon_id;
        if (icon >= FT_ICON_COUNT || used[icon]) return false;
        used[icon] = true;
    }
    for (i = 0U; i < FT_SYSTEM_SUMMARY_COUNT; i++)
    {
        ft_icon_id_t icon = s_system_summary_icons[i];
        if (icon >= FT_ICON_COUNT || used[icon]) return false;
        used[icon] = true;
    }
    return true;
}
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
lv_obj_t *ft_pages_test_get_time_format_button(size_t i)
{ return i < FT_TIME_FORMAT_COUNT ? s_time_format_buttons[i] : RT_NULL; }
lv_obj_t *ft_pages_test_get_timezone_dropdown(void)
{ return s_timezone_dropdown; }
lv_obj_t *ft_pages_test_get_language_button(size_t i)
{ return i < FT_LANGUAGE_COUNT ? s_language_buttons[i] : RT_NULL; }
size_t ft_pages_test_timezone_count(void) { return FT_TIMEZONE_COUNT; }
int16_t ft_pages_test_timezone_offset(size_t i)
{ return i < FT_TIMEZONE_COUNT ? s_timezones[i].offset_minutes : 0; }
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
        s_timezone_dropdown != RT_NULL || s_time_preview != RT_NULL ||
        s_media_prev_button != RT_NULL || s_media_button != RT_NULL ||
        s_media_next_button != RT_NULL || s_media_label != RT_NULL ||
        s_media_state_icon != RT_NULL || s_media_track_label != RT_NULL ||
        s_media_volume != RT_NULL || s_messages_button != RT_NULL ||
        s_messages_count_label != RT_NULL || s_files_refresh_button != RT_NULL ||
        s_files_status_label != RT_NULL) return false;
    for (i = 0U; i < FT_SYSTEM_SUMMARY_COUNT; i++)
        if (s_system_summary_values[i] != RT_NULL ||
            s_system_summary_notes[i] != RT_NULL) return false;
    for (i = 0U; i < FT_SYSTEM_SECTION_COUNT; i++)
        if (s_system_section_headers[i] != RT_NULL ||
            s_system_section_contents[i] != RT_NULL ||
            s_system_section_chevrons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_SYSTEM_FIELD_COUNT; i++)
        if (s_system_fields[i] != RT_NULL) return false;
    for (i = 0U; i < FT_ACCENT_COUNT; i++)
        if (s_accent_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_OPACITY_COUNT; i++)
        if (s_opacity_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_BACKGROUND_COUNT; i++)
        if (s_background_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_TIME_FORMAT_COUNT; i++)
        if (s_time_format_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_LANGUAGE_COUNT; i++)
        if (s_language_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < sizeof(s_search_results) / sizeof(s_search_results[0]); i++)
        if (s_search_results[i] != RT_NULL) return false;
    for (i = 0U; i < FT_SETTINGS_COUNT; i++)
        if (s_settings_results[i] != RT_NULL) return false;
    return true;
}
bool ft_pages_test_system_info_complete(void)
{
    const char *processors;
    const char *flash;
    const char *hyperram;
    const char *display_link;
    const char *devices;
    size_t i;
    bool toggled_open;
    bool toggled_closed;

    for (i = 0U; i < FT_SYSTEM_SUMMARY_COUNT; i++)
        if (!tracked_object_is_type(&s_system_summary_values[i], &lv_label_class) ||
            !tracked_object_is_type(&s_system_summary_notes[i], &lv_label_class) ||
            lv_label_get_text(s_system_summary_values[i])[0] == '\0' ||
            lv_label_get_text(s_system_summary_notes[i])[0] == '\0') return false;
    for (i = 0U; i < FT_SYSTEM_SECTION_COUNT; i++)
        if (!tracked_object_is_type(&s_system_section_headers[i], &lv_obj_class) ||
            !tracked_object_is_type(&s_system_section_contents[i], &lv_obj_class) ||
            !tracked_object_is_type(&s_system_section_chevrons[i], &lv_label_class))
            return false;
    if (lv_obj_has_flag(s_system_section_contents[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(s_system_section_contents[1], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(s_system_section_contents[2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(s_system_section_contents[3], LV_OBJ_FLAG_HIDDEN))
        return false;

    lv_obj_send_event(s_system_section_headers[1], LV_EVENT_CLICKED, RT_NULL);
    toggled_open = !lv_obj_has_flag(s_system_section_contents[1],
                                    LV_OBJ_FLAG_HIDDEN);
    lv_obj_send_event(s_system_section_headers[1], LV_EVENT_CLICKED, RT_NULL);
    toggled_closed = lv_obj_has_flag(s_system_section_contents[1],
                                     LV_OBJ_FLAG_HIDDEN);

    processors = lv_label_get_text(s_system_fields[FT_SYSTEM_FIELD_PROCESSORS]);
    flash = lv_label_get_text(s_system_fields[FT_SYSTEM_FIELD_FLASH]);
    hyperram = lv_label_get_text(s_system_fields[FT_SYSTEM_FIELD_HYPERRAM]);
    display_link = lv_label_get_text(s_system_fields[FT_SYSTEM_FIELD_DISPLAY_LINK]);
    devices = lv_label_get_text(s_system_fields[FT_SYSTEM_FIELD_DEVICES]);
    return toggled_open && toggled_closed &&
           processors != RT_NULL && strstr(processors, "Cortex-M55") != RT_NULL &&
           flash != RT_NULL && strstr(flash, "S25FS128S") != RT_NULL &&
           hyperram != RT_NULL && strstr(hyperram, "S70KS1283") != RT_NULL &&
           display_link != RT_NULL && strstr(display_link, "MIPI-DSI") != RT_NULL &&
           devices != RT_NULL &&
           (strstr(devices, "registered") != RT_NULL ||
            strstr(devices, "已注册") != RT_NULL);
}
bool ft_pages_test_language_surface(ft_language_t language)
{
    lv_obj_t *title;
    const char *expected_app = language == FT_LANGUAGE_ZH_CN ? "设置" : "Settings";
    const char *expected_category = language == FT_LANGUAGE_ZH_CN ?
                                    "显示和亮度" : "Display & brightness";
    if (s_settings_results[0] == RT_NULL || !lv_obj_is_valid(s_settings_results[0]) ||
        lv_obj_get_child_count(s_settings_results[0]) < 2U) return false;
    title = lv_obj_get_child(s_settings_results[0], 1);
    if (title == RT_NULL || !lv_obj_check_type(title, &lv_label_class)) return false;
    return strcmp(ft_tiles_test_name(0U), expected_app) == 0 &&
           strcmp(lv_label_get_text(title), expected_category) == 0;
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
bool ft_pages_test_tile_move_edge_autoscroll(size_t app_index)
{ return ft_tiles_test_move_edge_autoscroll(app_index); }
bool ft_pages_test_tile_layout_settled(void)
{ return ft_tiles_test_layout_settled(); }
bool ft_pages_test_tile_resize(size_t app_index, uint8_t columns, uint8_t rows)
{ return ft_tiles_test_resize(app_index, columns, rows); }
bool ft_pages_test_tile_resize_collision(void)
{ return ft_tiles_test_resize_collision(); }
bool ft_pages_test_tile_resize_boundary(void) { return ft_tiles_test_resize_boundary(); }
bool ft_pages_test_tile_resize_edge_autoscroll(size_t app_index)
{ return ft_tiles_test_resize_edge_autoscroll(app_index); }
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
