#include <ctype.h>
#include <rtthread.h>
#include <stdint.h>
#include <string.h>
#include <feathertalk/version.h>
#include "feathertalk_audio.h"
#include "feathertalk_player.h"
#include "feathertalk_storage.h"
#include "feathertalk_usb.h"
#include "ipc/feathertalk_ipc.h"
#include "feathertalk_ui.h"
#include "feathertalk_ui_gallery.h"
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_platform.h"
#include "feathertalk_ui_preferences_store.h"
#include "feathertalk_ui_recorder.h"

#define FT_ACCENT_COUNT      5U
#define FT_OPACITY_COUNT     3U
#define FT_SETTINGS_COUNT    10U
#define FT_TIME_FORMAT_COUNT 2U
#define FT_TIMEZONE_COUNT    7U
#define FT_SYSTEM_SUMMARY_COUNT 4U
#define FT_SYSTEM_SECTION_COUNT 4U
#define FT_FILES_PREVIEW_BYTES  384U
#define FT_STORAGE_FORMAT_IDLE    0U
#define FT_STORAGE_FORMAT_RUNNING 1U
#define FT_STORAGE_FORMAT_SUCCESS 2U
#define FT_STORAGE_FORMAT_FAILED  3U
#define FT_STORAGE_DEVICE_COUNT    2U
#define FT_AUDIO_RATE_COUNT        4U
#define FT_AUDIO_BITS_COUNT        2U
#define FT_AUDIO_CHANNEL_COUNT     2U
#define FT_MEDIA_FLOW_CELL_COUNT   5U
#define FT_MEDIA_FLOW_CENTER       (FT_MEDIA_FLOW_CELL_COUNT / 2U)
#define FT_MEDIA_FLOW_CONTROL_PERIOD_MS 10U
#define FT_MEDIA_FLOW_SETTLE_MS         20U
#define FT_MEDIA_FLOW_ANIM_MS           260U

typedef enum
{
    FT_STORAGE_DEVICE_FLASH = 0,
    FT_STORAGE_DEVICE_SD,
    FT_STORAGE_DEVICE_INVALID
} ft_storage_device_t;

typedef enum
{
    FT_MEDIA_COVER_IDLE = 0,
    FT_MEDIA_COVER_DRAGGING,
    FT_MEDIA_COVER_ANIMATING,
    FT_MEDIA_COVER_COMMIT_PENDING,
    FT_MEDIA_COVER_SETTLING
} ft_media_cover_state_t;

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
static lv_obj_t *create_files_page(lv_obj_t *parent);
static lv_obj_t *create_about_page(lv_obj_t *parent);
static lv_obj_t *create_settings_display_page(lv_obj_t *parent);
static lv_obj_t *create_settings_audio_page(lv_obj_t *parent);
static lv_obj_t *create_settings_wifi_page(lv_obj_t *parent);
static lv_obj_t *create_settings_bluetooth_page(lv_obj_t *parent);
static lv_obj_t *create_settings_storage_page(lv_obj_t *parent);
static lv_obj_t *create_settings_usb_page(lv_obj_t *parent);
static lv_obj_t *create_settings_time_language_page(lv_obj_t *parent);
static lv_obj_t *create_settings_personalization_page(lv_obj_t *parent);
static void files_page_enter(void);
static bool files_page_back(void);
static void files_page_leave(void);
static void files_refresh_view(bool manual_refresh);
static void files_format_bytes(uint64_t bytes, char *text, size_t text_size);
static void settings_time_language_refresh(void);
static void settings_choice_refresh(lv_obj_t *button, bool selected);
static void settings_audio_refresh(void);
static void settings_usb_page_enter(void);
static void settings_usb_page_leave(void);
static void settings_storage_page_enter(void);
static void settings_storage_page_leave(void);
static void language_refresh_async_cb(void *user_data);
static void media_tile_live_content(lv_obj_t *content_host,
                                    uint32_t frame, void *context);
static void update_media_labels(void);
static void media_cover_control_timer_cb(lv_timer_t *timer);
static void media_page_enter(void);
static void media_page_leave(void);
static void media_refresh_directory_label(void);

static const ft_app_descriptor_t s_apps[] =
{
    {FT_PAGE_SETTINGS,
     {"Settings", 2U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_SETTINGS, false, 0U, RT_NULL, RT_NULL}},
    {FT_PAGE_MEDIA,
     {"Media", 1U, 1U, 255U, FT_ICON_MEDIA_PATTERN},
     {FT_ICON_MEDIA, true, 1600U, media_tile_live_content, RT_NULL}},
    {FT_PAGE_RECORDER,
     {"Recorder", 1U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_RECORDER_APP, false, 0U, RT_NULL, RT_NULL}},
    {FT_PAGE_GALLERY,
     {"Gallery", 1U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_GALLERY, false, 0U, RT_NULL, RT_NULL}},
    {FT_PAGE_FILES,
     {"Files", 2U, 1U, 255U, FT_ICON_COUNT},
     {FT_ICON_FILES, false, 0U, RT_NULL, RT_NULL}},
};

static const char *s_app_names_zh[] = {"设置", "媒体", "录音机", "相册", "文件"};

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
    {FT_PAGE_SETTINGS_AUDIO, FT_ICON_AUDIO_SETTINGS, "Audio",
     "Onboard speaker, microphones and levels", "audio sound speaker microphone volume gain",
     "音频", "板载扬声器、麦克风和音量", "音频 声音 扬声器 麦克风 音量 增益"},
    {FT_PAGE_SETTINGS_WIFI, FT_ICON_WIFI_SETTINGS, "Wi-Fi",
     "Wireless network state and signal", "wifi wlan wireless network signal",
     "Wi-Fi", "无线网络状态与信号", "无线 网络 信号"},
    {FT_PAGE_SETTINGS_BLUETOOTH, FT_ICON_BLUETOOTH_SETTINGS, "Bluetooth",
     "Radio and connection state", "ble device radio wireless",
     "蓝牙", "蓝牙开关与连接状态", "蓝牙 设备 无线 连接"},
    {FT_PAGE_SETTINGS_STORAGE, FT_ICON_SD_STORAGE, "Storage",
     "Internal Flash and SD card capacity and actions", "storage flash sd card disk format capacity",
     "存储", "内置 Flash 与 SD 卡容量和操作", "存储 Flash SD卡 磁盘 格式化 容量"},
    {FT_PAGE_SETTINGS_USB, FT_ICON_USB, "USB",
     "Device role, SD storage and USB Audio", "usb device storage sd card audio uac",
     "USB", "设备角色、SD 卡存储与 USB 音频", "USB 设备 存储 SD卡 音频 UAC"},
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
static const uint32_t s_audio_rates[FT_AUDIO_RATE_COUNT] =
    {16000U, 24000U, 48000U, 96000U};
static const uint8_t s_audio_bits[FT_AUDIO_BITS_COUNT] = {16U, 24U};
static const uint8_t s_audio_channels[FT_AUDIO_CHANNEL_COUNT] = {1U, 2U};
static const char *s_background_names_en[FT_BACKGROUND_COUNT] =
    {"Black", "Dark", "Accent", "Gallery photo"};
static const char *s_background_names_zh[FT_BACKGROUND_COUNT] =
    {"纯黑", "深色", "强调色", "相册图片"};
static const char *s_tracks_en[] = {"Feather Intro", "PSoC Skyline", "Metro Pulse"};
/* Keep product, platform and protocol keywords intact across languages.
 * "Feather" is a product-family term here, not the noun "羽翼". */
static const char *s_tracks_zh[] = {"Feather 序曲", "PSoC 天际线", "都市脉冲"};

typedef struct
{
    const char *artist_en;
    const char *artist_zh;
    const char *album_en;
    const char *album_zh;
    uint32_t cover_rgb;
    uint32_t accent_rgb;
    uint32_t disc_rgb;
} ft_media_album_t;

static const ft_media_album_t s_media_albums[] =
{
    {"Feather Audio Lab", "Feather 音频实验室", "First Light", "初光",
     0x0B5CADUL, 0x43B7FFUL, 0x062744UL},
    {"PSoC Ensemble", "PSoC 合奏组", "Edge Skyline", "Edge 天际线",
     0x5A2A91UL, 0xBE7BFFUL, 0x25103DUL},
    {"Metro Signal", "都市信号", "Night Transit", "夜间轨道",
     0xB43B20UL, 0xFFB04AUL, 0x4A160EUL},
};

static const ft_page_definition_t s_pages[] =
{
    {FT_PAGE_HOME, "Start", create_home_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SEARCH, "Search", create_search_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SYSTEM, "System information", create_system_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS, "Settings", create_settings_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_MEDIA, "Media", create_media_page, media_page_enter, RT_NULL,
     media_page_leave},
    {FT_PAGE_RECORDER, "Recorder", ft_recorder_page_create,
     ft_recorder_page_enter, RT_NULL, ft_recorder_page_leave},
    {FT_PAGE_GALLERY, "Gallery", ft_gallery_create_page, ft_gallery_page_enter,
     ft_gallery_page_back, ft_gallery_page_leave},
    {FT_PAGE_FILES, "Files", create_files_page, files_page_enter,
     files_page_back, files_page_leave},
    {FT_PAGE_ABOUT, "About", create_about_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_DISPLAY, "Display & brightness", create_settings_display_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_AUDIO, "Audio", create_settings_audio_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_WIFI, "Wi-Fi", create_settings_wifi_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_BLUETOOTH, "Bluetooth", create_settings_bluetooth_page, RT_NULL, RT_NULL, RT_NULL},
    {FT_PAGE_SETTINGS_STORAGE, "Storage", create_settings_storage_page,
     settings_storage_page_enter, RT_NULL, settings_storage_page_leave},
    {FT_PAGE_SETTINGS_USB, "USB", create_settings_usb_page,
     settings_usb_page_enter, RT_NULL, settings_usb_page_leave},
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
static lv_obj_t *s_audio_output_slider;
static lv_obj_t *s_audio_output_value;
static lv_obj_t *s_audio_output_status;
static lv_obj_t *s_audio_output_details;
static lv_obj_t *s_audio_input_slider;
static lv_obj_t *s_audio_input_value;
static lv_obj_t *s_audio_input_status;
static lv_obj_t *s_audio_input_details;
static lv_obj_t *s_audio_analog_status;
static lv_obj_t *s_audio_rate_buttons[FT_AUDIO_RATE_COUNT];
static lv_obj_t *s_audio_bits_buttons[FT_AUDIO_BITS_COUNT];
static lv_obj_t *s_audio_channel_buttons[FT_AUDIO_CHANNEL_COUNT];
static lv_obj_t *s_settings_radio_status;
static lv_obj_t *s_settings_radio_button;
static lv_obj_t *s_usb_role_buttons[2];
static lv_obj_t *s_usb_function_buttons[2];
static lv_obj_t *s_usb_output_device_buttons[1];
static lv_obj_t *s_usb_input_device_buttons[2];
static lv_obj_t *s_usb_output_rate_buttons[FT_AUDIO_RATE_COUNT];
static lv_obj_t *s_usb_output_bits_buttons[FT_AUDIO_BITS_COUNT];
static lv_obj_t *s_usb_output_channel_buttons[FT_AUDIO_CHANNEL_COUNT];
static lv_obj_t *s_usb_input_rate_buttons[FT_AUDIO_RATE_COUNT];
static lv_obj_t *s_usb_input_bits_buttons[FT_AUDIO_BITS_COUNT];
static lv_obj_t *s_usb_input_channel_buttons[FT_AUDIO_CHANNEL_COUNT];
static lv_obj_t *s_usb_stop_button;
static lv_obj_t *s_usb_status_label;
static lv_timer_t *s_usb_monitor_timer;
static lv_obj_t *s_storage_device_buttons[FT_STORAGE_DEVICE_COUNT];
static lv_obj_t *s_storage_device_icons[FT_STORAGE_DEVICE_COUNT];
static lv_obj_t *s_storage_device_capacity[FT_STORAGE_DEVICE_COUNT];
static lv_obj_t *s_storage_device_state[FT_STORAGE_DEVICE_COUNT];
static lv_obj_t *s_storage_detail_icon;
static lv_obj_t *s_storage_detail_title;
static lv_obj_t *s_storage_detail_state;
static lv_obj_t *s_storage_capacity_caption;
static lv_obj_t *s_storage_capacity_total;
static lv_obj_t *s_storage_capacity_track;
static lv_obj_t *s_storage_capacity_fill;
static lv_obj_t *s_storage_used_label;
static lv_obj_t *s_storage_free_label;
static lv_obj_t *s_storage_volume_label;
static lv_obj_t *s_storage_browse_button;
static lv_obj_t *s_storage_format_button;
static lv_obj_t *s_storage_confirm_box;
static lv_obj_t *s_storage_confirm_cancel;
static lv_obj_t *s_storage_confirm_continue;
static lv_timer_t *s_storage_monitor_timer;
static volatile uint8_t s_storage_format_state;
static volatile int s_storage_format_result;
static uint8_t s_storage_confirm_stage;
static bool s_storage_result_notified;
static bool s_storage_format_from_files;
static ft_storage_device_t s_storage_selected_device = FT_STORAGE_DEVICE_FLASH;
static volatile ft_storage_device_t s_storage_format_target = FT_STORAGE_DEVICE_INVALID;
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
static lv_obj_t *s_media_artist_label;
static lv_obj_t *s_media_album_label;
static lv_obj_t *s_media_progress_label;
static lv_obj_t *s_media_progress_bar;
static lv_obj_t *s_media_volume;
static lv_obj_t *s_media_directory_label;
static lv_obj_t *s_media_loop_button;
static lv_obj_t *s_media_loop_label;
static lv_obj_t *s_media_folder_box;
static lv_obj_t *s_media_folder_list;
static char s_media_picker_path[FT_PLAYER_PATH_MAX];
static char s_media_picker_entries[16][FT_PLAYER_PATH_MAX];
static size_t s_media_picker_entry_count;
static lv_obj_t *s_media_cover_flow;
static lv_obj_t *s_media_cover_cells[FT_MEDIA_FLOW_CELL_COUNT];
static lv_obj_t *s_media_cover_cards[FT_MEDIA_FLOW_CELL_COUNT];
static lv_obj_t *s_media_cover_bands[FT_MEDIA_FLOW_CELL_COUNT];
static lv_obj_t *s_media_cover_shades[FT_MEDIA_FLOW_CELL_COUNT];
static lv_obj_t *s_media_cover_discs[FT_MEDIA_FLOW_CELL_COUNT];
static lv_obj_t *s_media_cover_dots[FT_MEDIA_FLOW_CELL_COUNT];
static lv_obj_t *s_media_cover_titles[FT_MEDIA_FLOW_CELL_COUNT];
static size_t s_media_cover_tracks[FT_MEDIA_FLOW_CELL_COUNT];
static int16_t s_media_cover_perspective[FT_MEDIA_FLOW_CELL_COUNT];
static uint8_t s_media_cover_opacity[FT_MEDIA_FLOW_CELL_COUNT];
static int32_t s_media_cover_cell_width;
static int32_t s_media_cover_max_size;
static int32_t s_media_cover_min_width;
static int32_t s_media_cover_min_height;
static bool s_media_cover_recentering;
static bool s_media_cover_visual_dirty;
static ft_media_cover_state_t s_media_cover_state;
static int32_t s_media_cover_pending_offset;
static int32_t s_media_cover_visual_offset;
static int32_t s_media_cover_anim_start_offset;
static int32_t s_media_cover_anim_target_offset;
static int32_t s_media_cover_drag_start_x;
static uint32_t s_media_cover_state_tick;
static lv_timer_t *s_media_cover_control_timer;
static lv_timer_t *s_media_monitor_timer;
static uint32_t s_media_player_generation;
static uint32_t s_media_position_second;
static bool s_media_cover_stress_active;
static uint32_t s_media_cover_stress_remaining;
static uint32_t s_media_cover_stress_completed;
static bool s_media_playing;
static int32_t s_media_track;
static lv_obj_t *s_files_refresh_button;
static lv_obj_t *s_files_status_label;
static lv_obj_t *s_files_path_label;
static lv_obj_t *s_files_up_button;
static lv_obj_t *s_files_list;
static lv_obj_t *s_files_action_box;
static lv_obj_t *s_files_action_quick;
static lv_obj_t *s_files_action_view;
static lv_obj_t *s_files_action_copy;
static lv_obj_t *s_files_action_cut;
static lv_obj_t *s_files_action_rename;
static lv_obj_t *s_files_action_new_folder;
static lv_obj_t *s_files_action_refresh;
static lv_obj_t *s_files_action_paste;
static lv_obj_t *s_files_action_delete;
static lv_obj_t *s_files_action_cancel;
static lv_obj_t *s_files_name_box;
static lv_obj_t *s_files_name_textarea;
static lv_obj_t *s_files_name_error;
static lv_obj_t *s_files_name_keyboard;
static lv_obj_t *s_files_name_cancel;
static lv_obj_t *s_files_name_confirm;
static lv_obj_t *s_files_delete_box;
static lv_obj_t *s_files_delete_cancel;
static lv_obj_t *s_files_delete_confirm;
static lv_timer_t *s_files_monitor_timer;
static char s_files_current_path[FT_STORAGE_PATH_MAX];
static char s_files_delete_path[FT_STORAGE_PATH_MAX];
static char s_files_delete_name[FT_STORAGE_NAME_MAX];
static char s_files_context_path[FT_STORAGE_PATH_MAX];
static char s_files_context_name[FT_STORAGE_NAME_MAX];
static char s_files_clipboard_path[FT_STORAGE_PATH_MAX];
static char s_files_clipboard_name[FT_STORAGE_NAME_MAX];
static char s_files_name_target[FT_STORAGE_PATH_MAX];
static lv_obj_t *s_files_suppress_click_row;
static ft_storage_device_t s_files_requested_device = FT_STORAGE_DEVICE_INVALID;
static bool s_files_delete_is_directory;
static bool s_files_context_is_directory;
static bool s_files_context_current_folder;
static bool s_files_clipboard_cut;
static bool s_files_name_is_rename;
static bool s_files_last_mounted;
static bool s_files_last_flash_mounted;
static bool s_files_last_sd_mounted;
static size_t s_files_directory_count;
static size_t s_files_file_count;
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

static size_t media_track_count(void)
{
    size_t count = ft_player_get_track_count();
    return count != 0U ? count : sizeof(s_tracks_en) / sizeof(s_tracks_en[0]);
}

static bool media_has_local_tracks(void)
{
    return ft_player_get_track_count() != 0U;
}

static void media_track_name(size_t index, char *text, size_t text_size)
{
    ft_player_track_t track;
    const char *name;
    if (text == RT_NULL || text_size == 0U) return;
    if (ft_player_get_track(index, &track) == RT_EOK)
        name = track.name;
    else
        name = track_display_name(index);
    rt_strncpy(text, name, text_size - 1U);
    text[text_size - 1U] = '\0';
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
        lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
    return label;
}

static void media_tile_live_content(lv_obj_t *content_host,
                                    uint32_t frame, void *context)
{
    size_t track_count = media_track_count();
    lv_obj_t *label = tile_live_text_label(content_host);
    char name[FT_PLAYER_NAME_MAX];
    LV_UNUSED(context);
    if (label != RT_NULL)
    {
        media_track_name(((uint32_t)s_media_track + frame) % track_count,
                         name, sizeof(name));
        lv_label_set_text(label, name);
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
    lv_obj_add_flag(tiles, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
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
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
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
    /* ft_ui_style_page() deliberately disables scrolling for ordinary pages.
     * A TileView is itself the horizontal pager, so restore that capability. */
    lv_obj_add_flag(s_home_tileview, LV_OBJ_FLAG_SCROLLABLE);
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

static int32_t label_text_height(lv_obj_t *label, int32_t max_width)
{
    lv_point_t size;
    const lv_font_t *font;
    const char *text;
    if (label == RT_NULL || !lv_obj_is_valid(label) || max_width <= 0)
        return 0;
    font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    text = lv_label_get_text(label);
    lv_text_get_size(&size, text != RT_NULL ? text : "", font,
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                     lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                     max_width, LV_TEXT_FLAG_NONE);
    return size.y;
}

static bool s_system_value_row_reflowing;

static void system_value_row_reflow(lv_obj_t *value)
{
    lv_obj_t *row;
    lv_obj_t *key;
    int32_t content_width;
    int32_t key_width;
    int32_t value_width;
    int32_t key_height;
    int32_t value_height;
    int32_t content_height;
    if (s_system_value_row_reflowing || value == RT_NULL ||
        !lv_obj_is_valid(value)) return;
    row = lv_obj_get_parent(value);
    if (row == RT_NULL || !lv_obj_is_valid(row) ||
        !lv_obj_has_flag(row, LV_OBJ_FLAG_USER_1) ||
        lv_obj_get_child_count(row) < 2U) return;

    /* LV_SIZE_CONTENT on a flex row does not reliably include the wrapped
     * height of a flex-grow child: its cross size can be evaluated before its
     * final width is known.  Measure with the actual post-layout width and
     * publish the content height explicitly.  No font size or line count is
     * assumed here. */
    s_system_value_row_reflowing = true;
    lv_obj_update_layout(row);
    key = lv_obj_get_child(row, 0U);
    content_width = lv_obj_get_content_width(row);
    key_width = lv_obj_get_width(key);
    value_width = content_width - key_width -
                  lv_obj_get_style_pad_column(row, LV_PART_MAIN);
    if (value_width < 1) value_width = 1;
    key_height = label_text_height(key, key_width);
    value_height = label_text_height(value, value_width);
    content_height = LV_MAX(key_height, value_height);
    if (content_height > 0 && lv_obj_get_content_height(row) != content_height)
        lv_obj_set_content_height(row, content_height);
    s_system_value_row_reflowing = false;
}

static void system_value_row_layout_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *row;
    lv_obj_t *value;
    if (code != LV_EVENT_SIZE_CHANGED && code != LV_EVENT_LAYOUT_CHANGED)
        return;
    row = lv_event_get_current_target(event);
    if (row == RT_NULL || lv_obj_get_child_count(row) < 2U) return;
    value = lv_obj_get_child(row, 1U);
    system_value_row_reflow(value);
}

static void system_label_set_text(lv_obj_t **slot, const char *text)
{
    if (tracked_object_is_type(slot, &lv_label_class))
    {
        const char *current = lv_label_get_text(*slot);
        if (current == RT_NULL || strcmp(current, text != RT_NULL ? text : "") != 0)
            lv_label_set_text(*slot, text != RT_NULL ? text : "");
        system_value_row_reflow(*slot);
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
    lv_obj_add_flag(row, LV_OBJ_FLAG_USER_1);
    lv_obj_add_event_cb(row, system_value_row_layout_cb, LV_EVENT_ALL, RT_NULL);
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
    ft_storage_volume_info_t sd_volume;
    ft_usb_status_t usb_status;
    char text[384];
    char note[128];
    char sd_total[24];
    char sd_free[24];
    uint32_t xip_percent;
    uint32_t heap_percent;
    uint32_t hyperram_percent;

    ft_platform_get_system_info(&info);
    ft_usb_get_status(&usb_status);
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

    if (ft_storage_get_volume(FT_STORAGE_SD_MOUNT_PATH, &sd_volume) == RT_EOK &&
        sd_volume.mounted)
    {
        files_format_bytes(sd_volume.total_bytes, sd_total, sizeof(sd_total));
        files_format_bytes(sd_volume.free_bytes, sd_free, sizeof(sd_free));
        lv_snprintf(text, sizeof(text),
                    ft_preferences_text(
                    "S25FS128S QSPI NOR，物理容量 %lu MiB；末尾 2 MiB 为 /flash FAT 用户盘\n"
                    "SDHC1 4-bit：%s，已挂载 /sdcard，可用 %s",
                    "S25FS128S QSPI NOR, %lu MiB physical; final 2 MiB is the /flash FAT user volume\n"
                    "SDHC1 4-bit: %s mounted at /sdcard, %s free"),
                    (unsigned long)(info.external_flash_bytes / (1024U * 1024U)),
                    sd_total, sd_free);
    }
    else if (usb_status.active && usb_status.function == FT_USB_FUNCTION_STORAGE)
    {
        uint64_t usb_sd_bytes = (uint64_t)usb_status.block_size *
                                usb_status.block_count;
        lv_snprintf(text, sizeof(text),
                    ft_preferences_text(
                    "S25FS128S QSPI NOR，物理容量 %lu MiB；末尾 2 MiB 作为 USB LUN 0\n"
                    "SDHC1 4-bit：%lu MiB，正由 USB Device MSC 独占",
                    "S25FS128S QSPI NOR, %lu MiB physical; final 2 MiB exported as USB LUN 0\n"
                    "SDHC1 4-bit: %lu MiB, exclusively exported by USB Device MSC"),
                    (unsigned long)(info.external_flash_bytes / (1024U * 1024U)),
                    (unsigned long)(usb_sd_bytes / (1024U * 1024U)));
    }
    else
    {
        lv_snprintf(text, sizeof(text),
                    ft_preferences_text(
                    "S25FS128S QSPI NOR，物理容量 %lu MiB；末尾 2 MiB 为 /flash FAT 用户盘\n"
                    "SDHC1 4-bit：驱动就绪，等待 SD 卡",
                    "S25FS128S QSPI NOR, %lu MiB physical; final 2 MiB is the /flash FAT user volume\n"
                    "SDHC1 4-bit: driver ready, waiting for a card"),
                    (unsigned long)(info.external_flash_bytes / (1024U * 1024U)));
    }
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
                          "Wi-Fi/蓝牙、USB Audio、USB Host、CAN-FD、I3C、PDM 和 TDM 驱动",
                          "Wi-Fi/Bluetooth, USB Audio, USB Host, CAN-FD, I3C, PDM and TDM drivers"));
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
{
    ft_background_mode_t background =
        (ft_background_mode_t)(uintptr_t)lv_event_get_user_data(event);
    if (background == FT_BACKGROUND_CUSTOM &&
        !ft_preferences_wallpaper_available())
    {
        feathertalk_ui_alert(ft_preferences_text("壁纸", "Wallpaper"),
                            ft_preferences_text("请先在相册中打开一张图片并选择“设为壁纸”。",
                                                "Open a photo in Gallery and choose Set wallpaper first."));
        (void)ft_router_push(FT_PAGE_GALLERY);
        return;
    }
    ft_preferences_set_background(background);
}

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

static void settings_audio_format_level(bool input, uint8_t value,
                                        char *text, size_t text_size)
{
    if (input)
        lv_snprintf(text, text_size, "%u.%u dB", value / 2U,
                    (value & 1U) != 0U ? 5U : 0U);
    else
        lv_snprintf(text, text_size, "%u%%", value);
}

static void settings_audio_slider_changed_cb(lv_event_t *event)
{
    bool input = (uintptr_t)lv_event_get_user_data(event) != 0U;
    lv_obj_t *slider = lv_event_get_target(event);
    lv_obj_t *label = input ? s_audio_input_value : s_audio_output_value;
    char text[24];

    if (label == RT_NULL || !lv_obj_is_valid(label)) return;
    settings_audio_format_level(input, (uint8_t)lv_slider_get_value(slider),
                                text, sizeof(text));
    lv_label_set_text(label, text);
}

static void settings_audio_format_clicked_cb(lv_event_t *event)
{
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    ft_usb_status_t usb_status;
    uintptr_t encoded = (uintptr_t)lv_event_get_user_data(event);
    uint8_t group = (uint8_t)(encoded >> 8);
    uint8_t index = (uint8_t)encoded;
    uint32_t sample_rate = preferences->audio_output_sample_rate;
    uint8_t sample_bits = preferences->audio_output_sample_bits;
    uint8_t channels = preferences->audio_output_channels;
    int result;

    if (group == 0U && index < FT_AUDIO_RATE_COUNT)
        sample_rate = s_audio_rates[index];
    else if (group == 1U && index < FT_AUDIO_BITS_COUNT)
        sample_bits = s_audio_bits[index];
    else if (group == 2U && index < FT_AUDIO_CHANNEL_COUNT)
        channels = s_audio_channels[index];
    else
        return;
    ft_usb_get_status(&usb_status);
    if (usb_status.function == FT_USB_FUNCTION_AUDIO && usb_status.active)
    {
        result = ft_usb_set_uac_output_format(sample_rate, sample_bits,
                                              channels);
        if (result == RT_EOK)
            result = ft_preferences_sync_audio_output_format(
                sample_rate, sample_bits, channels);
    }
    else
    {
        result = ft_preferences_set_audio_output_format(sample_rate,
                                                        sample_bits,
                                                        channels);
    }
    if (result != RT_EOK)
        feathertalk_ui_alert(
            ft_preferences_text("无法更改音频格式", "Unable to change audio format"),
            result == -RT_EBUSY ?
                ft_preferences_text("音频正在播放，请停止播放后重试。",
                                    "Audio is playing. Stop playback and try again.") :
                ft_preferences_text("sound0 或 ES8388 没有接受该设置。",
                                    "sound0 or ES8388 rejected this setting."));
    settings_audio_refresh();
}

static void settings_audio_refresh(void)
{
    ft_audio_status_t status;
    char text[128];
    size_t i;

    (void)ft_audio_get_status(&status);
    if (s_audio_output_slider != RT_NULL &&
        lv_obj_is_valid(s_audio_output_slider))
    {
        if (status.output_ready)
        {
            lv_obj_remove_state(s_audio_output_slider, LV_STATE_DISABLED);
            lv_slider_set_value(s_audio_output_slider, status.output_volume,
                                LV_ANIM_OFF);
        }
        else
        {
            lv_obj_add_state(s_audio_output_slider, LV_STATE_DISABLED);
        }
    }
    if (s_audio_output_status != RT_NULL &&
        lv_obj_is_valid(s_audio_output_status))
        lv_label_set_text(s_audio_output_status, status.output_ready ?
            ft_preferences_text("已注册 · 默认输出", "Registered · Default output") :
            status.output_registered ?
            ft_preferences_text("已注册 · 初始化失败", "Registered · Initialization failed") :
            ft_preferences_text("不可用 · sound0 未注册", "Unavailable · sound0 missing"));
    if (s_audio_output_details != RT_NULL &&
        lv_obj_is_valid(s_audio_output_details))
    {
        if (status.output_ready)
            lv_snprintf(text, sizeof(text),
                        "sound0 · ES8388 + MD8002 · %lu Hz · %u ch · %u bit",
                        (unsigned long)status.output_sample_rate,
                        status.output_channels, status.output_sample_bits);
        else
            lv_snprintf(text, sizeof(text), "%s",
                        ft_preferences_text("ES8388 / 功放驱动未就绪",
                                            "ES8388 / amplifier driver not ready"));
        lv_label_set_text(s_audio_output_details, text);
    }
    if (s_audio_output_value != RT_NULL &&
        lv_obj_is_valid(s_audio_output_value))
    {
        settings_audio_format_level(false, status.output_volume,
                                    text, sizeof(text));
        lv_label_set_text(s_audio_output_value, text);
    }

    if (s_audio_input_slider != RT_NULL && lv_obj_is_valid(s_audio_input_slider))
    {
        if (status.input_ready)
        {
            lv_obj_remove_state(s_audio_input_slider, LV_STATE_DISABLED);
            lv_slider_set_value(s_audio_input_slider, status.input_gain,
                                LV_ANIM_OFF);
        }
        else
        {
            lv_obj_add_state(s_audio_input_slider, LV_STATE_DISABLED);
        }
    }
    if (s_audio_input_status != RT_NULL && lv_obj_is_valid(s_audio_input_status))
        lv_label_set_text(s_audio_input_status, status.input_ready ?
            ft_preferences_text("已注册 · 默认输入", "Registered · Default input") :
            status.input_registered ?
            ft_preferences_text("已注册 · 初始化失败", "Registered · Initialization failed") :
            ft_preferences_text("不可用 · mic0 未注册", "Unavailable · mic0 missing"));
    if (s_audio_input_details != RT_NULL && lv_obj_is_valid(s_audio_input_details))
    {
        if (status.input_ready)
            lv_snprintf(text, sizeof(text),
                        "mic0 · Dual PDM · %lu Hz · %u ch · %u bit",
                        (unsigned long)status.input_sample_rate,
                        status.input_channels, status.input_sample_bits);
        else
            lv_snprintf(text, sizeof(text), "%s",
                        ft_preferences_text("PDM/PCM 驱动未就绪",
                                            "PDM/PCM driver not ready"));
        lv_label_set_text(s_audio_input_details, text);
    }
    if (s_audio_input_value != RT_NULL && lv_obj_is_valid(s_audio_input_value))
    {
        settings_audio_format_level(true, status.input_gain,
                                    text, sizeof(text));
        lv_label_set_text(s_audio_input_value, text);
    }
    if (s_audio_analog_status != RT_NULL && lv_obj_is_valid(s_audio_analog_status))
        lv_label_set_text(s_audio_analog_status,
            ft_preferences_text("硬件前端存在 · 产品驱动尚未接入",
                                "Hardware front end present · Product driver unavailable"));
    for (i = 0U; i < FT_AUDIO_RATE_COUNT; i++)
    {
        settings_choice_refresh(s_audio_rate_buttons[i],
                                status.output_sample_rate == s_audio_rates[i]);
        if (s_audio_rate_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_audio_rate_buttons[i]))
        {
            if (status.output_ready)
                lv_obj_remove_state(s_audio_rate_buttons[i], LV_STATE_DISABLED);
            else
                lv_obj_add_state(s_audio_rate_buttons[i], LV_STATE_DISABLED);
        }
    }
    for (i = 0U; i < FT_AUDIO_BITS_COUNT; i++)
    {
        settings_choice_refresh(s_audio_bits_buttons[i],
                                status.output_sample_bits == s_audio_bits[i]);
        if (s_audio_bits_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_audio_bits_buttons[i]))
        {
            if (status.output_ready)
                lv_obj_remove_state(s_audio_bits_buttons[i], LV_STATE_DISABLED);
            else
                lv_obj_add_state(s_audio_bits_buttons[i], LV_STATE_DISABLED);
        }
    }
    for (i = 0U; i < FT_AUDIO_CHANNEL_COUNT; i++)
    {
        settings_choice_refresh(s_audio_channel_buttons[i],
                                status.output_channels == s_audio_channels[i]);
        if (s_audio_channel_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_audio_channel_buttons[i]))
        {
            if (status.output_ready)
                lv_obj_remove_state(s_audio_channel_buttons[i], LV_STATE_DISABLED);
            else
                lv_obj_add_state(s_audio_channel_buttons[i], LV_STATE_DISABLED);
        }
    }
}

static void settings_audio_slider_released_cb(lv_event_t *event)
{
    bool input = (uintptr_t)lv_event_get_user_data(event) != 0U;
    uint8_t value = (uint8_t)lv_slider_get_value(lv_event_get_target(event));

    if (input)
        (void)ft_preferences_set_audio_input_gain(value);
    else
        (void)ft_preferences_set_audio_output_volume(value);
    settings_audio_refresh();
}

static lv_obj_t *settings_audio_create_device_card(
    lv_obj_t *parent, ft_icon_id_t icon_id, const char *title,
    lv_obj_t **status_slot, lv_obj_t **details_slot,
    const char *details_text, bool disabled)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *column;
    lv_obj_t *label;
    lv_obj_t *details;

    ft_ui_style_panel(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_column(card, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (disabled) lv_obj_set_style_opa(card, LV_OPA_60, LV_PART_MAIN);

    (void)ft_icon_create(card, icon_id, ft_layout_icon_size(32U), true);
    column = lv_obj_create(card);
    style_layout_container(column);
    lv_obj_set_width(column, 0);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_style_pad_row(column, ft_layout_px(3), LV_PART_MAIN);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);

    label = lv_label_create(column);
    lv_label_set_text(label, title);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, ft_layout_font(16), LV_PART_MAIN);
    track_object(status_slot, lv_label_create(column));
    lv_obj_set_width(*status_slot, lv_pct(100));
    lv_label_set_long_mode(*status_slot, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(*status_slot, ft_layout_font(13), LV_PART_MAIN);
    ft_ui_register_accent(*status_slot, FT_ACCENT_TEXT);
    details = lv_label_create(column);
    if (details_slot != RT_NULL) track_object(details_slot, details);
    lv_label_set_text(details, details_text != RT_NULL ? details_text : "");
    lv_obj_set_width(details, lv_pct(100));
    lv_label_set_long_mode(details, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(details, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(details, ft_layout_font(12), LV_PART_MAIN);
    return card;
}

static lv_obj_t *settings_audio_create_level_control(lv_obj_t *page,
                                                     bool input)
{
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_t *caption;
    lv_obj_t *slider;
    lv_obj_t **slider_slot = input ? &s_audio_input_slider :
                                     &s_audio_output_slider;
    lv_obj_t **value_slot = input ? &s_audio_input_value :
                                    &s_audio_output_value;

    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_px(28));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    caption = lv_label_create(row);
    lv_label_set_text(caption, input ?
                      ft_preferences_text("输入增益", "Input gain") :
                      ft_preferences_text("输出音量", "Output volume"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    track_object(value_slot, lv_label_create(row));
    lv_obj_set_style_text_font(*value_slot, ft_layout_font(14), LV_PART_MAIN);
    ft_ui_register_accent(*value_slot, FT_ACCENT_TEXT);

    track_object(slider_slot, lv_slider_create(page));
    slider = *slider_slot;
    lv_obj_set_size(slider, lv_pct(100), ft_layout_px(20));
    lv_slider_set_range(slider, 0, input ? 75 : 100);
    lv_obj_add_event_cb(slider, settings_audio_slider_changed_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)(input ? 1U : 0U));
    lv_obj_add_event_cb(slider, settings_audio_slider_released_cb,
                        LV_EVENT_RELEASED,
                        (void *)(uintptr_t)(input ? 1U : 0U));
    return slider;
}

static lv_obj_t *create_settings_audio_page(lv_obj_t *parent)
{
    lv_obj_t *page = create_text_page(
        parent, ft_preferences_text("音频", "Audio"), FT_ICON_AUDIO_SETTINGS,
        ft_preferences_text("管理此开发板实际连接的音频输出、输入和电平。",
                            "Manage the audio outputs, inputs and levels physically connected on this board."));
    lv_obj_t *caption;
    lv_obj_t *row;
    size_t i;

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("输出", "Output"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    (void)settings_audio_create_device_card(
        page, FT_ICON_SPEAKER_DEVICE,
        ft_preferences_text("板载扬声器", "Onboard speaker"),
        &s_audio_output_status, &s_audio_output_details, "", false);
    (void)settings_audio_create_level_control(page, false);

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("采样率", "Sample rate"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_get()->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_AUDIO_RATE_COUNT; i++)
    {
        char label[16];
        lv_snprintf(label, sizeof(label), "%lu kHz",
                    (unsigned long)(s_audio_rates[i] / 1000U));
        track_object(&s_audio_rate_buttons[i],
                     create_flat_button(row, label,
                        settings_audio_format_clicked_cb,
                        (void *)(uintptr_t)i));
        lv_obj_set_width(s_audio_rate_buttons[i], 0);
        lv_obj_set_flex_grow(s_audio_rate_buttons[i], 1);
    }

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("采样深度", "Sample depth"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_get()->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_AUDIO_BITS_COUNT; i++)
    {
        char label[16];
        lv_snprintf(label, sizeof(label), "%u bit", s_audio_bits[i]);
        track_object(&s_audio_bits_buttons[i],
                     create_flat_button(row, label,
                        settings_audio_format_clicked_cb,
                        (void *)(uintptr_t)((1U << 8) | i)));
        lv_obj_set_width(s_audio_bits_buttons[i], 0);
        lv_obj_set_flex_grow(s_audio_bits_buttons[i], 1);
    }

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("声道数", "Channels"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_get()->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    track_object(&s_audio_channel_buttons[0],
                 create_flat_button(row,
                    ft_preferences_text("单声道", "Mono"),
                    settings_audio_format_clicked_cb,
                    (void *)(uintptr_t)(2U << 8)));
    track_object(&s_audio_channel_buttons[1],
                 create_flat_button(row,
                    ft_preferences_text("双声道", "Stereo"),
                    settings_audio_format_clicked_cb,
                    (void *)(uintptr_t)((2U << 8) | 1U)));
    for (i = 0U; i < FT_AUDIO_CHANNEL_COUNT; i++)
    {
        lv_obj_set_width(s_audio_channel_buttons[i], 0);
        lv_obj_set_flex_grow(s_audio_channel_buttons[i], 1);
    }
    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text(
        "单声道数据会复制到左右 DAC；板载功放最终驱动一个扬声器。",
        "Mono data is copied to both DAC channels; the onboard amplifier drives one speaker."));
    lv_obj_set_width(caption, lv_pct(100));
    lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(caption, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xA8A8A8), LV_PART_MAIN);

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("输入", "Input"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    (void)settings_audio_create_device_card(
        page, FT_ICON_PDM_MIC_DEVICE,
        ft_preferences_text("双 PDM 麦克风阵列", "Dual PDM microphone array"),
        &s_audio_input_status, &s_audio_input_details, "", false);
    (void)settings_audio_create_level_control(page, true);
    (void)settings_audio_create_device_card(
        page, FT_ICON_ANALOG_MIC_DEVICE,
        ft_preferences_text("模拟麦克风前端", "Analog microphone front end"),
        &s_audio_analog_status, RT_NULL,
        ft_preferences_text("AMIC2 · 尚未注册为 RT-Thread Audio 设备",
                            "AMIC2 · Not registered as an RT-Thread Audio device"),
        true);
    settings_audio_refresh();
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

static const char *settings_storage_device_title(ft_storage_device_t device)
{
    return device == FT_STORAGE_DEVICE_SD ?
           ft_preferences_text("SD 卡", "SD card") :
           ft_preferences_text("内置 Flash", "Internal Flash");
}

static const char *settings_storage_device_mount(ft_storage_device_t device)
{
    return device == FT_STORAGE_DEVICE_SD ?
           FT_STORAGE_SD_MOUNT_PATH : FT_STORAGE_FLASH_MOUNT_PATH;
}

static ft_icon_id_t settings_storage_device_icon(ft_storage_device_t device)
{
    return device == FT_STORAGE_DEVICE_SD ?
           FT_ICON_SD_DEVICE : FT_ICON_FLASH_DEVICE;
}

static int settings_storage_get_info(ft_storage_device_t device,
                                     ft_storage_device_info_t *info)
{
    if (device == FT_STORAGE_DEVICE_FLASH)
        return ft_storage_get_flash_info(info);
    if (device == FT_STORAGE_DEVICE_SD)
        return ft_storage_get_device_info(info);
    if (info != RT_NULL) rt_memset(info, 0, sizeof(*info));
    return -RT_EINVAL;
}

static void settings_storage_set_device_style(size_t index, bool selected)
{
    lv_obj_t *button;
    lv_color_t highlight = ft_ui_accent_color();

    if (index >= FT_STORAGE_DEVICE_COUNT) return;
    button = s_storage_device_buttons[index];
    if (button == RT_NULL || !lv_obj_is_valid(button)) return;
    lv_obj_set_style_bg_color(button,
                              selected ? lv_color_hex(0x242424) :
                                         lv_color_hex(0x181818),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(button, selected ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button,
                                  selected ? highlight :
                                             lv_color_hex(0x444444),
                                  LV_PART_MAIN);
    if (s_storage_device_icons[index] != RT_NULL &&
        lv_obj_is_valid(s_storage_device_icons[index]))
        lv_obj_set_style_image_recolor(s_storage_device_icons[index],
                                       selected ? highlight : lv_color_white(),
                                       LV_PART_MAIN);
    if (s_storage_device_state[index] != RT_NULL &&
        lv_obj_is_valid(s_storage_device_state[index]))
        lv_obj_set_style_text_color(s_storage_device_state[index],
                                    selected ? highlight :
                                               lv_color_hex(0xA0A0A0),
                                    LV_PART_MAIN);
}

static void settings_storage_close_confirmation(void)
{
    s_storage_confirm_stage = 0U;
    if (s_storage_confirm_box != RT_NULL &&
        lv_obj_is_valid(s_storage_confirm_box))
        lv_msgbox_close(s_storage_confirm_box);
    s_storage_confirm_box = RT_NULL;
    s_storage_confirm_cancel = RT_NULL;
    s_storage_confirm_continue = RT_NULL;
}

static void settings_storage_cancel_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    settings_storage_close_confirmation();
    s_storage_format_target = FT_STORAGE_DEVICE_INVALID;
    s_storage_format_from_files = false;
}

static void settings_storage_format_worker(void *parameter)
{
    int result;
    bool flash_preferences_frozen = false;
    LV_UNUSED(parameter);
    if (s_storage_format_target == FT_STORAGE_DEVICE_FLASH)
    {
        result = ft_preferences_store_freeze();
        if (result == RT_EOK)
        {
            flash_preferences_frozen = true;
            result = ft_storage_format_flash();
        }
        if (flash_preferences_frozen)
            ft_preferences_store_thaw();
    }
    else
    {
        result = ft_storage_format_sd();
    }
    s_storage_format_result = result;
    s_storage_format_state = result == RT_EOK ?
                             FT_STORAGE_FORMAT_SUCCESS :
                             FT_STORAGE_FORMAT_FAILED;
}

static void settings_storage_refresh(void);

static void settings_storage_start_format(void)
{
    ft_storage_device_info_t info;
    rt_thread_t thread;
    ft_storage_device_t target = s_storage_format_target;

    if (s_storage_format_state == FT_STORAGE_FORMAT_RUNNING) return;
    if (target >= FT_STORAGE_DEVICE_INVALID)
        target = s_storage_selected_device;
    if (settings_storage_get_info(target, &info) != RT_EOK ||
        !info.can_format)
    {
        feathertalk_ui_alert(
            ft_preferences_text("无法格式化", "Unable to format"),
            target == FT_STORAGE_DEVICE_SD ?
            ft_preferences_text(
                "SD 卡当前不可用或正由 USB 存储占用。请先停止 USB 存储并关闭正在访问 SD 卡的文件。",
                "The SD card is unavailable or owned by USB storage. Stop USB storage and close files that are using the card.") :
            ft_preferences_text(
                "内置 Flash 当前不可用或正由 USB 存储占用。请先停止 USB 存储并关闭正在访问它的文件。",
                "Internal Flash is unavailable or owned by USB storage. Stop USB storage and close files that are using it."));
        s_storage_format_target = FT_STORAGE_DEVICE_INVALID;
        s_storage_format_from_files = false;
        return;
    }

    s_storage_format_target = target;
    s_storage_result_notified = false;
    s_storage_format_result = RT_EOK;
    s_storage_format_state = FT_STORAGE_FORMAT_RUNNING;
    thread = rt_thread_create("stg_fmt", settings_storage_format_worker,
                              RT_NULL, 4096,
                              RT_THREAD_PRIORITY_MAX - 5, 10);
    if (thread == RT_NULL)
    {
        s_storage_format_result = -RT_ENOMEM;
        s_storage_format_state = FT_STORAGE_FORMAT_FAILED;
    }
    else
    {
        rt_thread_startup(thread);
    }
    settings_storage_refresh();
}

static void settings_storage_final_confirm_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    settings_storage_close_confirmation();
    settings_storage_start_format();
}

static void settings_storage_show_confirmation(uint8_t stage);

static void settings_storage_continue_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    settings_storage_show_confirmation(2U);
}

static void settings_storage_show_confirmation(uint8_t stage)
{
    lv_obj_t *title;
    lv_obj_t *text;
    bool flash = s_storage_format_target == FT_STORAGE_DEVICE_FLASH;

    settings_storage_close_confirmation();
    s_storage_confirm_stage = stage;
    track_object(&s_storage_confirm_box, lv_msgbox_create(RT_NULL));
    lv_obj_set_width(s_storage_confirm_box, lv_pct(88));
    title = lv_msgbox_add_title(
        s_storage_confirm_box,
        stage == 1U ?
        (flash ? ft_preferences_text("格式化内置 Flash？", "Format Internal Flash?") :
                 ft_preferences_text("格式化 SD 卡？", "Format SD card?")) :
        ft_preferences_text("最后确认", "Final confirmation"));
    text = lv_msgbox_add_text(
        s_storage_confirm_box,
        stage == 1U ?
        (flash ?
         ft_preferences_text(
             "这会删除内置 Flash 用户盘中的全部文件，不会影响固件或 SD 卡。",
             "This deletes every file on the Internal Flash user volume. Firmware and the SD card are not affected.") :
         ft_preferences_text(
             "这会卸载 SD 卡，并删除整张卡上的全部分区、文件和文件夹。电脑端创建的分区也会被删除。",
             "This unmounts the SD card and deletes every partition, file and folder on the entire card, including partitions created by a computer.")) :
        (flash ?
         ft_preferences_text(
             "此操作不可撤销。确认后将内置 Flash 用户盘重建为 FAT 卷。不要断电。",
             "This cannot be undone. Internal Flash will be rebuilt as a FAT volume. Do not remove power.") :
         ft_preferences_text(
             "此操作不可撤销。确认后将整张 SD 卡重建为一个 FAT 卷；当前容量通常会使用 FAT32。不要断电或拔卡。",
             "This cannot be undone. The entire SD card will be rebuilt as one FAT volume; this card size normally uses FAT32. Do not remove power or the card.")));
    lv_obj_set_style_text_font(title, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_font(text, ft_layout_font(14), LV_PART_MAIN);
    track_object(&s_storage_confirm_cancel,
                 lv_msgbox_add_footer_button(
                     s_storage_confirm_box,
                     ft_preferences_text("取消", "Cancel")));
    track_object(&s_storage_confirm_continue,
                 lv_msgbox_add_footer_button(
                     s_storage_confirm_box,
                     stage == 1U ? ft_preferences_text("继续", "Continue") :
                                   ft_preferences_text("擦除并格式化", "Erase and format")));
    lv_obj_add_event_cb(s_storage_confirm_cancel,
                        settings_storage_cancel_clicked_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(s_storage_confirm_continue,
                        stage == 1U ? settings_storage_continue_clicked_cb :
                                      settings_storage_final_confirm_clicked_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    if (lv_obj_get_child_count(s_storage_confirm_cancel) > 0U)
        lv_obj_set_style_text_font(lv_obj_get_child(s_storage_confirm_cancel, 0U),
                                   ft_layout_font(14), LV_PART_MAIN);
    if (lv_obj_get_child_count(s_storage_confirm_continue) > 0U)
        lv_obj_set_style_text_font(lv_obj_get_child(s_storage_confirm_continue, 0U),
                                   ft_layout_font(14), LV_PART_MAIN);
    if (stage == 2U)
    {
        lv_obj_set_style_bg_color(s_storage_confirm_continue,
                                  lv_color_hex(0xE81123), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_storage_confirm_continue,
                                LV_OPA_COVER, LV_PART_MAIN);
    }
}

static void settings_storage_format_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_storage_format_from_files = false;
    s_storage_format_target = s_storage_selected_device;
    settings_storage_show_confirmation(1U);
}

static void settings_storage_device_clicked_cb(lv_event_t *event)
{
    ft_storage_device_t device =
        (ft_storage_device_t)(uintptr_t)lv_event_get_user_data(event);
    if (device >= FT_STORAGE_DEVICE_INVALID ||
        s_storage_format_state == FT_STORAGE_FORMAT_RUNNING)
        return;
    s_storage_selected_device = device;
    s_storage_format_target = FT_STORAGE_DEVICE_INVALID;
    settings_storage_refresh();
}

static void settings_storage_browse_clicked_cb(lv_event_t *event)
{
    ft_storage_device_info_t info;
    LV_UNUSED(event);
    if (settings_storage_get_info(s_storage_selected_device, &info) != RT_EOK ||
        !info.mounted || info.usb_exported)
    {
        feathertalk_ui_alert(
            ft_preferences_text("无法浏览", "Unable to browse"),
            ft_preferences_text(
                "所选设备当前未挂载，或正在由电脑通过 USB 使用。",
                "The selected device is not mounted or is currently owned by the computer over USB."));
        return;
    }
    s_files_requested_device = s_storage_selected_device;
    if (ft_router_push(FT_PAGE_FILES) != RT_EOK)
        s_files_requested_device = FT_STORAGE_DEVICE_INVALID;
}

static void settings_storage_refresh(void)
{
    ft_storage_device_info_t info[FT_STORAGE_DEVICE_COUNT];
    int result[FT_STORAGE_DEVICE_COUNT];
    ft_storage_device_info_t *selected;
    int selected_result;
    char physical_total[24];
    char volume_total[24];
    char used_text[24];
    char free_text[24];
    char volume_state[160];
    const char *state;
    const char *filesystem;
    const char *scheme;
    uint64_t capacity_total;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint32_t used_percent = 0U;
    bool usage_valid;
    bool formatting = s_storage_format_state == FT_STORAGE_FORMAT_RUNNING;
    size_t index;

    result[FT_STORAGE_DEVICE_FLASH] =
        ft_storage_get_flash_info(&info[FT_STORAGE_DEVICE_FLASH]);
    result[FT_STORAGE_DEVICE_SD] =
        ft_storage_get_device_info(&info[FT_STORAGE_DEVICE_SD]);

    for (index = 0U; index < FT_STORAGE_DEVICE_COUNT; index++)
    {
        bool valid = result[index] == RT_EOK && info[index].present;
        if (valid)
            files_format_bytes(info[index].total_bytes, physical_total,
                               sizeof(physical_total));
        else
            lv_snprintf(physical_total, sizeof(physical_total), "--");
        state = !valid ?
                (index == FT_STORAGE_DEVICE_SD ?
                 ft_preferences_text("未插卡", "Not inserted") :
                 ft_preferences_text("不可用", "Unavailable")) :
                info[index].usb_exported ?
                 ft_preferences_text("USB 使用中", "In use by USB") :
                info[index].busy ?
                 ft_preferences_text("处理中", "Working") :
                info[index].mounted ?
                 ft_preferences_text("已挂载", "Mounted") :
                 ft_preferences_text("未挂载", "Not mounted");
        if (s_storage_device_capacity[index] != RT_NULL &&
            lv_obj_is_valid(s_storage_device_capacity[index]))
            lv_label_set_text(s_storage_device_capacity[index], physical_total);
        if (s_storage_device_state[index] != RT_NULL &&
            lv_obj_is_valid(s_storage_device_state[index]))
            lv_label_set_text(s_storage_device_state[index], state);
        settings_storage_set_device_style(
            index, index == (size_t)s_storage_selected_device);
        if (s_storage_device_buttons[index] != RT_NULL &&
            lv_obj_is_valid(s_storage_device_buttons[index]))
        {
            if (formatting)
                lv_obj_add_state(s_storage_device_buttons[index],
                                 LV_STATE_DISABLED);
            else
                lv_obj_remove_state(s_storage_device_buttons[index],
                                    LV_STATE_DISABLED);
        }
    }

    selected = &info[s_storage_selected_device];
    selected_result = result[s_storage_selected_device];
    if (s_storage_detail_icon != RT_NULL &&
        lv_obj_is_valid(s_storage_detail_icon))
        ft_icon_set(s_storage_detail_icon,
                    settings_storage_device_icon(s_storage_selected_device),
                    ft_layout_icon_size(32U));
    if (s_storage_detail_title != RT_NULL &&
        lv_obj_is_valid(s_storage_detail_title))
        lv_label_set_text(s_storage_detail_title,
                          settings_storage_device_title(
                              s_storage_selected_device));

    if (selected_result != RT_EOK || !selected->present)
        state = s_storage_selected_device == FT_STORAGE_DEVICE_SD ?
                ft_preferences_text("未插卡", "Not inserted") :
                ft_preferences_text("不可用", "Unavailable");
    else if (selected->usb_exported)
        state = ft_preferences_text("电脑正在使用", "Used by computer");
    else if (selected->busy || formatting)
        state = formatting ?
                ft_preferences_text("正在格式化", "Formatting") :
                ft_preferences_text("处理中", "Working");
    else
        state = selected->mounted ?
                ft_preferences_text("已挂载", "Mounted") :
                ft_preferences_text("未挂载", "Not mounted");
    if (s_storage_detail_state != RT_NULL &&
        lv_obj_is_valid(s_storage_detail_state))
        lv_label_set_text(s_storage_detail_state, state);

    usage_valid = selected_result == RT_EOK && selected->present &&
                  selected->mounted && selected->volume_total_bytes > 0U &&
                  selected->volume_free_bytes <= selected->volume_total_bytes;
    capacity_total = usage_valid ? selected->volume_total_bytes :
                                   selected->total_bytes;
    used_bytes = usage_valid ? capacity_total - selected->volume_free_bytes : 0U;
    free_bytes = usage_valid ? selected->volume_free_bytes : 0U;
    if (usage_valid && capacity_total > 0U)
    {
        used_percent = used_bytes >= capacity_total ? 100U :
                       (uint32_t)((used_bytes * 100U) / capacity_total);
        if (used_bytes > 0U && used_percent == 0U) used_percent = 1U;
    }
    files_format_bytes(capacity_total, volume_total, sizeof(volume_total));
    if (s_storage_capacity_caption != RT_NULL &&
        lv_obj_is_valid(s_storage_capacity_caption))
        lv_label_set_text(s_storage_capacity_caption,
                          usage_valid ?
                          ft_preferences_text("当前卷", "Current volume") :
                          ft_preferences_text("设备容量", "Device capacity"));
    if (s_storage_capacity_total != RT_NULL &&
        lv_obj_is_valid(s_storage_capacity_total))
        lv_label_set_text(s_storage_capacity_total,
                          capacity_total > 0U ? volume_total : "--");
    if (s_storage_capacity_fill != RT_NULL &&
        lv_obj_is_valid(s_storage_capacity_fill))
        lv_obj_set_width(s_storage_capacity_fill, lv_pct((int32_t)used_percent));
    if (s_storage_capacity_track != RT_NULL &&
        lv_obj_is_valid(s_storage_capacity_track))
        lv_obj_set_style_opa(s_storage_capacity_track,
                             usage_valid ? LV_OPA_COVER : LV_OPA_50,
                             LV_PART_MAIN);
    if (usage_valid)
    {
        files_format_bytes(used_bytes, used_text, sizeof(used_text));
        files_format_bytes(free_bytes, free_text, sizeof(free_text));
    }
    else
    {
        lv_snprintf(used_text, sizeof(used_text), "--");
        lv_snprintf(free_text, sizeof(free_text), "--");
    }
    if (s_storage_used_label != RT_NULL &&
        lv_obj_is_valid(s_storage_used_label))
    {
        lv_snprintf(physical_total, sizeof(physical_total),
                    ft_preferences_text("已用 %s", "Used %s"), used_text);
        lv_label_set_text(s_storage_used_label, physical_total);
    }
    if (s_storage_free_label != RT_NULL &&
        lv_obj_is_valid(s_storage_free_label))
    {
        lv_snprintf(physical_total, sizeof(physical_total),
                    ft_preferences_text("可用 %s", "Free %s"), free_text);
        lv_label_set_text(s_storage_free_label, physical_total);
    }

    filesystem = selected->filesystem[0] != '\0' ?
                 (strcmp(selected->filesystem, "elm") == 0 ?
                  "FAT" : selected->filesystem) : "--";
    scheme = selected->partition_scheme == FT_STORAGE_PARTITION_GPT ? "GPT" :
             selected->partition_scheme == FT_STORAGE_PARTITION_MBR ? "MBR" :
             "--";
    if (selected_result != RT_EOK || !selected->present)
        lv_snprintf(volume_state, sizeof(volume_state), "%s",
                    s_storage_selected_device == FT_STORAGE_DEVICE_SD ?
                    ft_preferences_text("等待插入 SD 卡", "Waiting for SD card") :
                    ft_preferences_text("设备不可用", "Device unavailable"));
    else if (selected->usb_exported)
        lv_snprintf(volume_state, sizeof(volume_state),
                    ft_preferences_text("USB LUN %u · 电脑占用",
                                        "USB LUN %u · used by computer"),
                    s_storage_selected_device == FT_STORAGE_DEVICE_FLASH ? 0U : 1U);
    else if (s_storage_selected_device == FT_STORAGE_DEVICE_SD)
        lv_snprintf(volume_state, sizeof(volume_state),
                    ft_preferences_text("%s · %s · %s · %u 个分区",
                                        "%s · %s · %s · %u partitions"),
                    filesystem, settings_storage_device_mount(
                        s_storage_selected_device), scheme,
                    (unsigned int)selected->partition_count);
    else
        lv_snprintf(volume_state, sizeof(volume_state), "%s · %s",
                    filesystem, settings_storage_device_mount(
                        s_storage_selected_device));
    if (s_storage_volume_label != RT_NULL &&
        lv_obj_is_valid(s_storage_volume_label))
        lv_label_set_text(s_storage_volume_label, volume_state);

    if (s_storage_browse_button != RT_NULL &&
        lv_obj_is_valid(s_storage_browse_button))
    {
        if (selected_result == RT_EOK && selected->mounted &&
            !selected->usb_exported && !formatting)
            lv_obj_remove_state(s_storage_browse_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_storage_browse_button, LV_STATE_DISABLED);
    }
    if (s_storage_format_button != RT_NULL &&
        lv_obj_is_valid(s_storage_format_button))
    {
        lv_obj_t *label = lv_obj_get_child(s_storage_format_button, 0U);
        if (label != RT_NULL && lv_obj_check_type(label, &lv_label_class))
            lv_label_set_text(label,
                s_storage_selected_device == FT_STORAGE_DEVICE_SD ?
                ft_preferences_text("格式化 SD 卡", "Format SD card") :
                ft_preferences_text("格式化 Flash", "Format Flash"));
        if (selected_result == RT_EOK && selected->can_format && !formatting)
            lv_obj_remove_state(s_storage_format_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_storage_format_button, LV_STATE_DISABLED);
    }

    if ((s_storage_format_state == FT_STORAGE_FORMAT_SUCCESS ||
         s_storage_format_state == FT_STORAGE_FORMAT_FAILED) &&
        !s_storage_result_notified)
    {
        char message[160];
        s_storage_result_notified = true;
        if (s_storage_format_state == FT_STORAGE_FORMAT_SUCCESS)
            feathertalk_ui_alert(
                ft_preferences_text("格式化完成", "Format complete"),
                s_storage_format_target == FT_STORAGE_DEVICE_FLASH ?
                ft_preferences_text(
                    "内置 Flash 已重建为 FAT 卷并重新挂载到 /flash。",
                    "Internal Flash was rebuilt as a FAT volume and remounted at /flash.") :
                ft_preferences_text(
                    "SD 卡已重建为一个 FAT 卷并重新挂载到 /sdcard。",
                    "The SD card was rebuilt as one FAT volume and remounted at /sdcard."));
        else
        {
            lv_snprintf(message, sizeof(message),
                        s_storage_format_target == FT_STORAGE_DEVICE_FLASH ?
                        ft_preferences_text(
                            "内置 Flash 格式化未完成（错误 %d）。请关闭正在使用它的文件后重试。",
                            "Internal Flash formatting did not complete (error %d). Close files using it and try again.") :
                        ft_preferences_text(
                            "SD 卡格式化未完成（错误 %d）。请关闭正在使用它的文件后重试。",
                            "SD card formatting did not complete (error %d). Close files using it and try again."),
                        s_storage_format_result);
            feathertalk_ui_alert(ft_preferences_text("格式化失败", "Format failed"),
                                message);
        }
        s_storage_format_from_files = false;
    }
}

static void settings_storage_monitor_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    settings_storage_refresh();
}

static void settings_storage_page_enter(void)
{
    settings_storage_refresh();
    if (s_storage_monitor_timer == RT_NULL)
        s_storage_monitor_timer = lv_timer_create(
            settings_storage_monitor_cb, 1000U, RT_NULL);
}

static void settings_storage_page_leave(void)
{
    settings_storage_close_confirmation();
    if (s_storage_monitor_timer != RT_NULL)
    {
        lv_timer_delete(s_storage_monitor_timer);
        s_storage_monitor_timer = RT_NULL;
    }
}

static lv_obj_t *settings_storage_create_device_card(
    lv_obj_t *parent, ft_storage_device_t device)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    size_t index = (size_t)device;
    lv_obj_t *button;
    lv_obj_t *title;
    uint16_t icon_size = ft_layout_icon_size(32U);
    int32_t text_x = (int32_t)icon_size + ft_layout_px(20);

    track_object(&s_storage_device_buttons[index], lv_button_create(parent));
    button = s_storage_device_buttons[index];
    lv_obj_set_height(button, layout->compact ?
                      layout->list_row_height + ft_layout_px(8) :
                      ft_layout_px(104));
    lv_obj_set_style_bg_color(button, lv_color_hex(0x181818), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(button, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_add_event_cb(button, settings_storage_device_clicked_cb,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)device);

    track_object(&s_storage_device_icons[index],
                 ft_icon_create(button, settings_storage_device_icon(device),
                                icon_size, false));
    lv_obj_align(s_storage_device_icons[index], LV_ALIGN_LEFT_MID, 0, 0);

    title = lv_label_create(button);
    lv_label_set_text(title, settings_storage_device_title(device));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, lv_pct(68));
    lv_obj_set_style_text_font(title, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, text_x, 0);

    track_object(&s_storage_device_capacity[index], lv_label_create(button));
    lv_label_set_text(s_storage_device_capacity[index], "--");
    lv_obj_set_style_text_font(s_storage_device_capacity[index],
                               ft_layout_font(16), LV_PART_MAIN);
    lv_obj_align(s_storage_device_capacity[index], LV_ALIGN_LEFT_MID,
                 text_x, ft_layout_px(3));

    track_object(&s_storage_device_state[index], lv_label_create(button));
    lv_label_set_text(s_storage_device_state[index], "--");
    lv_obj_set_style_text_font(s_storage_device_state[index],
                               ft_layout_font(12), LV_PART_MAIN);
    lv_obj_align(s_storage_device_state[index], LV_ALIGN_BOTTOM_LEFT,
                 text_x, 0);
    return button;
}

static lv_obj_t *create_settings_storage_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *title;
    lv_obj_t *section;
    lv_obj_t *devices;
    lv_obj_t *detail;
    lv_obj_t *detail_header;
    lv_obj_t *legend;
    lv_obj_t *actions;
    lv_obj_t *button;

    ft_ui_style_page(page);
    lv_obj_set_style_pad_all(page, layout->page_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);

    title = lv_label_create(page);
    lv_label_set_text(title, ft_preferences_text("磁盘", "Disks"));
    lv_obj_set_style_text_font(title, ft_layout_font(22), LV_PART_MAIN);
    ft_ui_register_accent(title, FT_ACCENT_TEXT);

    section = lv_label_create(page);
    lv_label_set_text(section, ft_preferences_text("存储设备", "Storage devices"));
    lv_obj_set_style_text_font(section, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(section, lv_color_hex(0xB8B8B8), LV_PART_MAIN);

    devices = lv_obj_create(page);
    style_layout_container(devices);
    lv_obj_set_width(devices, lv_pct(100));
    lv_obj_set_height(devices, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(devices, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_style_pad_row(devices, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(devices, layout->compact ?
                        LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    button = settings_storage_create_device_card(
        devices, FT_STORAGE_DEVICE_FLASH);
    if (layout->compact)
        lv_obj_set_width(button, lv_pct(100));
    else
    {
        lv_obj_set_width(button, 0);
        lv_obj_set_flex_grow(button, 1);
    }
    button = settings_storage_create_device_card(devices, FT_STORAGE_DEVICE_SD);
    if (layout->compact)
        lv_obj_set_width(button, lv_pct(100));
    else
    {
        lv_obj_set_width(button, 0);
        lv_obj_set_flex_grow(button, 1);
    }

    detail = lv_obj_create(page);
    ft_ui_style_panel(detail);
    lv_obj_set_width(detail, lv_pct(100));
    lv_obj_set_height(detail, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(detail, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_style_pad_all(detail, ft_layout_px(14), LV_PART_MAIN);
    lv_obj_set_style_pad_row(detail, ft_layout_px(9), LV_PART_MAIN);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(detail, LV_OBJ_FLAG_SCROLLABLE);

    detail_header = lv_obj_create(detail);
    style_layout_container(detail_header);
    lv_obj_set_size(detail_header, lv_pct(100), ft_layout_px(40));
    lv_obj_set_style_pad_column(detail_header, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_set_flex_flow(detail_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(detail_header, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    track_object(&s_storage_detail_icon,
                 ft_icon_create(detail_header, FT_ICON_FLASH_DEVICE,
                                ft_layout_icon_size(32U), true));
    track_object(&s_storage_detail_title, lv_label_create(detail_header));
    lv_obj_set_width(s_storage_detail_title, 0);
    lv_obj_set_flex_grow(s_storage_detail_title, 1);
    lv_label_set_long_mode(s_storage_detail_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_storage_detail_title,
                               ft_layout_font(18), LV_PART_MAIN);
    track_object(&s_storage_detail_state, lv_label_create(detail_header));
    lv_obj_set_style_text_font(s_storage_detail_state,
                               ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_storage_detail_state,
                                lv_color_hex(0xB8B8B8), LV_PART_MAIN);

    track_object(&s_storage_capacity_caption, lv_label_create(detail));
    lv_obj_set_style_text_font(s_storage_capacity_caption,
                               ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_storage_capacity_caption,
                                lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    track_object(&s_storage_capacity_total, lv_label_create(detail));
    lv_obj_set_style_text_font(s_storage_capacity_total,
                               ft_layout_font(22), LV_PART_MAIN);

    track_object(&s_storage_capacity_track, lv_obj_create(detail));
    lv_obj_set_size(s_storage_capacity_track, lv_pct(100), ft_layout_px(20));
    lv_obj_set_style_bg_color(s_storage_capacity_track,
                              lv_color_hex(0x090909), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_storage_capacity_track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_storage_capacity_track, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_storage_capacity_track,
                                  lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_radius(s_storage_capacity_track, ft_layout_px(3), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_storage_capacity_track, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_storage_capacity_track, LV_OBJ_FLAG_SCROLLABLE);
    track_object(&s_storage_capacity_fill,
                 lv_obj_create(s_storage_capacity_track));
    lv_obj_set_size(s_storage_capacity_fill, 0, lv_pct(100));
    lv_obj_set_style_border_width(s_storage_capacity_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_storage_capacity_fill, ft_layout_px(2), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_storage_capacity_fill, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_storage_capacity_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_storage_capacity_fill, LV_ALIGN_LEFT_MID, 0, 0);
    ft_ui_register_accent(s_storage_capacity_fill, FT_ACCENT_BACKGROUND);

    legend = lv_obj_create(detail);
    style_layout_container(legend);
    lv_obj_set_size(legend, lv_pct(100), ft_layout_px(22));
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    track_object(&s_storage_used_label, lv_label_create(legend));
    track_object(&s_storage_free_label, lv_label_create(legend));
    lv_obj_set_style_text_font(s_storage_used_label,
                               ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_storage_free_label,
                               ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_storage_used_label,
                                lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_storage_free_label,
                                lv_color_hex(0xB8B8B8), LV_PART_MAIN);

    track_object(&s_storage_volume_label, lv_label_create(detail));
    lv_obj_set_width(s_storage_volume_label, lv_pct(100));
    lv_label_set_long_mode(s_storage_volume_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_storage_volume_label,
                               ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_storage_volume_label,
                                lv_color_hex(0xA0A0A0), LV_PART_MAIN);

    actions = lv_obj_create(page);
    style_layout_container(actions);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(actions, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_style_pad_row(actions, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(actions, layout->compact ?
                        LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    track_object(&s_storage_browse_button,
                 create_icon_button(actions, FT_ICON_FILES,
                                    ft_preferences_text("浏览文件", "Browse files"),
                                    settings_storage_browse_clicked_cb, RT_NULL,
                                    RT_NULL, RT_NULL));
    track_object(&s_storage_format_button,
                 create_flat_button(actions, "--",
                                    settings_storage_format_clicked_cb,
                                    RT_NULL));
    if (layout->compact)
    {
        lv_obj_set_width(s_storage_browse_button, lv_pct(100));
        lv_obj_set_width(s_storage_format_button, lv_pct(100));
    }
    else
    {
        lv_obj_set_width(s_storage_browse_button, 0);
        lv_obj_set_flex_grow(s_storage_browse_button, 1);
        lv_obj_set_width(s_storage_format_button, 0);
        lv_obj_set_flex_grow(s_storage_format_button, 1);
    }
    settings_storage_refresh();
    return page;
}

static void settings_usb_refresh(void)
{
    ft_usb_status_t status;
    ft_audio_status_t audio;
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    uint32_t output_rate;
    uint8_t output_bits;
    uint8_t output_channels;
    char text[320];
    size_t i;

    ft_usb_get_status(&status);
    (void)ft_audio_get_status(&audio);

    /* SET_CUR and SET_INTERFACE requests made by the USB host are the
     * authoritative format while UAC is active. Mirror them into the local
     * settings model without sending the same change back to USB. */
    if (status.function == FT_USB_FUNCTION_AUDIO && status.active &&
        !status.uac_format_pending &&
        ft_usb_uac_output_supported(status.uac_output_sample_rate,
                                    status.uac_output_sample_bits,
                                    status.uac_output_channels) &&
        (preferences->audio_output_sample_rate !=
             status.uac_output_sample_rate ||
         preferences->audio_output_sample_bits !=
             status.uac_output_sample_bits ||
         preferences->audio_output_channels != status.uac_output_channels))
    {
        (void)ft_preferences_sync_audio_output_format(
            status.uac_output_sample_rate,
            status.uac_output_sample_bits,
            status.uac_output_channels);
        preferences = ft_preferences_get();
    }
    output_rate = status.function == FT_USB_FUNCTION_AUDIO && status.active ?
                  status.uac_output_sample_rate :
                  preferences->audio_output_sample_rate;
    output_bits = status.function == FT_USB_FUNCTION_AUDIO && status.active ?
                  status.uac_output_sample_bits :
                  preferences->audio_output_sample_bits;
    output_channels = status.function == FT_USB_FUNCTION_AUDIO && status.active ?
                      status.uac_output_channels :
                      preferences->audio_output_channels;

    settings_choice_refresh(s_usb_role_buttons[FT_USB_ROLE_DEVICE], true);
    settings_choice_refresh(s_usb_role_buttons[FT_USB_ROLE_HOST], false);
    settings_choice_refresh(s_usb_function_buttons[0],
                            status.function == FT_USB_FUNCTION_STORAGE);
    settings_choice_refresh(s_usb_function_buttons[1],
                            status.function == FT_USB_FUNCTION_AUDIO);

    if (s_usb_role_buttons[FT_USB_ROLE_HOST] != RT_NULL &&
        lv_obj_is_valid(s_usb_role_buttons[FT_USB_ROLE_HOST]))
        lv_obj_add_state(s_usb_role_buttons[FT_USB_ROLE_HOST], LV_STATE_DISABLED);
    if (s_usb_function_buttons[0] != RT_NULL &&
        lv_obj_is_valid(s_usb_function_buttons[0]))
    {
        if (status.storage_supported && (status.sd_present || status.active))
            lv_obj_remove_state(s_usb_function_buttons[0], LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_usb_function_buttons[0], LV_STATE_DISABLED);
    }
    if (s_usb_function_buttons[1] != RT_NULL &&
        lv_obj_is_valid(s_usb_function_buttons[1]))
    {
        if (status.audio_supported)
            lv_obj_remove_state(s_usb_function_buttons[1], LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_usb_function_buttons[1], LV_STATE_DISABLED);
    }
    settings_choice_refresh(s_usb_output_device_buttons[0], true);
    settings_choice_refresh(s_usb_input_device_buttons[0], true);
    settings_choice_refresh(s_usb_input_device_buttons[1], false);
    if (s_usb_output_device_buttons[0] != RT_NULL &&
        lv_obj_is_valid(s_usb_output_device_buttons[0]))
        lv_obj_add_state(s_usb_output_device_buttons[0], LV_STATE_DISABLED);
    if (s_usb_input_device_buttons[0] != RT_NULL &&
        lv_obj_is_valid(s_usb_input_device_buttons[0]))
        lv_obj_add_state(s_usb_input_device_buttons[0], LV_STATE_DISABLED);
    if (s_usb_input_device_buttons[1] != RT_NULL &&
        lv_obj_is_valid(s_usb_input_device_buttons[1]))
        lv_obj_add_state(s_usb_input_device_buttons[1], LV_STATE_DISABLED);

    for (i = 0U; i < FT_AUDIO_RATE_COUNT; i++)
    {
        settings_choice_refresh(s_usb_output_rate_buttons[i],
                                output_rate == s_audio_rates[i]);
        if (s_usb_output_rate_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_usb_output_rate_buttons[i]))
        {
            if (status.audio_supported && audio.output_ready &&
                ft_usb_uac_output_supported(s_audio_rates[i], output_bits,
                                            output_channels))
                lv_obj_remove_state(s_usb_output_rate_buttons[i],
                                    LV_STATE_DISABLED);
            else
                lv_obj_add_state(s_usb_output_rate_buttons[i],
                                 LV_STATE_DISABLED);
        }
        settings_choice_refresh(s_usb_input_rate_buttons[i],
                                status.uac_input_sample_rate == s_audio_rates[i] ||
                                (!status.active && s_audio_rates[i] == 16000U));
        if (s_usb_input_rate_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_usb_input_rate_buttons[i]))
            lv_obj_add_state(s_usb_input_rate_buttons[i], LV_STATE_DISABLED);
    }
    for (i = 0U; i < FT_AUDIO_BITS_COUNT; i++)
    {
        settings_choice_refresh(s_usb_output_bits_buttons[i],
                                output_bits == s_audio_bits[i]);
        if (s_usb_output_bits_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_usb_output_bits_buttons[i]))
        {
            if (status.audio_supported && audio.output_ready &&
                ft_usb_uac_output_supported(output_rate, s_audio_bits[i],
                                            output_channels))
                lv_obj_remove_state(s_usb_output_bits_buttons[i],
                                    LV_STATE_DISABLED);
            else
                lv_obj_add_state(s_usb_output_bits_buttons[i],
                                 LV_STATE_DISABLED);
        }
        settings_choice_refresh(s_usb_input_bits_buttons[i],
                                s_audio_bits[i] == 16U);
        if (s_usb_input_bits_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_usb_input_bits_buttons[i]))
            lv_obj_add_state(s_usb_input_bits_buttons[i], LV_STATE_DISABLED);
    }
    for (i = 0U; i < FT_AUDIO_CHANNEL_COUNT; i++)
    {
        settings_choice_refresh(s_usb_output_channel_buttons[i],
                                output_channels == s_audio_channels[i]);
        if (s_usb_output_channel_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_usb_output_channel_buttons[i]))
        {
            if (status.audio_supported && audio.output_ready &&
                ft_usb_uac_output_supported(output_rate, output_bits,
                                            s_audio_channels[i]))
                lv_obj_remove_state(s_usb_output_channel_buttons[i],
                                    LV_STATE_DISABLED);
            else
                lv_obj_add_state(s_usb_output_channel_buttons[i],
                                 LV_STATE_DISABLED);
        }
        settings_choice_refresh(s_usb_input_channel_buttons[i],
                                s_audio_channels[i] == 2U);
        if (s_usb_input_channel_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_usb_input_channel_buttons[i]))
            lv_obj_add_state(s_usb_input_channel_buttons[i],
                             LV_STATE_DISABLED);
    }
    if (s_usb_stop_button != RT_NULL && lv_obj_is_valid(s_usb_stop_button))
    {
        if (status.active)
            lv_obj_remove_state(s_usb_stop_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_usb_stop_button, LV_STATE_DISABLED);
    }

    if (s_usb_status_label == RT_NULL || !lv_obj_is_valid(s_usb_status_label))
        return;
    if (status.active && status.function == FT_USB_FUNCTION_AUDIO)
    {
        lv_snprintf(text, sizeof(text), ft_preferences_text(
                    "USB Audio：运行中 · %s\n输出：sound0 · %lu Hz · %u bit · %u ch · %s\n"
                    "输入：mic0 · %lu Hz · %u bit · %u ch · %s\n"
                    "同步：主机 %lu 次 / 本机 %lu 次 · 错误 %d",
                    "USB Audio: active · %s\nOutput: sound0 · %lu Hz · %u bit · %u ch · %s\n"
                    "Input: mic0 · %lu Hz · %u bit · %u ch · %s\n"
                    "Sync: host %lu / device %lu · error %d"),
                    status.configured ? ft_preferences_text("已枚举", "enumerated") :
                    status.connected ? ft_preferences_text("已连接", "connected") :
                                       ft_preferences_text("等待电脑", "waiting for host"),
                    (unsigned long)status.uac_output_sample_rate,
                    status.uac_output_sample_bits,
                    status.uac_output_channels,
                    status.uac_output_streaming ?
                        ft_preferences_text("传输中", "streaming") :
                        ft_preferences_text("空闲", "idle"),
                    (unsigned long)status.uac_input_sample_rate,
                    status.uac_input_sample_bits,
                    status.uac_input_channels,
                    status.uac_input_streaming ?
                        ft_preferences_text("传输中", "streaming") :
                        ft_preferences_text("空闲", "idle"),
                    (unsigned long)status.uac_host_update_count,
                    (unsigned long)status.uac_device_update_count,
                    status.last_error);
    }
    else if (status.active)
    {
        uint64_t flash_bytes = (uint64_t)status.flash_block_size *
                               status.flash_block_count;
        uint64_t sd_bytes = (uint64_t)status.sd_block_size *
                            status.sd_block_count;
        lv_snprintf(text, sizeof(text), ft_preferences_text(
                    "存储器模式：运行中\n连接：%s\nLUN 0 内置 Flash：%lu MiB\nLUN 1 SD 卡：%lu MiB\n"
                    "两块介质已从本机卸载，由电脑独占访问。请先停止 USB 存储再拔卡或断开连接。",
                    "Storage mode: active\nConnection: %s\nLUN 0 Internal Flash: %lu MiB\nLUN 1 SD card: %lu MiB\n"
                    "Both volumes are unmounted locally and owned exclusively by the computer. Stop USB storage before removing media or disconnecting."),
                    status.configured ? ft_preferences_text("已枚举", "enumerated") :
                    status.connected ? ft_preferences_text("已连接，等待枚举", "connected, enumerating") :
                                       ft_preferences_text("等待电脑连接", "waiting for computer"),
                    (unsigned long)(flash_bytes / (1024U * 1024U)),
                    (unsigned long)(sd_bytes / (1024U * 1024U)));
    }
    else if (!status.storage_supported && !status.audio_supported)
    {
        lv_snprintf(text, sizeof(text), "%s", ft_preferences_text(
                    "USB 设备功能未编入当前固件。",
                    "USB device functions are not built into this firmware."));
    }
    else if (!status.sd_present)
    {
        lv_snprintf(text, sizeof(text), "%s", ft_preferences_text(
                    "内置 Flash 已就绪，但未检测到已挂载的 SD 卡。当前双磁盘模式需要插卡后才能启动。",
                    "Internal Flash is ready, but no mounted SD card was detected. Insert a card to start the current two-disk mode."));
    }
    else if (status.last_error != RT_EOK)
    {
        lv_snprintf(text, sizeof(text), ft_preferences_text(
                    "Flash 和 SD 卡已就绪，但上次 USB 操作失败（错误 %d）。",
                    "Flash and SD card are ready, but the last USB operation failed (error %d)."),
                    status.last_error);
    }
    else
    {
        lv_snprintf(text, sizeof(text), "%s", ft_preferences_text(
                    "USB Audio 已就绪；输出格式可由本机或电脑双向更新。存储器模式需要 SD 卡。",
                    "USB Audio is ready; its output format can be updated by either the device or host. Storage mode requires an SD card."));
    }
    lv_label_set_text(s_usb_status_label, text);
}

static void settings_usb_storage_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    (void)ft_usb_set_function(FT_USB_FUNCTION_STORAGE);
    settings_usb_refresh();
}

static void settings_usb_audio_clicked_cb(lv_event_t *event)
{
    int result;
    LV_UNUSED(event);
    result = ft_usb_set_function(FT_USB_FUNCTION_AUDIO);
    if (result != RT_EOK)
        feathertalk_ui_alert(
            ft_preferences_text("USB Audio 启动失败",
                                "USB Audio failed to start"),
            ft_preferences_text("请检查 USB 音频驱动和音频设备状态。",
                                "Check the USB audio driver and audio device state."));
    settings_usb_refresh();
}

static void settings_usb_output_format_clicked_cb(lv_event_t *event)
{
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    uintptr_t encoded = (uintptr_t)lv_event_get_user_data(event);
    uint8_t group = (uint8_t)(encoded >> 8);
    uint8_t index = (uint8_t)encoded;
    uint32_t sample_rate = preferences->audio_output_sample_rate;
    uint8_t sample_bits = preferences->audio_output_sample_bits;
    uint8_t channels = preferences->audio_output_channels;
    int result;

    if (group == 0U && index < FT_AUDIO_RATE_COUNT)
        sample_rate = s_audio_rates[index];
    else if (group == 1U && index < FT_AUDIO_BITS_COUNT)
        sample_bits = s_audio_bits[index];
    else if (group == 2U && index < FT_AUDIO_CHANNEL_COUNT)
        channels = s_audio_channels[index];
    else
        return;
    result = ft_usb_set_uac_output_format(sample_rate, sample_bits, channels);
    if (result == RT_EOK)
        result = ft_preferences_sync_audio_output_format(sample_rate,
                                                         sample_bits,
                                                         channels);
    if (result != RT_EOK)
        feathertalk_ui_alert(
            ft_preferences_text("UAC 格式更新失败",
                                "UAC format update failed"),
            ft_preferences_text(
                "当前 sound0 驱动没有接受该组合，USB 主机配置保持不变。",
                "The current sound0 driver rejected this combination; the USB host configuration was not changed."));
    settings_usb_refresh();
}

static void settings_usb_stop_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    (void)ft_usb_set_function(FT_USB_FUNCTION_NONE);
    settings_usb_refresh();
}

static void settings_usb_monitor_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    settings_usb_refresh();
}

static void settings_usb_page_enter(void)
{
    settings_usb_refresh();
    if (s_usb_monitor_timer == RT_NULL)
        s_usb_monitor_timer = lv_timer_create(settings_usb_monitor_cb, 500U, RT_NULL);
}

static void settings_usb_page_leave(void)
{
    if (s_usb_monitor_timer != RT_NULL)
    {
        lv_timer_delete(s_usb_monitor_timer);
        s_usb_monitor_timer = RT_NULL;
    }
}

static void settings_usb_create_format_controls(lv_obj_t *page, bool input)
{
    lv_obj_t **rate_buttons = input ? s_usb_input_rate_buttons :
                                      s_usb_output_rate_buttons;
    lv_obj_t **bits_buttons = input ? s_usb_input_bits_buttons :
                                      s_usb_output_bits_buttons;
    lv_obj_t **channel_buttons = input ? s_usb_input_channel_buttons :
                                         s_usb_output_channel_buttons;
    lv_event_cb_t callback = input ? RT_NULL :
                                     settings_usb_output_format_clicked_cb;
    lv_obj_t *caption;
    lv_obj_t *row;
    size_t i;

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("采样率", "Sample rate"));
    lv_obj_set_style_text_font(caption, ft_layout_font(13), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_get()->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_AUDIO_RATE_COUNT; i++)
    {
        char label[16];
        lv_snprintf(label, sizeof(label), "%lu kHz",
                    (unsigned long)(s_audio_rates[i] / 1000U));
        track_object(&rate_buttons[i],
                     create_flat_button(row, label, callback,
                                        (void *)(uintptr_t)i));
        lv_obj_set_width(rate_buttons[i], 0);
        lv_obj_set_flex_grow(rate_buttons[i], 1);
    }

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("采样深度", "Sample depth"));
    lv_obj_set_style_text_font(caption, ft_layout_font(13), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_get()->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_AUDIO_BITS_COUNT; i++)
    {
        char label[16];
        lv_snprintf(label, sizeof(label), "%u bit", s_audio_bits[i]);
        track_object(&bits_buttons[i],
                     create_flat_button(row, label, callback,
                        (void *)(uintptr_t)((1U << 8) | i)));
        lv_obj_set_width(bits_buttons[i], 0);
        lv_obj_set_flex_grow(bits_buttons[i], 1);
    }

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("声道数", "Channels"));
    lv_obj_set_style_text_font(caption, ft_layout_font(13), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_get()->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    track_object(&channel_buttons[0],
                 create_flat_button(row,
                    ft_preferences_text("单声道", "Mono"), callback,
                    (void *)(uintptr_t)(2U << 8)));
    track_object(&channel_buttons[1],
                 create_flat_button(row,
                    ft_preferences_text("双声道", "Stereo"), callback,
                    (void *)(uintptr_t)((2U << 8) | 1U)));
    for (i = 0U; i < FT_AUDIO_CHANNEL_COUNT; i++)
    {
        lv_obj_set_width(channel_buttons[i], 0);
        lv_obj_set_flex_grow(channel_buttons[i], 1);
    }
}

static lv_obj_t *create_settings_usb_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = create_text_page(
        parent, "USB", FT_ICON_USB,
        ft_preferences_text(
        "此版硬件的 Type-C 用户口固定为 USB 设备/受电端；主机模式不可用。",
        "This board revision fixes the user Type-C port as a USB device/sink; Host mode is unavailable."));
    lv_obj_t *caption;
    lv_obj_t *row;
    lv_obj_t *note;

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("USB 角色", "USB role"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    track_object(&s_usb_role_buttons[FT_USB_ROLE_DEVICE],
                 create_flat_button(row,
                                    ft_preferences_text("设备（默认）", "Device (default)"),
                                    RT_NULL, RT_NULL));
    track_object(&s_usb_role_buttons[FT_USB_ROLE_HOST],
                 create_flat_button(row,
                                    ft_preferences_text("主机", "Host"),
                                    RT_NULL, RT_NULL));
    lv_obj_set_width(s_usb_role_buttons[0], 0);
    lv_obj_set_width(s_usb_role_buttons[1], 0);
    lv_obj_set_flex_grow(s_usb_role_buttons[0], 1);
    lv_obj_set_flex_grow(s_usb_role_buttons[1], 1);

    note = lv_label_create(page);
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_label_set_text(note, ft_preferences_text(
                      "主机模式需要 Type-C Rp 和 VBUS 5V 输出硬件，本板未实现。",
                      "Host mode requires Type-C Rp and a switched 5 V VBUS source, which this board does not implement."));
    lv_obj_set_style_text_color(note, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ft_layout_font(12), LV_PART_MAIN);

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("设备功能", "Device function"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    track_object(&s_usb_function_buttons[0],
                 create_flat_button(row,
                                    ft_preferences_text("存储器", "Storage"),
                                    settings_usb_storage_clicked_cb, RT_NULL));
    track_object(&s_usb_function_buttons[1],
                 create_flat_button(row, "USB Audio (UAC2)",
                                    settings_usb_audio_clicked_cb, RT_NULL));
    lv_obj_set_width(s_usb_function_buttons[0], 0);
    lv_obj_set_width(s_usb_function_buttons[1], 0);
    lv_obj_set_flex_grow(s_usb_function_buttons[0], 1);
    lv_obj_set_flex_grow(s_usb_function_buttons[1], 1);

    note = lv_label_create(page);
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_label_set_text(note, ft_preferences_text(
                      "存储器模式共享 Flash 和 SD 卡；UAC2 同时提供扬声器输出和麦克风输入。",
                      "Storage shares Flash and SD media; UAC2 provides speaker output and microphone input simultaneously."));
    lv_obj_set_style_text_color(note, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ft_layout_font(12), LV_PART_MAIN);

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("UAC 输出设备", "UAC output device"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    track_object(&s_usb_output_device_buttons[0],
                 create_flat_button(page,
                    ft_preferences_text("板载扬声器 · sound0（默认）",
                                        "Onboard speaker · sound0 (default)"),
                    RT_NULL, RT_NULL));
    lv_obj_set_width(s_usb_output_device_buttons[0], lv_pct(100));
    settings_usb_create_format_controls(page, false);

    caption = lv_label_create(page);
    lv_label_set_text(caption, ft_preferences_text("UAC 输入设备", "UAC input device"));
    lv_obj_set_style_text_font(caption, ft_layout_font(14), LV_PART_MAIN);
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_get()->control_height);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    track_object(&s_usb_input_device_buttons[0],
                 create_flat_button(row,
                    ft_preferences_text("双 PDM 麦克风 · mic0",
                                        "Dual PDM microphones · mic0"),
                    RT_NULL, RT_NULL));
    track_object(&s_usb_input_device_buttons[1],
                 create_flat_button(row,
                    ft_preferences_text("模拟麦克风（无驱动）",
                                        "Analog microphone (no driver)"),
                    RT_NULL, RT_NULL));
    lv_obj_set_width(s_usb_input_device_buttons[0], 0);
    lv_obj_set_width(s_usb_input_device_buttons[1], 0);
    lv_obj_set_flex_grow(s_usb_input_device_buttons[0], 1);
    lv_obj_set_flex_grow(s_usb_input_device_buttons[1], 1);
    settings_usb_create_format_controls(page, true);

    note = lv_label_create(page);
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_label_set_text(note, ft_preferences_text(
        "UAC 输出可选 16/24/48/96 kHz、16/24 bit；USB Terminal 当前固定为双声道，单声道仅供本地 sound0 使用。mic0 驱动固定为 16 kHz、16 bit、双声道，因此输入格式会显示但不可修改。电脑更改 UAC 格式后，本页会自动同步。",
        "UAC output supports 16/24/48/96 kHz and 16/24 bit. Its USB Terminal is currently stereo; mono remains available only to local sound0 clients. The mic0 driver is fixed at 16 kHz, 16 bit stereo, so input controls are visible but locked. Host-side UAC changes are mirrored here automatically."));
    lv_obj_set_style_text_color(note, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(note, ft_layout_font(12), LV_PART_MAIN);

    track_object(&s_usb_stop_button,
                 create_flat_button(page,
                                    ft_preferences_text("停止 USB 功能", "Stop USB function"),
                                    settings_usb_stop_clicked_cb, RT_NULL));
    lv_obj_set_width(s_usb_stop_button, lv_pct(100));

    track_object(&s_usb_status_label, lv_label_create(page));
    lv_obj_set_width(s_usb_status_label, lv_pct(100));
    lv_label_set_long_mode(s_usb_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_usb_status_label, ft_layout_font(14), LV_PART_MAIN);
    ft_ui_register_accent(s_usb_status_label, FT_ACCENT_TEXT);
    settings_usb_refresh();
    return page;
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
                                      ft_preferences_text("壁纸、配色和标签外观会保存在本机 Flash，掉电不丢失。",
                                                          "Wallpaper, color and Tile appearance are saved in device Flash."));
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
    row = lv_obj_create(page);
    style_layout_container(row);
    lv_obj_set_size(row, lv_pct(100), ft_layout_px(32));
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    (void)ft_icon_create(row, FT_ICON_WALLPAPER,
                         ft_layout_icon_size(24U), true);
    caption = lv_label_create(row);
    lv_label_set_text(caption, ft_preferences_text("壁纸与背景", "Wallpaper & background"));
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
    ft_pages_apply_preferences();
    return page;
}

void ft_pages_apply_preferences(void)
{
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    size_t i;
    ft_tiles_apply_opacity(preferences->tile_opa);
    for (i = 0U; i < FT_ACCENT_COUNT; i++)
    {
        if (s_accent_buttons[i] != RT_NULL &&
            lv_obj_is_valid(s_accent_buttons[i]))
            lv_obj_set_style_border_width(s_accent_buttons[i],
                preferences->accent_rgb == s_accent_rgb[i] ? 4 : 1,
                LV_PART_MAIN);
    }
    for (i = 0U; i < FT_OPACITY_COUNT; i++)
        settings_choice_refresh(s_opacity_buttons[i],
                                preferences->tile_opa == s_opacity_values[i]);
    for (i = 0U; i < FT_BACKGROUND_COUNT; i++)
        settings_choice_refresh(s_background_buttons[i],
                                preferences->background == (ft_background_mode_t)i);
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
    ft_recorder_page_apply_language();
    ft_gallery_apply_language();
    update_media_labels();
}

static int32_t media_clamp_i32(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static size_t media_track_wrap(int32_t track)
{
    const int32_t count = (int32_t)media_track_count();
    while (track < 0) track += count;
    return (size_t)(track % count);
}

static void media_cover_bind_tracks(void)
{
    size_t i;
    for (i = 0U; i < FT_MEDIA_FLOW_CELL_COUNT; i++)
    {
        size_t track = media_track_wrap(s_media_track + (int32_t)i -
                                       (int32_t)FT_MEDIA_FLOW_CENTER);
        const ft_media_album_t *album =
            &s_media_albums[track % (sizeof(s_media_albums) /
                                     sizeof(s_media_albums[0]))];
        char name[FT_PLAYER_NAME_MAX];
        s_media_cover_tracks[i] = track;
        if (s_media_cover_cards[i] == RT_NULL ||
            !lv_obj_is_valid(s_media_cover_cards[i])) continue;
        lv_obj_set_style_bg_color(s_media_cover_cards[i],
                                  lv_color_hex(album->cover_rgb), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_media_cover_cards[i],
                                      lv_color_hex(album->accent_rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_media_cover_bands[i],
                                  lv_color_hex(album->accent_rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_media_cover_discs[i],
                                  lv_color_hex(album->disc_rgb), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_media_cover_discs[i],
                                      lv_color_hex(album->accent_rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_media_cover_dots[i],
                                  lv_color_hex(album->accent_rgb), LV_PART_MAIN);
        media_track_name(track, name, sizeof(name));
        lv_label_set_text(s_media_cover_titles[i], name);
    }
}

static void media_cover_refresh_visuals(void)
{
    int32_t flow_width;
    int32_t range;
    size_t i;
    if (s_media_cover_flow == RT_NULL || !lv_obj_is_valid(s_media_cover_flow) ||
        s_media_cover_cell_width <= 0 || s_media_cover_max_size <= 0) return;
    flow_width = lv_obj_get_content_width(s_media_cover_flow);
    range = s_media_cover_cell_width + s_media_cover_cell_width / 2;
    for (i = 0U; i < FT_MEDIA_FLOW_CELL_COUNT; i++)
    {
        int32_t signed_distance;
        int32_t distance;
        int32_t depth;
        int32_t width;
        int32_t height;
        int32_t opacity;
        int32_t y_offset;
        int32_t old_depth;
        int32_t old_perspective;
        if (s_media_cover_cells[i] == RT_NULL || s_media_cover_cards[i] == RT_NULL ||
            !lv_obj_is_valid(s_media_cover_cells[i]) ||
            !lv_obj_is_valid(s_media_cover_cards[i])) continue;
        signed_distance = ((int32_t)i - (int32_t)FT_MEDIA_FLOW_CENTER) *
                          s_media_cover_cell_width + s_media_cover_visual_offset;
        if (lv_obj_get_x(s_media_cover_cells[i]) !=
            (flow_width - s_media_cover_cell_width) / 2 + signed_distance)
            lv_obj_set_x(s_media_cover_cells[i],
                         (flow_width - s_media_cover_cell_width) / 2 +
                         signed_distance);
        distance = signed_distance < 0 ? -signed_distance : signed_distance;
        if (distance > range) distance = range;
        depth = distance * 256 / range;
        width = s_media_cover_max_size -
                (s_media_cover_max_size - s_media_cover_min_width) * depth / 256;
        height = s_media_cover_max_size -
                 (s_media_cover_max_size - s_media_cover_min_height) * depth / 256;
        opacity = LV_OPA_COVER - (LV_OPA_COVER - LV_OPA_50) * distance / range;
        width = media_clamp_i32(width, s_media_cover_min_width,
                                s_media_cover_max_size);
        height = media_clamp_i32(height, s_media_cover_min_height,
                                 s_media_cover_max_size);
        y_offset = (s_media_cover_max_size - height) / 4;
        old_perspective = s_media_cover_perspective[i];
        old_depth = old_perspective < 0 ? -old_perspective : old_perspective;
        if (lv_obj_get_width(s_media_cover_cards[i]) != width ||
            lv_obj_get_height(s_media_cover_cards[i]) != height)
        {
            lv_obj_set_size(s_media_cover_cards[i], width, height);
            lv_obj_align(s_media_cover_cards[i], LV_ALIGN_CENTER, 0, y_offset);
        }
        /* Child alignment is defined once when the card is created.  Rewriting
         * text and artwork coordinates for every scroll sample needlessly
         * dirties the font layout cache and was the source of visible glyph
         * reordering while the cover animation was active. */
        /* Never fade the parent as a group.  A transformed/faded parent makes
         * LVGL allocate one off-screen layer per album and breaks the
         * one-submit frame chain.  Fade each primitive directly instead. */
        if (s_media_cover_opacity[i] != (uint8_t)opacity)
        {
            lv_obj_set_style_bg_opa(s_media_cover_cards[i], (lv_opa_t)opacity, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_media_cover_bands[i], (lv_opa_t)opacity, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_media_cover_discs[i], (lv_opa_t)opacity, LV_PART_MAIN);
            lv_obj_set_style_border_opa(s_media_cover_discs[i], (lv_opa_t)opacity, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_media_cover_dots[i], (lv_opa_t)opacity, LV_PART_MAIN);
        }
        if (old_depth != depth)
            lv_obj_set_style_bg_opa(s_media_cover_shades[i],
                                    (lv_opa_t)(depth * 3 / 5), LV_PART_MAIN);
        if ((old_perspective < 0) != (signed_distance < 0))
            lv_obj_align(s_media_cover_shades[i],
                         signed_distance < 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID,
                         0, 0);
        /* Track text lives in the fixed details panel.  Keeping labels inside
         * a continuously resized cover would rebuild glyph geometry in the
         * moving object tree on every animation frame. */
        if (!lv_obj_has_flag(s_media_cover_titles[i], LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(s_media_cover_titles[i], LV_OBJ_FLAG_HIDDEN);
        s_media_cover_perspective[i] =
            (int16_t)(signed_distance < 0 ? -depth : depth);
        s_media_cover_opacity[i] = (uint8_t)opacity;
    }
}

static void update_media_labels(void)
{
    size_t track = media_track_wrap(s_media_track);
    const ft_media_album_t *album =
        &s_media_albums[track % (sizeof(s_media_albums) /
                                 sizeof(s_media_albums[0]))];
    ft_player_track_t local_track;
    bool local = ft_player_get_track(track, &local_track) == RT_EOK;
    char name[FT_PLAYER_NAME_MAX];
    char details[96];
    ft_player_status_t status;
    (void)ft_player_get_status(&status);
    if (s_media_label != RT_NULL && lv_obj_is_valid(s_media_label))
        lv_label_set_text(s_media_label,
                          s_media_playing ? ft_preferences_text("暂停", "Pause") :
                                            ft_preferences_text("播放", "Play"));
    if (s_media_state_icon != RT_NULL && lv_obj_is_valid(s_media_state_icon))
        ft_icon_set(s_media_state_icon, s_media_playing ? FT_ICON_PAUSE : FT_ICON_PLAY,
                    ft_layout_icon_size(24U));
    if (s_media_track_label != RT_NULL && lv_obj_is_valid(s_media_track_label))
    {
        media_track_name(track, name, sizeof(name));
        lv_label_set_text(s_media_track_label, name);
    }
    if (s_media_artist_label != RT_NULL && lv_obj_is_valid(s_media_artist_label))
        lv_label_set_text(s_media_artist_label, local ?
            (local_track.recording ?
                ft_preferences_text("本地录音", "Local recording") :
                ft_preferences_text("本地音乐", "Local music")) :
            ft_preferences_text(album->artist_zh, album->artist_en));
    if (s_media_album_label != RT_NULL && lv_obj_is_valid(s_media_album_label))
    {
        if (local)
        {
            lv_snprintf(details, sizeof(details), "%lu Hz · %u bit · %u ch",
                        (unsigned long)local_track.sample_rate,
                        local_track.sample_bits, local_track.channels);
            lv_label_set_text(s_media_album_label, details);
        }
        else
            lv_label_set_text(s_media_album_label,
                              ft_preferences_text(album->album_zh,
                                                  album->album_en));
    }
    if (s_media_loop_label != RT_NULL && lv_obj_is_valid(s_media_loop_label))
        lv_label_set_text(s_media_loop_label, status.folder_loop ?
            ft_preferences_text("循环开", "Loop on") :
            ft_preferences_text("循环关", "Loop off"));
    media_refresh_directory_label();
    media_cover_bind_tracks();
}

static void media_cover_recenter(void)
{
    if (s_media_cover_flow == RT_NULL || !lv_obj_is_valid(s_media_cover_flow) ||
        s_media_cover_cells[FT_MEDIA_FLOW_CENTER] == RT_NULL ||
        !lv_obj_is_valid(s_media_cover_cells[FT_MEDIA_FLOW_CENTER])) return;
    s_media_cover_recentering = true;
    s_media_cover_visual_offset = 0;
    media_cover_refresh_visuals();
    s_media_cover_visual_dirty = false;
    s_media_cover_recentering = false;
}

static void media_cover_commit_offset(int32_t offset)
{
    ft_player_status_t player;
    bool restart;
    if (offset == 0) return;
    (void)ft_player_get_status(&player);
    restart = media_has_local_tracks() &&
              (player.state == FT_PLAYER_PLAYING ||
               player.state == FT_PLAYER_STARTING);
    s_media_track = (int32_t)media_track_wrap(s_media_track + offset);
    if (restart) (void)ft_player_play((size_t)s_media_track);
    update_media_labels();
}

static void media_cover_wake_controller(void)
{
    if (s_media_cover_control_timer == RT_NULL) return;
    lv_timer_resume(s_media_cover_control_timer);
}

static bool media_cover_start_turn(int32_t offset)
{
    if (offset == 0 || s_media_cover_state != FT_MEDIA_COVER_IDLE ||
        s_media_cover_flow == RT_NULL || !lv_obj_is_valid(s_media_cover_flow) ||
        offset < -(int32_t)FT_MEDIA_FLOW_CENTER ||
        offset > (int32_t)FT_MEDIA_FLOW_CENTER) return false;

    s_media_cover_pending_offset = offset;
    s_media_cover_anim_start_offset = s_media_cover_visual_offset;
    s_media_cover_anim_target_offset = -offset * s_media_cover_cell_width;
    s_media_cover_state = FT_MEDIA_COVER_ANIMATING;
    s_media_cover_state_tick = lv_tick_get();
    media_cover_wake_controller();
    return true;
}

static void media_cover_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED && s_media_cover_state == FT_MEDIA_COVER_IDLE)
    {
        lv_indev_t *indev = lv_indev_active();
        lv_point_t point;
        if (indev == RT_NULL) return;
        lv_indev_get_point(indev, &point);
        s_media_cover_drag_start_x = point.x;
        s_media_cover_state = FT_MEDIA_COVER_DRAGGING;
        s_media_cover_state_tick = lv_tick_get();
        media_cover_wake_controller();
    }
    else if (code == LV_EVENT_PRESSING &&
             s_media_cover_state == FT_MEDIA_COVER_DRAGGING)
    {
        lv_indev_t *indev = lv_indev_active();
        lv_point_t point;
        if (indev == RT_NULL) return;
        lv_indev_get_point(indev, &point);
        s_media_cover_visual_offset = media_clamp_i32(
            point.x - s_media_cover_drag_start_x,
            -s_media_cover_cell_width, s_media_cover_cell_width);
        s_media_cover_visual_dirty = true;
    }
    else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
             s_media_cover_state == FT_MEDIA_COVER_DRAGGING)
    {
        int32_t distance = s_media_cover_visual_offset < 0 ?
                           -s_media_cover_visual_offset :
                           s_media_cover_visual_offset;
        if (distance == 0)
        {
            s_media_cover_state = FT_MEDIA_COVER_IDLE;
            return;
        }
        s_media_cover_pending_offset =
            distance >= s_media_cover_cell_width / 4 ?
            (s_media_cover_visual_offset < 0 ? 1 : -1) : 0;
        s_media_cover_anim_start_offset = s_media_cover_visual_offset;
        s_media_cover_anim_target_offset =
            -s_media_cover_pending_offset * s_media_cover_cell_width;
        s_media_cover_state = FT_MEDIA_COVER_ANIMATING;
        s_media_cover_state_tick = lv_tick_get();
        media_cover_wake_controller();
    }
    else if (code == LV_EVENT_SHORT_CLICKED)
    {
        lv_indev_t *indev = lv_indev_active();
        lv_point_t point;
        int32_t best = (int32_t)FT_MEDIA_FLOW_CENTER;
        int32_t best_distance = INT32_MAX;
        size_t i;
        if (indev == RT_NULL || s_media_cover_state != FT_MEDIA_COVER_IDLE) return;
        lv_indev_get_point(indev, &point);
        for (i = 0U; i < FT_MEDIA_FLOW_CELL_COUNT; i++)
        {
            lv_area_t area;
            int32_t distance;
            if (s_media_cover_cells[i] == RT_NULL ||
                !lv_obj_is_valid(s_media_cover_cells[i])) continue;
            lv_obj_get_coords(s_media_cover_cells[i], &area);
            distance = (area.x1 + area.x2) / 2 - point.x;
            if (distance < 0) distance = -distance;
            if (distance < best_distance)
            {
                best_distance = distance;
                best = (int32_t)i;
            }
        }
        if (best != (int32_t)FT_MEDIA_FLOW_CENTER)
            (void)media_cover_start_turn(best - (int32_t)FT_MEDIA_FLOW_CENTER);
    }
    else if (code == LV_EVENT_SIZE_CHANGED && !s_media_cover_recentering)
    {
        s_media_cover_visual_dirty = true;
        media_cover_wake_controller();
    }
}

static lv_obj_t *media_cover_create_cell(lv_obj_t *flow, size_t index,
                                         int32_t flow_height)
{
    lv_obj_t *cell = lv_obj_create(flow);
    lv_obj_t *cover = lv_obj_create(cell);
    lv_obj_t *band = lv_obj_create(cover);
    lv_obj_t *shade = lv_obj_create(cover);
    lv_obj_t *disc = lv_obj_create(cover);
    lv_obj_t *dot = lv_obj_create(disc);
    lv_obj_t *title = lv_label_create(cover);
    style_layout_container(cell);
    lv_obj_set_size(cell, s_media_cover_cell_width, flow_height);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(cover, s_media_cover_min_width, s_media_cover_min_height);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cover, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(cover, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cover, 0, LV_PART_MAIN);
    lv_obj_remove_flag(cover, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(band, s_media_cover_max_size * 86 / 100,
                    media_clamp_i32(s_media_cover_max_size / 20, 3, 12));
    lv_obj_align(band, LV_ALIGN_TOP_MID, 0, ft_layout_px(8));
    lv_obj_set_style_border_width(band, 0, LV_PART_MAIN);
    /* These decorations are resized continuously while Cover Flow moves.
     * Keep them on the proven rectangle fast path: runtime rounded geometry
     * allocates/rebuilds temporary VG-Lite paths and eventually stalls after
     * repeated animated page turns on PSE84. */
    lv_obj_set_style_radius(band, 0, LV_PART_MAIN);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(shade, s_media_cover_max_size * 12 / 100,
                    s_media_cover_max_size * 88 / 100);
    lv_obj_align(shade, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(shade, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(shade, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(shade, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(shade, 0, LV_PART_MAIN);
    lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(disc, s_media_cover_max_size * 48 / 100,
                    s_media_cover_max_size * 48 / 100);
    lv_obj_align(disc, LV_ALIGN_CENTER, 0, -ft_layout_px(8));
    lv_obj_set_style_radius(disc, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(disc, ft_layout_px(3), LV_PART_MAIN);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, s_media_cover_max_size * 12 / 100,
                    s_media_cover_max_size * 12 / 100);
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_width(title, lv_pct(84));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -ft_layout_px(10));
    lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);

    s_media_cover_cells[index] = cell;
    s_media_cover_cards[index] = cover;
    s_media_cover_bands[index] = band;
    s_media_cover_shades[index] = shade;
    s_media_cover_discs[index] = disc;
    s_media_cover_dots[index] = dot;
    s_media_cover_titles[index] = title;
    return cell;
}

static void media_refresh_directory_label(void)
{
    char path[FT_PLAYER_PATH_MAX];
    char text[FT_PLAYER_PATH_MAX + 32U];
    if (s_media_directory_label == RT_NULL ||
        !lv_obj_is_valid(s_media_directory_label) ||
        ft_player_get_directory(path, sizeof(path)) != RT_EOK)
        return;
    lv_snprintf(text, sizeof(text), "%s: %s",
                ft_preferences_text("播放文件夹", "Playback folder"), path);
    lv_label_set_text(s_media_directory_label, text);
}

static void media_folder_close(void)
{
    if (s_media_folder_box != RT_NULL && lv_obj_is_valid(s_media_folder_box))
        lv_msgbox_close(s_media_folder_box);
    s_media_folder_box = RT_NULL;
    s_media_folder_list = RT_NULL;
    s_media_picker_entry_count = 0U;
}

static void media_folder_rebuild(void);

static void media_folder_entry_cb(lv_event_t *event)
{
    const char *path = (const char *)lv_event_get_user_data(event);
    if (path == RT_NULL) return;
    rt_strncpy(s_media_picker_path, path, sizeof(s_media_picker_path) - 1U);
    s_media_picker_path[sizeof(s_media_picker_path) - 1U] = '\0';
    media_folder_rebuild();
}

static void media_folder_root_cb(lv_event_t *event)
{
    const char *path = (const char *)lv_event_get_user_data(event);
    if (path == RT_NULL) return;
    rt_strncpy(s_media_picker_path, path, sizeof(s_media_picker_path) - 1U);
    s_media_picker_path[sizeof(s_media_picker_path) - 1U] = '\0';
    media_folder_rebuild();
}

static void media_folder_up_cb(lv_event_t *event)
{
    const char *mount;
    LV_UNUSED(event);
    mount = strncmp(s_media_picker_path, FT_STORAGE_SD_MOUNT_PATH,
                    strlen(FT_STORAGE_SD_MOUNT_PATH)) == 0 ?
            FT_STORAGE_SD_MOUNT_PATH : FT_STORAGE_FLASH_MOUNT_PATH;
    (void)ft_storage_parent_path(s_media_picker_path, mount);
    media_folder_rebuild();
}

static bool media_folder_list_cb(const ft_storage_entry_t *entry,
                                 void *context)
{
    lv_obj_t *button;
    lv_obj_t *label;
    char caption[FT_STORAGE_NAME_MAX + 8U];
    LV_UNUSED(context);
    if (entry == RT_NULL || entry->type != FT_STORAGE_ENTRY_DIRECTORY ||
        s_media_picker_entry_count >=
            sizeof(s_media_picker_entries) / sizeof(s_media_picker_entries[0]))
        return true;
    if (ft_storage_join_path(s_media_picker_path, entry->name,
            s_media_picker_entries[s_media_picker_entry_count],
            sizeof(s_media_picker_entries[0])) != RT_EOK)
        return true;
    button = lv_button_create(s_media_folder_list);
    lv_obj_set_size(button, lv_pct(100), ft_layout_px(44));
    lv_obj_set_style_radius(button, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x242424), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    label = lv_label_create(button);
    lv_snprintf(caption, sizeof(caption), "%s  >", entry->name);
    lv_label_set_text(label, caption);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, media_folder_entry_cb, LV_EVENT_CLICKED,
                        s_media_picker_entries[s_media_picker_entry_count]);
    s_media_picker_entry_count++;
    return true;
}

static void media_folder_select_cb(lv_event_t *event)
{
    int result;
    LV_UNUSED(event);
    result = ft_player_set_directory(s_media_picker_path);
    if (result != RT_EOK)
    {
        media_folder_close();
        feathertalk_ui_alert(
            ft_preferences_text("无法切换播放文件夹", "Unable to switch playback folder"),
            ft_preferences_text("文件夹当前不可读，或存储介质已交给 USB 主机。",
                                "The folder is unavailable or exported to the USB host."));
        return;
    }
    s_media_track = 0;
    s_media_player_generation = UINT32_MAX;
    media_folder_close();
    media_refresh_directory_label();
    update_media_labels();
}

static void media_folder_cancel_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    media_folder_close();
}

static lv_obj_t *media_folder_nav_button(lv_obj_t *parent, const char *text,
                                         lv_event_cb_t callback,
                                         const char *path)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_height(button, ft_layout_px(38));
    lv_obj_set_width(button, 0);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, ft_layout_px(4), LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, (void *)path);
    return button;
}

static void media_folder_rebuild(void)
{
    lv_obj_t *content;
    lv_obj_t *path_label;
    lv_obj_t *hint;
    lv_obj_t *nav;
    lv_obj_t *title;
    lv_obj_t *button;
    int listed;
    media_folder_close();
    s_media_folder_box = lv_msgbox_create(RT_NULL);
    lv_obj_set_width(s_media_folder_box, lv_pct(90));
    lv_obj_set_height(s_media_folder_box, lv_pct(78));
    lv_obj_set_style_text_font(s_media_folder_box, ft_layout_font(14),
                               LV_PART_MAIN);
    title = lv_msgbox_add_title(s_media_folder_box,
        ft_preferences_text("音乐文件夹", "Music folder"));
    lv_obj_set_style_text_font(title, ft_layout_font(18), LV_PART_MAIN);
    content = lv_msgbox_get_content(s_media_folder_box);
    lv_obj_set_style_text_font(content, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, ft_layout_px(6), LV_PART_MAIN);
    hint = lv_label_create(content);
    lv_label_set_text(hint, ft_preferences_text(
        "选择后立即停止当前曲目，并把该文件夹切换为播放列表。",
        "Selecting immediately stops the current track and uses this folder as the playlist."));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    path_label = lv_label_create(content);
    lv_label_set_text(path_label, s_media_picker_path);
    lv_label_set_long_mode(path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(path_label, lv_pct(100));
    lv_obj_set_style_text_font(path_label, ft_layout_font(12), LV_PART_MAIN);
    ft_ui_register_accent(path_label, FT_ACCENT_TEXT);

    nav = lv_obj_create(content);
    style_layout_container(nav);
    lv_obj_set_size(nav, lv_pct(100), ft_layout_px(38));
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(nav, ft_layout_px(4), LV_PART_MAIN);
    button = media_folder_nav_button(nav,
        ft_preferences_text("SD 卡", "SD card"), media_folder_root_cb,
        FT_STORAGE_SD_MOUNT_PATH);
    LV_UNUSED(button);
    (void)media_folder_nav_button(nav,
        ft_preferences_text("内置 Flash", "Internal Flash"),
        media_folder_root_cb, FT_STORAGE_FLASH_MOUNT_PATH);
    (void)media_folder_nav_button(nav,
        ft_preferences_text("上一级", "Up"), media_folder_up_cb, RT_NULL);

    s_media_folder_list = lv_obj_create(content);
    lv_obj_set_width(s_media_folder_list, lv_pct(100));
    lv_obj_set_height(s_media_folder_list, 0);
    lv_obj_set_flex_grow(s_media_folder_list, 1);
    lv_obj_set_style_bg_opa(s_media_folder_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_media_folder_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_media_folder_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_media_folder_list, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_media_folder_list, LV_FLEX_FLOW_COLUMN);
    s_media_picker_entry_count = 0U;
    listed = ft_storage_list(s_media_picker_path, FT_STORAGE_ENTRY_DIRECTORY,
                             media_folder_list_cb, RT_NULL);
    if (listed == 0)
    {
        lv_obj_t *empty = lv_label_create(s_media_folder_list);
        lv_label_set_text(empty, ft_preferences_text(
            "没有子文件夹；仍可切换到此文件夹。",
            "No subfolders; you can still switch to this folder."));
        lv_obj_set_style_text_font(empty, ft_layout_font(12), LV_PART_MAIN);
    }
    button = lv_msgbox_add_footer_button(s_media_folder_box,
        ft_preferences_text("切换到此文件夹", "Switch to this folder"));
    lv_obj_set_style_text_font(button, ft_layout_font(14), LV_PART_MAIN);
    if (lv_obj_get_child_count(button) > 0U)
        lv_obj_set_style_text_font(lv_obj_get_child(button, 0U),
                                   ft_layout_font(14), LV_PART_MAIN);
    lv_obj_add_event_cb(button, media_folder_select_cb, LV_EVENT_CLICKED, RT_NULL);
    button = lv_msgbox_add_footer_button(s_media_folder_box,
        ft_preferences_text("取消", "Cancel"));
    lv_obj_set_style_text_font(button, ft_layout_font(14), LV_PART_MAIN);
    if (lv_obj_get_child_count(button) > 0U)
        lv_obj_set_style_text_font(lv_obj_get_child(button, 0U),
                                   ft_layout_font(14), LV_PART_MAIN);
    lv_obj_add_event_cb(button, media_folder_cancel_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_update_layout(s_media_folder_box);
}

static void media_folder_open_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if (ft_player_get_directory(s_media_picker_path,
                                sizeof(s_media_picker_path)) != RT_EOK)
        rt_strncpy(s_media_picker_path, FT_STORAGE_SD_MOUNT_PATH,
                   sizeof(s_media_picker_path) - 1U);
    s_media_picker_path[sizeof(s_media_picker_path) - 1U] = '\0';
    media_folder_rebuild();
}

static void media_loop_cb(lv_event_t *event)
{
    ft_player_status_t status;
    LV_UNUSED(event);
    if (ft_player_get_status(&status) != RT_EOK) return;
    (void)ft_player_set_folder_loop(!status.folder_loop);
    if (s_media_loop_label != RT_NULL && lv_obj_is_valid(s_media_loop_label))
        lv_label_set_text(s_media_loop_label, !status.folder_loop ?
            ft_preferences_text("循环开", "Loop on") :
            ft_preferences_text("循环关", "Loop off"));
}

static void media_page_deleted_cb(lv_event_t *event)
{
    size_t i;
    LV_UNUSED(event);
    if (s_media_cover_control_timer != RT_NULL)
    {
        lv_timer_delete(s_media_cover_control_timer);
        s_media_cover_control_timer = RT_NULL;
    }
    if (s_media_monitor_timer != RT_NULL)
    {
        lv_timer_delete(s_media_monitor_timer);
        s_media_monitor_timer = RT_NULL;
    }
    s_media_cover_stress_active = false;
    s_media_cover_stress_remaining = 0U;
    s_media_cover_stress_completed = 0U;
    s_media_cover_flow = RT_NULL;
    s_media_cover_recentering = false;
    s_media_cover_visual_dirty = false;
    s_media_cover_state = FT_MEDIA_COVER_IDLE;
    s_media_cover_pending_offset = 0;
    s_media_cover_visual_offset = 0;
    s_media_cover_anim_start_offset = 0;
    s_media_cover_anim_target_offset = 0;
    s_media_cover_drag_start_x = 0;
    s_media_cover_state_tick = 0U;
    s_media_cover_cell_width = 0;
    s_media_cover_max_size = 0;
    s_media_cover_min_width = 0;
    s_media_cover_min_height = 0;
    s_media_player_generation = 0U;
    s_media_position_second = UINT32_MAX;
    media_folder_close();
    s_media_directory_label = RT_NULL;
    s_media_loop_button = RT_NULL;
    s_media_loop_label = RT_NULL;
    for (i = 0U; i < FT_MEDIA_FLOW_CELL_COUNT; i++)
    {
        s_media_cover_cells[i] = RT_NULL;
        s_media_cover_cards[i] = RT_NULL;
        s_media_cover_bands[i] = RT_NULL;
        s_media_cover_shades[i] = RT_NULL;
        s_media_cover_discs[i] = RT_NULL;
        s_media_cover_dots[i] = RT_NULL;
        s_media_cover_titles[i] = RT_NULL;
        s_media_cover_tracks[i] = 0U;
        s_media_cover_perspective[i] = 0;
        s_media_cover_opacity[i] = LV_OPA_TRANSP;
    }
}

static void media_format_time(uint32_t milliseconds, char *text,
                              size_t text_size)
{
    uint32_t seconds = milliseconds / 1000U;
    lv_snprintf(text, text_size, "%lu:%02lu",
                (unsigned long)(seconds / 60U),
                (unsigned long)(seconds % 60U));
}

static void media_refresh_progress(const ft_player_status_t *status)
{
    char position[16];
    char duration[16];
    char line[40];
    uint32_t value = 0U;
    if (status == RT_NULL) return;
    if (status->duration_ms != 0U)
        value = (uint32_t)((uint64_t)status->position_ms * 1000U /
                           status->duration_ms);
    if (value > 1000U) value = 1000U;
    if (s_media_progress_bar != RT_NULL &&
        lv_obj_is_valid(s_media_progress_bar))
        lv_bar_set_value(s_media_progress_bar, (int32_t)value, LV_ANIM_OFF);
    if (s_media_progress_label != RT_NULL &&
        lv_obj_is_valid(s_media_progress_label))
    {
        media_format_time(status->position_ms, position, sizeof(position));
        media_format_time(status->duration_ms, duration, sizeof(duration));
        lv_snprintf(line, sizeof(line), "%s / %s", position, duration);
        lv_label_set_text(s_media_progress_label, line);
    }
}

static void media_monitor_cb(lv_timer_t *timer)
{
    ft_player_status_t status;
    bool playing;
    LV_UNUSED(timer);
    if (ft_player_get_status(&status) != RT_EOK) return;
    playing = status.state == FT_PLAYER_PLAYING ||
              status.state == FT_PLAYER_STARTING;
    if (status.generation != s_media_player_generation)
    {
        s_media_player_generation = status.generation;
        s_media_playing = playing;
        if (media_has_local_tracks() &&
            status.current_track < status.track_count &&
            (playing || status.state == FT_PLAYER_PAUSED))
            s_media_track = (int32_t)status.current_track;
        ft_tiles_set_live_loop(FT_PAGE_MEDIA, playing);
        update_media_labels();
        if (status.state == FT_PLAYER_ERROR)
            feathertalk_ui_alert(
                ft_preferences_text("播放失败", "Playback failed"),
                status.last_error == -RT_EBUSY ?
                    ft_preferences_text("扬声器正在被 USB Audio 使用。",
                                        "The speaker is in use by USB Audio.") :
                    ft_preferences_text("无法打开或解码这个 WAV / MP3 文件。",
                                        "Unable to open or decode this WAV / MP3 file."));
    }
    if (status.position_ms / 1000U != s_media_position_second ||
        status.generation != s_media_player_generation)
    {
        s_media_position_second = status.position_ms / 1000U;
        media_refresh_progress(&status);
    }
}

static void media_page_enter(void)
{
    ft_player_status_t status;
    (void)ft_player_scan();
    (void)ft_player_get_status(&status);
    if (status.track_count != 0U && status.current_track < status.track_count)
        s_media_track = (int32_t)status.current_track;
    else
        s_media_track = 0;
    s_media_player_generation = UINT32_MAX;
    s_media_position_second = UINT32_MAX;
    media_monitor_cb(s_media_monitor_timer);
}

static void media_page_leave(void)
{
    /* Local playback deliberately continues when the page is closed. */
}

static void media_clicked_cb(lv_event_t *event)
{
    ft_player_status_t status;
    int result = RT_EOK;
    LV_UNUSED(event);
    if (media_has_local_tracks())
    {
        (void)ft_player_get_status(&status);
        if (status.state == FT_PLAYER_PLAYING)
            result = ft_player_pause();
        else if (status.state == FT_PLAYER_PAUSED &&
                 status.current_track == (size_t)s_media_track)
            result = ft_player_resume();
        else
            result = ft_player_play((size_t)s_media_track);
        if (result != RT_EOK)
        {
            feathertalk_ui_alert(
                ft_preferences_text("无法播放", "Unable to play"),
                ft_preferences_text("音频输出当前不可用。",
                                    "The audio output is currently unavailable."));
            return;
        }
        s_media_playing = status.state != FT_PLAYER_PLAYING;
    }
    else
    {
        feathertalk_ui_alert(
            ft_preferences_text("没有本地音频", "No local audio"),
            ft_preferences_text(
                "请选择包含 WAV 或 MP3 的文件夹。播放列表只包含该文件夹直属的音频文件。",
                "Choose a folder containing WAV or MP3 files. Its direct audio children become the playlist."));
        return;
    }
    ft_tiles_set_live_loop(FT_PAGE_MEDIA, s_media_playing);
    update_media_labels();
}

static void media_volume_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    (void)ft_audio_set_output_volume((uint8_t)lv_slider_get_value(slider));
}

static void media_prev_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if (!media_cover_start_turn(-1) && s_media_cover_flow == RT_NULL)
        media_cover_commit_offset(-1);
}

static void media_next_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if (!media_cover_start_turn(1) && s_media_cover_flow == RT_NULL)
        media_cover_commit_offset(1);
}

static lv_obj_t *create_media_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    int32_t page_width = layout->screen_width - 2 * layout->page_padding;
    int32_t flow_width;
    int32_t flow_height;
    int32_t stage_height;
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_t *header = lv_obj_create(page);
    lv_obj_t *heading = lv_obj_create(header);
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *source_row;
    lv_obj_t *stage;
    lv_obj_t *details;
    lv_obj_t *row;
    lv_obj_t *volume_caption;
    ft_player_status_t player_status;
    size_t i;

    ft_ui_style_page(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(page, layout->page_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page, layout->section_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_add_event_cb(page, media_page_deleted_cb, LV_EVENT_DELETE, RT_NULL);

    style_layout_container(header);
    lv_obj_set_size(header, lv_pct(100), layout->control_height);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, ft_layout_px(12), LV_PART_MAIN);
    (void)ft_icon_create(header, FT_ICON_MEDIA, ft_layout_icon_size(32U), true);
    style_layout_container(heading);
    lv_obj_set_height(heading, lv_pct(100));
    lv_obj_set_width(heading, 0);
    lv_obj_set_flex_grow(heading, 1);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    title = lv_label_create(heading);
    lv_label_set_text(title, ft_preferences_text("音乐", "Music"));
    lv_obj_set_style_text_font(title, ft_layout_font(22), LV_PART_MAIN);
    ft_ui_register_accent(title, FT_ACCENT_TEXT);
    subtitle = lv_label_create(heading);
    lv_label_set_text(subtitle, ft_preferences_text(
        "文件夹播放列表 · WAV / MP3 · 3D 专辑流",
        "Folder playlist · WAV / MP3 · 3D Cover Flow"));
    lv_obj_set_style_text_font(subtitle, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xA8A8A8), LV_PART_MAIN);

    source_row = lv_obj_create(page);
    style_layout_container(source_row);
    lv_obj_set_size(source_row, lv_pct(100), ft_layout_px(44));
    lv_obj_set_flex_flow(source_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(source_row, ft_layout_px(6), LV_PART_MAIN);
    {
        lv_obj_t *folder_button = lv_button_create(source_row);
        track_object(&s_media_directory_label, lv_label_create(folder_button));
        lv_obj_set_height(folder_button, lv_pct(100));
        lv_obj_set_width(folder_button, 0);
        lv_obj_set_flex_grow(folder_button, 3);
        lv_obj_set_style_radius(folder_button, ft_layout_px(4), LV_PART_MAIN);
        lv_label_set_long_mode(s_media_directory_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_media_directory_label, lv_pct(94));
        lv_obj_set_style_text_font(s_media_directory_label,
                                   ft_layout_font(12), LV_PART_MAIN);
        lv_obj_center(s_media_directory_label);
        lv_obj_add_event_cb(folder_button, media_folder_open_cb,
                            LV_EVENT_CLICKED, RT_NULL);
    }
    track_object(&s_media_loop_button, lv_button_create(source_row));
    track_object(&s_media_loop_label, lv_label_create(s_media_loop_button));
    lv_obj_set_height(s_media_loop_button, lv_pct(100));
    lv_obj_set_width(s_media_loop_button, 0);
    lv_obj_set_flex_grow(s_media_loop_button, 1);
    lv_obj_set_style_radius(s_media_loop_button, ft_layout_px(4), LV_PART_MAIN);
    (void)ft_player_get_status(&player_status);
    lv_label_set_text(s_media_loop_label, player_status.folder_loop ?
        ft_preferences_text("循环开", "Loop on") :
        ft_preferences_text("循环关", "Loop off"));
    lv_obj_set_style_text_font(s_media_loop_label, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_center(s_media_loop_label);
    lv_obj_add_event_cb(s_media_loop_button, media_loop_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    media_refresh_directory_label();

    stage = lv_obj_create(page);

    stage_height = layout->landscape ?
                   media_clamp_i32(layout->screen_height - layout->status_bar_height -
                                   layout->nav_bar_height - 2 * layout->page_padding -
                                   layout->control_height - ft_layout_px(44) -
                                   2 * layout->section_gap,
                                   ft_layout_px(260), ft_layout_px(410)) :
                   media_clamp_i32(ft_layout_px(470), ft_layout_px(390),
                                   layout->screen_height - layout->status_bar_height -
                                   layout->nav_bar_height - layout->control_height);
    style_layout_container(stage);
    lv_obj_set_width(stage, lv_pct(100));
    lv_obj_set_height(stage, layout->landscape ? stage_height : LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(stage, layout->section_gap, LV_PART_MAIN);
    lv_obj_set_style_pad_row(stage, layout->section_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(stage, layout->landscape ? LV_FLEX_FLOW_ROW :
                                                   LV_FLEX_FLOW_COLUMN);

    flow_width = layout->landscape ? page_width * 3 / 5 : page_width;
    flow_height = layout->landscape ? stage_height :
                  media_clamp_i32(stage_height * 3 / 5,
                                  ft_layout_px(220), ft_layout_px(290));
    track_object(&s_media_cover_flow, lv_obj_create(stage));
    style_layout_container(s_media_cover_flow);
    lv_obj_set_size(s_media_cover_flow, flow_width, flow_height);
    lv_obj_add_flag(s_media_cover_flow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_media_cover_flow, LV_OBJ_FLAG_SCROLLABLE |
                                           LV_OBJ_FLAG_SCROLL_ONE |
                                           LV_OBJ_FLAG_SCROLL_CHAIN |
                                           LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scrollbar_mode(s_media_cover_flow, LV_SCROLLBAR_MODE_OFF);
    s_media_cover_max_size = media_clamp_i32(flow_width * 11 / 20,
                                             ft_layout_px(150), flow_height -
                                             ft_layout_px(24));
    s_media_cover_min_width = s_media_cover_max_size * 46 / 100;
    s_media_cover_min_height = s_media_cover_max_size * 78 / 100;
    s_media_cover_cell_width = s_media_cover_max_size + ft_layout_px(10);
    lv_obj_add_event_cb(s_media_cover_flow, media_cover_event_cb,
                        LV_EVENT_ALL, RT_NULL);
    for (i = 0U; i < FT_MEDIA_FLOW_CELL_COUNT; i++)
        (void)media_cover_create_cell(s_media_cover_flow, i, flow_height);
    s_media_cover_state = FT_MEDIA_COVER_IDLE;
    s_media_cover_visual_dirty = false;
    s_media_cover_pending_offset = 0;
    s_media_cover_visual_offset = 0;
    s_media_cover_anim_start_offset = 0;
    s_media_cover_anim_target_offset = 0;
    s_media_cover_drag_start_x = 0;
    s_media_cover_state_tick = lv_tick_get();
    s_media_cover_stress_active = false;
    s_media_cover_stress_remaining = 0U;
    s_media_cover_stress_completed = 0U;
    s_media_cover_control_timer = lv_timer_create(media_cover_control_timer_cb,
                                                   FT_MEDIA_FLOW_CONTROL_PERIOD_MS,
                                                   RT_NULL);
    if (s_media_cover_control_timer != RT_NULL)
        lv_timer_pause(s_media_cover_control_timer);

    /* Child creation order is the visual/flex order.  Build Cover Flow first,
     * then its metadata so portrait and landscape both put artwork before the
     * text/controls. */
    details = lv_obj_create(stage);
    style_layout_container(details);
    if (layout->landscape)
    {
        lv_obj_set_height(details, lv_pct(100));
        lv_obj_set_width(details, 0);
        lv_obj_set_flex_grow(details, 1);
    }
    else
    {
        /* The metadata block is content-sized.  Its former fixed remainder
         * assumed a specific set of font line heights and made long filenames
         * overlap the artist, controls and volume slider after a font/scale
         * change.  The page itself scrolls, so the measured content is the
         * correct constraint. */
        lv_obj_set_width(details, lv_pct(100));
        lv_obj_set_height(details, LV_SIZE_CONTENT);
    }
    lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(details, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(details, ft_layout_px(6), LV_PART_MAIN);
    track_object(&s_media_track_label, lv_label_create(details));
    lv_obj_set_width(s_media_track_label, lv_pct(100));
    lv_label_set_long_mode(s_media_track_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_media_track_label, ft_layout_font(22), LV_PART_MAIN);
    lv_obj_set_height(s_media_track_label,
                      lv_font_get_line_height(ft_layout_font(22)));
    ft_ui_register_accent(s_media_track_label, FT_ACCENT_TEXT);
    track_object(&s_media_artist_label, lv_label_create(details));
    lv_obj_set_width(s_media_artist_label, lv_pct(100));
    lv_label_set_long_mode(s_media_artist_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_media_artist_label, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_set_height(s_media_artist_label,
                      lv_font_get_line_height(ft_layout_font(14)));
    lv_obj_set_style_text_color(s_media_artist_label, lv_color_hex(0xD0D0D0), LV_PART_MAIN);
    track_object(&s_media_album_label, lv_label_create(details));
    lv_obj_set_width(s_media_album_label, lv_pct(100));
    lv_label_set_long_mode(s_media_album_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_media_album_label, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_height(s_media_album_label,
                      lv_font_get_line_height(ft_layout_font(12)));
    lv_obj_set_style_text_color(s_media_album_label, lv_color_hex(0x909090), LV_PART_MAIN);

    track_object(&s_media_progress_bar, lv_bar_create(details));
    lv_obj_set_size(s_media_progress_bar, lv_pct(100), ft_layout_px(6));
    lv_bar_set_range(s_media_progress_bar, 0, 1000);
    lv_bar_set_value(s_media_progress_bar, 0, LV_ANIM_OFF);
    track_object(&s_media_progress_label, lv_label_create(details));
    lv_label_set_text(s_media_progress_label, "0:00 / 0:00");
    lv_obj_set_style_text_font(s_media_progress_label, ft_layout_font(11), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_media_progress_label,
                                lv_color_hex(0x909090), LV_PART_MAIN);

    row = lv_obj_create(details);
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
    volume_caption = lv_label_create(details);
    lv_label_set_text(volume_caption, ft_preferences_text("音量", "Volume"));
    lv_obj_set_style_text_font(volume_caption, ft_layout_font(12), LV_PART_MAIN);
    track_object(&s_media_volume, lv_slider_create(details));
    lv_obj_set_size(s_media_volume, lv_pct(100), ft_layout_px(20));
    lv_slider_set_range(s_media_volume, 0, 100);
    lv_slider_set_value(s_media_volume,
                        ft_preferences_get()->audio_output_volume,
                        LV_ANIM_OFF);
    lv_obj_add_event_cb(s_media_volume, media_volume_cb,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);

    s_media_playing = false;
    s_media_track = 0;
    s_media_player_generation = UINT32_MAX;
    s_media_position_second = UINT32_MAX;
    s_media_monitor_timer = lv_timer_create(media_monitor_cb, 200U, RT_NULL);
    update_media_labels();
    lv_obj_update_layout(page);
    media_cover_recenter();
    return page;
}

typedef struct
{
    size_t directories;
    size_t files;
} ft_files_list_context_t;

static uint8_t s_files_directory_marker;
static void files_refresh_view(bool manual_refresh);
static void files_preview_file(const char *path, const char *name);

typedef enum
{
    FT_FILES_ACTION_VIEW = 0,
    FT_FILES_ACTION_COPY,
    FT_FILES_ACTION_CUT,
    FT_FILES_ACTION_RENAME,
    FT_FILES_ACTION_NEW_FOLDER,
    FT_FILES_ACTION_REFRESH,
    FT_FILES_ACTION_PASTE,
    FT_FILES_ACTION_DELETE,
    FT_FILES_ACTION_CANCEL
} ft_files_action_t;

static void files_close_action_menu(void)
{
    if (s_files_action_box != RT_NULL && lv_obj_is_valid(s_files_action_box))
        lv_msgbox_close(s_files_action_box);
    s_files_action_box = RT_NULL;
    s_files_action_quick = RT_NULL;
    s_files_action_view = RT_NULL;
    s_files_action_copy = RT_NULL;
    s_files_action_cut = RT_NULL;
    s_files_action_rename = RT_NULL;
    s_files_action_new_folder = RT_NULL;
    s_files_action_refresh = RT_NULL;
    s_files_action_paste = RT_NULL;
    s_files_action_delete = RT_NULL;
    s_files_action_cancel = RT_NULL;
    s_files_context_path[0] = '\0';
    s_files_context_name[0] = '\0';
    s_files_context_is_directory = false;
    s_files_context_current_folder = false;
}

static void files_close_name_editor(void)
{
    if (s_files_name_box != RT_NULL && lv_obj_is_valid(s_files_name_box))
        lv_msgbox_close(s_files_name_box);
    s_files_name_box = RT_NULL;
    s_files_name_textarea = RT_NULL;
    s_files_name_error = RT_NULL;
    s_files_name_keyboard = RT_NULL;
    s_files_name_cancel = RT_NULL;
    s_files_name_confirm = RT_NULL;
    s_files_name_target[0] = '\0';
    s_files_name_is_rename = false;
}

static void files_name_cancel_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    files_close_name_editor();
}

static void files_name_set_error(const char *text)
{
    if (s_files_name_error != RT_NULL && lv_obj_is_valid(s_files_name_error))
        lv_label_set_text(s_files_name_error, text != RT_NULL ? text : "");
}

static void files_name_confirm_cb(lv_event_t *event)
{
    const char *input;
    char name[FT_STORAGE_NAME_MAX];
    char result_path[FT_STORAGE_PATH_MAX];
    size_t start = 0U;
    size_t end;
    size_t length;
    bool rename_item = s_files_name_is_rename;
    int result;

    LV_UNUSED(event);
    if (s_files_name_textarea == RT_NULL ||
        !lv_obj_is_valid(s_files_name_textarea))
        return;
    input = lv_textarea_get_text(s_files_name_textarea);
    if (input == RT_NULL) input = "";
    end = strlen(input);
    while (start < end && (input[start] == ' ' || input[start] == '\t')) start++;
    while (end > start && (input[end - 1U] == ' ' || input[end - 1U] == '\t')) end--;
    length = end - start;
    if (length == 0U || length >= sizeof(name))
    {
        files_name_set_error(ft_preferences_text(
            "请输入有效名称。", "Enter a valid name."));
        return;
    }
    rt_memcpy(name, input + start, length);
    name[length] = '\0';
    result = rename_item ?
        ft_storage_rename_path(s_files_name_target, name, result_path,
                               sizeof(result_path)) :
        ft_storage_create_directory(s_files_name_target, name, result_path,
                                    sizeof(result_path));
    if (result != RT_EOK)
    {
        files_name_set_error(ft_preferences_text(
            "名称无效、已存在，或存储设备当前不可写。",
            "The name is invalid or already exists, or the storage device is not writable."));
        return;
    }
    if (rename_item && strcmp(s_files_clipboard_path, s_files_name_target) == 0)
    {
        rt_strncpy(s_files_clipboard_path, result_path,
                   sizeof(s_files_clipboard_path) - 1U);
        s_files_clipboard_path[sizeof(s_files_clipboard_path) - 1U] = '\0';
        rt_strncpy(s_files_clipboard_name, name,
                   sizeof(s_files_clipboard_name) - 1U);
        s_files_clipboard_name[sizeof(s_files_clipboard_name) - 1U] = '\0';
    }
    files_close_name_editor();
    files_refresh_view(false);
}

static void files_name_keyboard_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY)
        files_name_confirm_cb(event);
    else if (code == LV_EVENT_CANCEL)
        files_name_cancel_cb(event);
}

static void files_show_name_editor(bool rename_item, const char *target,
                                   const char *initial_name)
{
    lv_obj_t *content;
    lv_obj_t *title;
    lv_obj_t *label;

    if (target == RT_NULL || target[0] == '\0') return;
    files_close_name_editor();
    s_files_name_is_rename = rename_item;
    rt_strncpy(s_files_name_target, target,
               sizeof(s_files_name_target) - 1U);
    s_files_name_target[sizeof(s_files_name_target) - 1U] = '\0';
    s_files_name_box = lv_msgbox_create(RT_NULL);
    lv_obj_set_size(s_files_name_box, lv_pct(94), lv_pct(88));
    title = lv_msgbox_add_title(
        s_files_name_box,
        rename_item ? ft_preferences_text("重命名", "Rename") :
                      ft_preferences_text("新建文件夹", "New folder"));
    lv_obj_set_style_text_font(title, ft_layout_font(18), LV_PART_MAIN);
    content = lv_msgbox_get_content(s_files_name_box);
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_row(content, ft_layout_px(5), LV_PART_MAIN);
    s_files_name_textarea = lv_textarea_create(content);
    lv_obj_set_size(s_files_name_textarea, lv_pct(100),
                    ft_layout_get()->control_height);
    lv_obj_set_style_text_font(s_files_name_textarea, ft_layout_font(16),
                               LV_PART_MAIN);
    lv_textarea_set_one_line(s_files_name_textarea, true);
    lv_textarea_set_max_length(s_files_name_textarea,
                               FT_STORAGE_NAME_MAX - 1U);
    lv_textarea_set_placeholder_text(
        s_files_name_textarea,
        ft_preferences_text("输入名称", "Enter a name"));
    lv_textarea_set_text(s_files_name_textarea,
                         initial_name != RT_NULL ? initial_name : "");
    lv_textarea_set_cursor_pos(s_files_name_textarea, LV_TEXTAREA_CURSOR_LAST);
    s_files_name_error = lv_label_create(content);
    lv_label_set_text(s_files_name_error, "");
    lv_obj_set_width(s_files_name_error, lv_pct(100));
    lv_obj_set_style_text_color(s_files_name_error,
                                lv_color_hex(0xFF6B6B), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_files_name_error, ft_layout_font(12),
                               LV_PART_MAIN);
    s_files_name_keyboard = lv_keyboard_create(content);
    lv_obj_set_width(s_files_name_keyboard, lv_pct(100));
    lv_obj_set_height(s_files_name_keyboard, 0);
    lv_obj_set_flex_grow(s_files_name_keyboard, 1);
    lv_keyboard_set_textarea(s_files_name_keyboard, s_files_name_textarea);
    lv_obj_add_event_cb(s_files_name_keyboard, files_name_keyboard_cb,
                        LV_EVENT_READY, RT_NULL);
    lv_obj_add_event_cb(s_files_name_keyboard, files_name_keyboard_cb,
                        LV_EVENT_CANCEL, RT_NULL);
    s_files_name_cancel = lv_msgbox_add_footer_button(
        s_files_name_box, ft_preferences_text("取消", "Cancel"));
    s_files_name_confirm = lv_msgbox_add_footer_button(
        s_files_name_box,
        rename_item ? ft_preferences_text("保存", "Save") :
                      ft_preferences_text("创建", "Create"));
    label = lv_obj_get_child_count(s_files_name_cancel) > 0U ?
            lv_obj_get_child(s_files_name_cancel, 0U) : RT_NULL;
    if (label != RT_NULL)
        lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
    label = lv_obj_get_child_count(s_files_name_confirm) > 0U ?
            lv_obj_get_child(s_files_name_confirm, 0U) : RT_NULL;
    if (label != RT_NULL)
        lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_add_event_cb(s_files_name_cancel, files_name_cancel_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(s_files_name_confirm, files_name_confirm_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_send_event(s_files_name_textarea, LV_EVENT_FOCUSED, RT_NULL);
}

static void files_close_delete_confirmation(void)
{
    if (s_files_delete_box != RT_NULL && lv_obj_is_valid(s_files_delete_box))
        lv_msgbox_close(s_files_delete_box);
    s_files_delete_box = RT_NULL;
    s_files_delete_cancel = RT_NULL;
    s_files_delete_confirm = RT_NULL;
}

static void files_delete_cancel_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    files_close_delete_confirmation();
    s_files_delete_path[0] = '\0';
    s_files_delete_name[0] = '\0';
}

static void files_delete_confirm_cb(lv_event_t *event)
{
    int result;
    char name[FT_STORAGE_NAME_MAX];
    bool was_directory = s_files_delete_is_directory;

    LV_UNUSED(event);
    rt_strncpy(name, s_files_delete_name, sizeof(name) - 1U);
    name[sizeof(name) - 1U] = '\0';
    result = ft_storage_delete_path(s_files_delete_path);
    files_close_delete_confirmation();
    s_files_delete_path[0] = '\0';
    s_files_delete_name[0] = '\0';
    files_refresh_view(false);
    if (result == RT_EOK)
    {
        feathertalk_ui_alert(ft_preferences_text("已删除", "Deleted"), name);
    }
    else
    {
        feathertalk_ui_alert(
            ft_preferences_text("无法删除", "Unable to delete"),
            was_directory ?
            ft_preferences_text("文件夹可能已被移除、介质为只读，或正在被其他功能使用。",
                                "The folder may be gone, read-only, or in use by another feature.") :
            ft_preferences_text("文件可能已被移除、处于只读介质上，或正在被其他功能使用。",
                                "The file may be gone, on read-only media, or in use by another feature."));
    }
}

static void files_show_delete_confirmation(const char *name, const char *path,
                                           bool is_directory)
{
    lv_obj_t *title;
    lv_obj_t *text;
    char message[FT_STORAGE_NAME_MAX + 128U];

    if (name == RT_NULL || path == RT_NULL) return;
    files_close_delete_confirmation();
    rt_strncpy(s_files_delete_name, name, sizeof(s_files_delete_name) - 1U);
    s_files_delete_name[sizeof(s_files_delete_name) - 1U] = '\0';
    rt_strncpy(s_files_delete_path, path, sizeof(s_files_delete_path) - 1U);
    s_files_delete_path[sizeof(s_files_delete_path) - 1U] = '\0';
    s_files_delete_is_directory = is_directory;
    lv_snprintf(message, sizeof(message),
                is_directory ?
                ft_preferences_text("删除文件夹“%s”及其中全部内容？\n此操作不可撤销。",
                                    "Delete folder \"%s\" and all its contents?\nThis cannot be undone.") :
                ft_preferences_text("删除文件“%s”？\n此操作不可撤销。",
                                    "Delete file \"%s\"?\nThis cannot be undone."),
                name);
    track_object(&s_files_delete_box, lv_msgbox_create(RT_NULL));
    lv_obj_set_width(s_files_delete_box, lv_pct(88));
    title = lv_msgbox_add_title(
        s_files_delete_box,
        is_directory ? ft_preferences_text("删除文件夹", "Delete folder") :
                       ft_preferences_text("删除文件", "Delete file"));
    text = lv_msgbox_add_text(s_files_delete_box, message);
    lv_obj_set_style_text_font(title, ft_layout_font(18), LV_PART_MAIN);
    lv_obj_set_style_text_font(text, ft_layout_font(14), LV_PART_MAIN);
    track_object(&s_files_delete_cancel,
                 lv_msgbox_add_footer_button(
                     s_files_delete_box, ft_preferences_text("取消", "Cancel")));
    track_object(&s_files_delete_confirm,
                 lv_msgbox_add_footer_button(
                     s_files_delete_box, ft_preferences_text("删除", "Delete")));
    if (lv_obj_get_child_count(s_files_delete_cancel) > 0U)
        lv_obj_set_style_text_font(lv_obj_get_child(s_files_delete_cancel, 0U),
                                   ft_layout_font(14), LV_PART_MAIN);
    if (lv_obj_get_child_count(s_files_delete_confirm) > 0U)
        lv_obj_set_style_text_font(lv_obj_get_child(s_files_delete_confirm, 0U),
                                   ft_layout_font(14), LV_PART_MAIN);
    lv_obj_add_event_cb(s_files_delete_cancel, files_delete_cancel_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(s_files_delete_confirm, files_delete_confirm_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_set_style_bg_color(s_files_delete_confirm,
                              lv_color_hex(0xC42B1C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_files_delete_confirm, LV_OPA_COVER, LV_PART_MAIN);
}

static void files_action_clicked_cb(lv_event_t *event)
{
    ft_files_action_t action =
        (ft_files_action_t)(uintptr_t)lv_event_get_user_data(event);
    char context_path[FT_STORAGE_PATH_MAX];
    char context_name[FT_STORAGE_NAME_MAX];
    char destination[FT_STORAGE_PATH_MAX];
    char result_path[FT_STORAGE_PATH_MAX];
    bool context_is_directory = s_files_context_is_directory;
    bool clipboard_cut = s_files_clipboard_cut;
    int result;

    rt_strncpy(context_path, s_files_context_path, sizeof(context_path) - 1U);
    context_path[sizeof(context_path) - 1U] = '\0';
    rt_strncpy(context_name, s_files_context_name, sizeof(context_name) - 1U);
    context_name[sizeof(context_name) - 1U] = '\0';
    if (action == FT_FILES_ACTION_CANCEL)
    {
        files_close_action_menu();
        return;
    }
    if (action == FT_FILES_ACTION_VIEW)
    {
        files_close_action_menu();
        if (context_is_directory)
        {
            rt_strncpy(s_files_current_path, context_path,
                       sizeof(s_files_current_path) - 1U);
            s_files_current_path[sizeof(s_files_current_path) - 1U] = '\0';
            files_refresh_view(false);
        }
        else if (ft_gallery_can_open_file(context_path))
        {
            if (ft_gallery_request_open_file(context_path))
                (void)ft_router_push(FT_PAGE_GALLERY);
            else
                feathertalk_ui_alert(
                    ft_preferences_text("无法查看", "Unable to view"),
                    ft_preferences_text("图片格式、路径或存储介质当前不可用。",
                                        "The image format, path, or medium is unavailable."));
        }
        else
        {
            files_preview_file(context_path, context_name);
        }
        return;
    }
    if (action == FT_FILES_ACTION_COPY || action == FT_FILES_ACTION_CUT)
    {
        rt_strncpy(s_files_clipboard_path, context_path,
                   sizeof(s_files_clipboard_path) - 1U);
        s_files_clipboard_path[sizeof(s_files_clipboard_path) - 1U] = '\0';
        rt_strncpy(s_files_clipboard_name, context_name,
                   sizeof(s_files_clipboard_name) - 1U);
        s_files_clipboard_name[sizeof(s_files_clipboard_name) - 1U] = '\0';
        s_files_clipboard_cut = action == FT_FILES_ACTION_CUT;
        files_close_action_menu();
        feathertalk_ui_alert(
            action == FT_FILES_ACTION_CUT ? ft_preferences_text("已剪切", "Cut") :
                                            ft_preferences_text("已复制", "Copied"),
            ft_preferences_text("请长按目标文件夹或空白区域，然后选择“粘贴”。",
                                "Long-press the destination folder or empty area, then choose Paste."));
        return;
    }
    if (action == FT_FILES_ACTION_RENAME)
    {
        files_close_action_menu();
        files_show_name_editor(true, context_path, context_name);
        return;
    }
    if (action == FT_FILES_ACTION_NEW_FOLDER)
    {
        rt_strncpy(destination,
                   context_is_directory ? context_path : s_files_current_path,
                   sizeof(destination) - 1U);
        destination[sizeof(destination) - 1U] = '\0';
        files_close_action_menu();
        files_show_name_editor(false, destination, "");
        return;
    }
    if (action == FT_FILES_ACTION_REFRESH)
    {
        files_close_action_menu();
        files_refresh_view(true);
        return;
    }
    if (action == FT_FILES_ACTION_DELETE)
    {
        files_close_action_menu();
        files_show_delete_confirmation(context_name, context_path,
                                       context_is_directory);
        return;
    }
    if (action != FT_FILES_ACTION_PASTE || s_files_clipboard_path[0] == '\0')
        return;
    rt_strncpy(destination,
               context_is_directory ? context_path : s_files_current_path,
               sizeof(destination) - 1U);
    destination[sizeof(destination) - 1U] = '\0';
    files_close_action_menu();
    result = ft_storage_paste_path(s_files_clipboard_path, destination,
                                   clipboard_cut, result_path,
                                   sizeof(result_path));
    if (result == RT_EOK)
    {
        if (clipboard_cut)
        {
            s_files_clipboard_path[0] = '\0';
            s_files_clipboard_name[0] = '\0';
            s_files_clipboard_cut = false;
        }
        files_refresh_view(false);
        feathertalk_ui_alert(ft_preferences_text("粘贴完成", "Paste complete"),
                            s_files_clipboard_name[0] != '\0' ?
                            s_files_clipboard_name :
                            ft_preferences_text("项目已移动。", "The item was moved."));
    }
    else
    {
        feathertalk_ui_alert(ft_preferences_text("无法粘贴", "Unable to paste"),
                            ft_preferences_text("请检查目标空间、介质状态和文件夹层级。不能把文件夹粘贴进自身。",
                                                "Check free space, media state, and folder nesting. A folder cannot be pasted into itself."));
    }
}

static lv_obj_t *files_add_action_button(lv_obj_t *parent,
                                         const char *symbol,
                                         const char *text,
                                         ft_files_action_t action,
                                         bool quick)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);
    char caption[FT_STORAGE_NAME_MAX + 16U];

    lv_obj_set_style_radius(button, quick ? ft_layout_px(4) : 0,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x242424), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, quick ? LV_OPA_COVER : LV_OPA_50,
                            LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, quick ? 0 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(button, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x454545), LV_PART_MAIN);
    lv_obj_set_height(button, ft_layout_px(quick ? 64 : 46));
    if (quick)
    {
        lv_obj_set_width(button, 0);
        lv_obj_set_flex_grow(button, 1);
        lv_snprintf(caption, sizeof(caption), "%s\n%s", symbol, text);
        lv_label_set_text(label, caption);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
        lv_obj_center(label);
    }
    else
    {
        lv_obj_set_width(button, lv_pct(100));
        lv_snprintf(caption, sizeof(caption), "%s   %s", symbol, text);
        lv_label_set_text(label, caption);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_add_event_cb(button, files_action_clicked_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)action);
    return button;
}

static void files_show_action_menu(const char *name, const char *path,
                                   bool is_directory, bool current_folder)
{
    lv_obj_t *title;
    lv_obj_t *content;
    lv_obj_t *separator;

    if (name == RT_NULL || path == RT_NULL) return;
    files_close_action_menu();
    rt_strncpy(s_files_context_name, name, sizeof(s_files_context_name) - 1U);
    s_files_context_name[sizeof(s_files_context_name) - 1U] = '\0';
    rt_strncpy(s_files_context_path, path, sizeof(s_files_context_path) - 1U);
    s_files_context_path[sizeof(s_files_context_path) - 1U] = '\0';
    s_files_context_is_directory = is_directory;
    s_files_context_current_folder = current_folder;
    s_files_action_box = lv_msgbox_create(RT_NULL);
    lv_obj_set_width(s_files_action_box, lv_pct(86));
    title = lv_msgbox_add_title(s_files_action_box, name);
    lv_obj_set_style_text_font(title, ft_layout_font(18), LV_PART_MAIN);
    content = lv_msgbox_get_content(s_files_action_box);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, ft_layout_px(3), LV_PART_MAIN);
    s_files_action_quick = lv_obj_create(content);
    lv_obj_set_size(s_files_action_quick, lv_pct(100), ft_layout_px(64));
    lv_obj_set_style_bg_opa(s_files_action_quick, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_files_action_quick, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_files_action_quick, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_files_action_quick, ft_layout_px(3), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_files_action_quick, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(s_files_action_quick, LV_OBJ_FLAG_SCROLLABLE);
    s_files_action_cut = files_add_action_button(
        s_files_action_quick, LV_SYMBOL_CUT,
        ft_preferences_text("剪切", "Cut"), FT_FILES_ACTION_CUT, true);
    s_files_action_copy = files_add_action_button(
        s_files_action_quick, LV_SYMBOL_COPY,
        ft_preferences_text("复制", "Copy"), FT_FILES_ACTION_COPY, true);
    s_files_action_rename = files_add_action_button(
        s_files_action_quick, LV_SYMBOL_EDIT,
        ft_preferences_text("重命名", "Rename"), FT_FILES_ACTION_RENAME, true);
    s_files_action_delete = files_add_action_button(
        s_files_action_quick, LV_SYMBOL_TRASH,
        ft_preferences_text("删除", "Delete"), FT_FILES_ACTION_DELETE, true);
    separator = lv_obj_create(content);
    lv_obj_set_size(separator, lv_pct(100), 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(separator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(separator, 0, LV_PART_MAIN);
    s_files_action_view = files_add_action_button(
        content, LV_SYMBOL_EYE_OPEN,
        ft_preferences_text("打开", "Open"), FT_FILES_ACTION_VIEW, false);
    s_files_action_refresh = files_add_action_button(
        content, LV_SYMBOL_REFRESH,
        ft_preferences_text("刷新", "Refresh"), FT_FILES_ACTION_REFRESH, false);
    s_files_action_new_folder = files_add_action_button(
        content, LV_SYMBOL_DIRECTORY,
        ft_preferences_text("新建文件夹", "New folder"),
        FT_FILES_ACTION_NEW_FOLDER, false);
    s_files_action_paste = files_add_action_button(
        content, LV_SYMBOL_PASTE,
        ft_preferences_text("粘贴", "Paste"), FT_FILES_ACTION_PASTE, false);
    s_files_action_cancel = files_add_action_button(
        content, LV_SYMBOL_CLOSE,
        ft_preferences_text("取消", "Cancel"), FT_FILES_ACTION_CANCEL, false);
    lv_obj_set_style_text_color(lv_obj_get_child(s_files_action_delete, 0U),
                                lv_color_hex(0xFF8A80), LV_PART_MAIN);
    if (!is_directory)
    {
        lv_obj_add_flag(s_files_action_new_folder, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_files_action_paste, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_files_clipboard_path[0] == '\0')
        lv_obj_add_state(s_files_action_paste, LV_STATE_DISABLED);
    if (current_folder)
    {
        lv_obj_add_flag(s_files_action_quick, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_files_action_view, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_files_action_refresh, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_update_layout(s_files_action_box);
}

static void files_entry_long_pressed_cb(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_target(event);
    lv_obj_t *name_label;
    const char *name;
    char path[FT_STORAGE_PATH_MAX];
    bool is_directory = lv_event_get_user_data(event) ==
                        &s_files_directory_marker;

    if (row == RT_NULL || lv_obj_get_child_count(row) == 0U)
        return;
    name_label = lv_obj_get_child(row, 0U);
    if (name_label == RT_NULL || !lv_obj_check_type(name_label, &lv_label_class))
        return;
    name = lv_label_get_text(name_label);
    if (strcmp(s_files_current_path, FT_STORAGE_BROWSE_ROOT) == 0)
    {
        ft_storage_device_info_t info;
        ft_storage_device_t device;
        int info_result;

        if (strcmp(name, "flash") == 0)
            device = FT_STORAGE_DEVICE_FLASH;
        else if (strcmp(name, "sdcard") == 0)
            device = FT_STORAGE_DEVICE_SD;
        else
            return;
        s_files_suppress_click_row = row;
        s_storage_selected_device = device;
        s_storage_format_target = FT_STORAGE_DEVICE_INVALID;
        s_storage_format_from_files = false;
        info_result = settings_storage_get_info(device, &info);
        if (info_result == RT_EOK && info.can_format)
        {
            s_storage_format_from_files = true;
            s_storage_format_target = device;
            settings_storage_show_confirmation(1U);
        }
        else
        {
            feathertalk_ui_alert(
                ft_preferences_text("当前不能格式化", "Formatting unavailable"),
                device == FT_STORAGE_DEVICE_SD ?
                ft_preferences_text("SD 卡未插入、正在使用，或已交给 USB 主机。",
                                    "The SD card is absent, busy, or owned by the USB host.") :
                ft_preferences_text("内置 Flash 正在使用，或已交给 USB 主机。",
                                    "Internal Flash is busy or owned by the USB host."));
        }
        return;
    }
    if (ft_storage_join_path(s_files_current_path, name, path,
                             sizeof(path)) != RT_EOK)
        return;
    s_files_suppress_click_row = row;
    files_show_action_menu(name, path, is_directory, false);
}

static void files_list_long_pressed_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_files_list ||
        strcmp(s_files_current_path, FT_STORAGE_BROWSE_ROOT) == 0)
        return;
    files_show_action_menu(ft_preferences_text("当前文件夹", "Current folder"),
                           s_files_current_path, true, true);
}

static void files_format_bytes(uint64_t bytes, char *text, size_t text_size)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    uint64_t divisor = 1U;
    uint32_t unit = 0U;
    uint32_t whole;
    uint32_t decimal;

    while (unit < 4U && bytes >= divisor * 1024U)
    {
        divisor *= 1024U;
        unit++;
    }
    whole = (uint32_t)(bytes / divisor);
    decimal = divisor > 1U ? (uint32_t)(((bytes % divisor) * 10U) / divisor) : 0U;
    if (unit == 0U)
        lv_snprintf(text, text_size, "%lu %s", (unsigned long)whole, units[unit]);
    else
        lv_snprintf(text, text_size, "%lu.%lu %s", (unsigned long)whole,
                    (unsigned long)decimal, units[unit]);
}

static void files_preview_file(const char *path, const char *name)
{
    uint8_t preview[FT_FILES_PREVIEW_BYTES];
    char cleaned[FT_FILES_PREVIEW_BYTES];
    char size_text[24];
    char message[FT_FILES_PREVIEW_BYTES + 128U];
    uint64_t file_size = 0U;
    bool binary = false;
    int read_size;
    size_t source;
    size_t target = 0U;

    read_size = ft_storage_read_preview(path, preview, sizeof(preview),
                                        &binary, &file_size);
    if (read_size < 0)
    {
        feathertalk_ui_alert(name,
                            ft_preferences_text("无法读取这个文件。",
                                                "Unable to read this file."));
        return;
    }

    files_format_bytes(file_size, size_text, sizeof(size_text));
    if (binary)
    {
        lv_snprintf(message, sizeof(message),
                    ft_preferences_text("大小：%s\n二进制文件，当前仅显示文件信息。",
                                        "Size: %s\nBinary file; showing metadata only."),
                    size_text);
    }
    else
    {
        for (source = 0U; source < (size_t)read_size &&
             target + 1U < sizeof(cleaned); source++)
        {
            uint8_t value = preview[source];
            if (value == '\r') continue;
            cleaned[target++] = value == '\t' ? ' ' : (char)value;
        }
        cleaned[target] = '\0';
        lv_snprintf(message, sizeof(message),
                    ft_preferences_text("大小：%s\n\n%s%s",
                                        "Size: %s\n\n%s%s"),
                    size_text,
                    target > 0U ? cleaned : ft_preferences_text("（空文件）", "(empty file)"),
                    file_size > (uint64_t)read_size ?
                    ft_preferences_text("\n\n（仅预览开头内容）",
                                        "\n\n(previewing the beginning only)") : "");
    }
    feathertalk_ui_alert(name, message);
}

static void files_entry_clicked_cb(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_target(event);
    lv_obj_t *name_label;
    const char *name;
    char path[FT_STORAGE_PATH_MAX];

    if (row == s_files_suppress_click_row)
    {
        s_files_suppress_click_row = RT_NULL;
        return;
    }
    if (row == RT_NULL || lv_obj_get_child_count(row) == 0U)
        return;
    name_label = lv_obj_get_child(row, 0U);
    if (name_label == RT_NULL || !lv_obj_check_type(name_label, &lv_label_class))
        return;
    name = lv_label_get_text(name_label);
    if (ft_storage_join_path(s_files_current_path, name, path,
                             sizeof(path)) != RT_EOK)
    {
        feathertalk_ui_alert(ft_preferences_text("文件", "Files"),
                            ft_preferences_text("路径过长，无法打开。",
                                                "The path is too long to open."));
        return;
    }

    if (lv_event_get_user_data(event) == &s_files_directory_marker)
    {
        rt_strncpy(s_files_current_path, path,
                   sizeof(s_files_current_path) - 1U);
        s_files_current_path[sizeof(s_files_current_path) - 1U] = '\0';
        files_refresh_view(false);
    }
    else
    {
        files_preview_file(path, name);
    }
}

static bool files_add_entry(const ft_storage_entry_t *entry, void *context)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    ft_files_list_context_t *counts = context;
    lv_obj_t *row;
    lv_obj_t *name;
    lv_obj_t *detail;
    char detail_text[28];

    if (entry == RT_NULL || counts == RT_NULL || s_files_list == RT_NULL ||
        !lv_obj_is_valid(s_files_list))
        return false;

    row = lv_button_create(s_files_list);
    lv_obj_set_size(row, lv_pct(100), layout->list_row_height);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x181818), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, files_entry_clicked_cb, LV_EVENT_CLICKED,
                        entry->type == FT_STORAGE_ENTRY_DIRECTORY ?
                        &s_files_directory_marker : RT_NULL);
    lv_obj_add_event_cb(row, files_entry_long_pressed_cb,
                        LV_EVENT_LONG_PRESSED,
                        entry->type == FT_STORAGE_ENTRY_DIRECTORY ?
                        &s_files_directory_marker : RT_NULL);

    name = lv_label_create(row);
    lv_label_set_text(name, entry->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 0);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_style_text_font(name, ft_layout_font(16), LV_PART_MAIN);

    detail = lv_label_create(row);
    if (entry->type == FT_STORAGE_ENTRY_DIRECTORY)
    {
        lv_label_set_text(detail, ft_preferences_text("文件夹", "Folder"));
        counts->directories++;
    }
    else
    {
        files_format_bytes(entry->size_bytes, detail_text, sizeof(detail_text));
        lv_label_set_text(detail, detail_text);
        counts->files++;
    }
    lv_obj_set_style_text_color(detail, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_set_style_text_font(detail, ft_layout_font(12), LV_PART_MAIN);

    return true;
}

static void files_set_path_label(void)
{
    char text[FT_STORAGE_PATH_MAX + 24U];
    const char *mount_path;
    const char *title;
    const char *relative;

    if (s_files_path_label == RT_NULL || !lv_obj_is_valid(s_files_path_label))
        return;
    if (strcmp(s_files_current_path, FT_STORAGE_BROWSE_ROOT) == 0)
    {
        lv_label_set_text(s_files_path_label,
                          ft_preferences_text("存储设备", "Storage devices"));
        return;
    }
    if (strncmp(s_files_current_path, FT_STORAGE_FLASH_MOUNT_PATH,
                strlen(FT_STORAGE_FLASH_MOUNT_PATH)) == 0)
    {
        mount_path = FT_STORAGE_FLASH_MOUNT_PATH;
        title = ft_preferences_text("内置 Flash", "Internal Flash");
    }
    else
    {
        mount_path = FT_STORAGE_SD_MOUNT_PATH;
        title = ft_preferences_text("SD 卡", "SD card");
    }
    relative = s_files_current_path + strlen(mount_path);
    if (relative[0] == '\0')
        lv_label_set_text(s_files_path_label, title);
    else
    {
        lv_snprintf(text, sizeof(text), "%s  >  %s", title,
                    relative[0] == '/' ? relative + 1 : relative);
        lv_label_set_text(s_files_path_label, text);
    }
}

static void files_add_empty_message(const char *message)
{
    lv_obj_t *label;
    if (s_files_list == RT_NULL || !lv_obj_is_valid(s_files_list)) return;
    label = lv_label_create(s_files_list);
    lv_label_set_text(label, message);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, ft_layout_px(12), LV_PART_MAIN);
}

static void files_refresh_view(bool manual_refresh)
{
    ft_storage_volume_info_t volume;
    ft_storage_volume_info_t flash_volume;
    ft_storage_volume_info_t sd_volume;
    ft_files_list_context_t counts = {0U, 0U};
    char total_text[24];
    char free_text[24];
    char status[192];
    int directories;
    int files;

    if (manual_refresh) s_files_refresh_count++;
    if (s_files_list == RT_NULL || !lv_obj_is_valid(s_files_list) ||
        s_files_status_label == RT_NULL || !lv_obj_is_valid(s_files_status_label))
        return;

    s_files_last_flash_mounted =
        ft_storage_get_volume(FT_STORAGE_FLASH_MOUNT_PATH,
                              &flash_volume) == RT_EOK && flash_volume.mounted;
    s_files_last_sd_mounted =
        ft_storage_get_volume(FT_STORAGE_SD_MOUNT_PATH,
                              &sd_volume) == RT_EOK && sd_volume.mounted;
    lv_obj_clean(s_files_list);
    s_files_suppress_click_row = RT_NULL;

    if (strcmp(s_files_current_path, FT_STORAGE_BROWSE_ROOT) == 0)
    {
        directories = ft_storage_list(FT_STORAGE_BROWSE_ROOT,
                                      FT_STORAGE_ENTRY_DIRECTORY,
                                      files_add_entry, &counts);
        s_files_last_mounted = s_files_last_flash_mounted ||
                               s_files_last_sd_mounted;
        files_set_path_label();
        lv_snprintf(status, sizeof(status), ft_preferences_text(
                    "内置 Flash：%s · SD 卡：%s\n选择一个存储设备查看文件。",
                    "Internal Flash: %s · SD card: %s\nChoose a storage device to browse files."),
                    s_files_last_flash_mounted ?
                        ft_preferences_text("已挂载", "mounted") :
                        ft_preferences_text("不可用", "unavailable"),
                    s_files_last_sd_mounted ?
                        ft_preferences_text("已挂载", "mounted") :
                        ft_preferences_text("未插卡", "not inserted"));
        lv_label_set_text(s_files_status_label, status);
        if (directories <= 0)
            files_add_empty_message(ft_preferences_text("没有可用的存储设备。",
                                                        "No storage devices are available."));
        if (s_files_up_button != RT_NULL && lv_obj_is_valid(s_files_up_button))
            lv_obj_add_state(s_files_up_button, LV_STATE_DISABLED);
        s_files_directory_count = counts.directories;
        s_files_file_count = 0U;
        return;
    }

    if (strncmp(s_files_current_path, FT_STORAGE_FLASH_MOUNT_PATH,
                strlen(FT_STORAGE_FLASH_MOUNT_PATH)) == 0)
        volume = flash_volume;
    else
        volume = sd_volume;
    if (!volume.mounted)
    {
        bool is_sd = strncmp(s_files_current_path, FT_STORAGE_SD_MOUNT_PATH,
                             strlen(FT_STORAGE_SD_MOUNT_PATH)) == 0;
        s_files_last_mounted = false;
        files_set_path_label();
        lv_label_set_text(s_files_status_label, is_sd ?
            ft_preferences_text("SD 卡未插入或未挂载。插卡后系统会自动识别。",
                                "The SD card is absent or not mounted. It will be detected automatically after insertion.") :
            ft_preferences_text("内置 Flash 未挂载，请在存储设置中检查。",
                                "Internal Flash is not mounted. Check Storage settings."));
        files_add_empty_message(is_sd ?
            ft_preferences_text("当前没有 SD 卡介质。", "No SD card media is present.") :
            ft_preferences_text("内置 Flash 暂时不可用。", "Internal Flash is temporarily unavailable."));
        if (s_files_up_button != RT_NULL && lv_obj_is_valid(s_files_up_button))
            lv_obj_remove_state(s_files_up_button, LV_STATE_DISABLED);
        s_files_directory_count = 0U;
        s_files_file_count = 0U;
        return;
    }

    s_files_last_mounted = true;
    directories = ft_storage_list(s_files_current_path,
                                  FT_STORAGE_ENTRY_DIRECTORY,
                                  files_add_entry, &counts);
    files = directories >= 0 ?
            ft_storage_list(s_files_current_path, FT_STORAGE_ENTRY_FILE,
                            files_add_entry, &counts) : -RT_ERROR;
    if (directories < 0 || files < 0)
    {
        if (strcmp(s_files_current_path, FT_STORAGE_BROWSE_ROOT) != 0)
        {
            rt_strncpy(s_files_current_path, FT_STORAGE_BROWSE_ROOT,
                       sizeof(s_files_current_path) - 1U);
            s_files_current_path[sizeof(s_files_current_path) - 1U] = '\0';
            files_refresh_view(false);
            return;
        }
        lv_obj_clean(s_files_list);
        lv_label_set_text(s_files_status_label,
                          ft_preferences_text("存储设备读取失败，请返回后重试。",
                                              "Unable to read the storage device. Go back and retry."));
        files_add_empty_message(ft_preferences_text("目录读取失败。",
                                                    "Directory read failed."));
        return;
    }

    files_set_path_label();
    files_format_bytes(volume.total_bytes, total_text, sizeof(total_text));
    files_format_bytes(volume.free_bytes, free_text, sizeof(free_text));
    lv_snprintf(status, sizeof(status), ft_preferences_text(
                "已挂载 · %s · 容量 %s · 可用 %s\n%lu 个文件夹，%lu 个文件",
                "Mounted · %s · %s total · %s free\n%lu folders, %lu files"),
                volume.filesystem, total_text, free_text,
                (unsigned long)counts.directories,
                (unsigned long)counts.files);
    lv_label_set_text(s_files_status_label, status);
    if (counts.directories == 0U && counts.files == 0U)
        files_add_empty_message(ft_preferences_text("这个文件夹是空的。",
                                                    "This folder is empty."));
    if (s_files_up_button != RT_NULL && lv_obj_is_valid(s_files_up_button))
    {
        if (strcmp(s_files_current_path, FT_STORAGE_BROWSE_ROOT) == 0)
            lv_obj_add_state(s_files_up_button, LV_STATE_DISABLED);
        else
            lv_obj_remove_state(s_files_up_button, LV_STATE_DISABLED);
    }
    s_files_directory_count = counts.directories;
    s_files_file_count = counts.files;
}

static void files_refresh_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    files_refresh_view(true);
}

static void files_up_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if (ft_storage_parent_path(s_files_current_path, FT_STORAGE_BROWSE_ROOT))
        files_refresh_view(false);
}

static void files_monitor_cb(lv_timer_t *timer)
{
    ft_storage_volume_info_t flash_volume;
    ft_storage_volume_info_t sd_volume;
    bool flash_mounted;
    bool sd_mounted;
    LV_UNUSED(timer);
    if (s_storage_format_from_files)
    {
        uint8_t format_state = s_storage_format_state;
        settings_storage_refresh();
        if (format_state == FT_STORAGE_FORMAT_SUCCESS ||
            format_state == FT_STORAGE_FORMAT_FAILED)
            files_refresh_view(false);
    }
    flash_mounted = ft_storage_get_volume(FT_STORAGE_FLASH_MOUNT_PATH,
                                          &flash_volume) == RT_EOK &&
                    flash_volume.mounted;
    sd_mounted = ft_storage_get_volume(FT_STORAGE_SD_MOUNT_PATH,
                                       &sd_volume) == RT_EOK &&
                 sd_volume.mounted;
    if (flash_mounted != s_files_last_flash_mounted ||
        sd_mounted != s_files_last_sd_mounted)
        files_refresh_view(false);
}

static void files_page_enter(void)
{
    files_refresh_view(false);
    if (s_files_monitor_timer == RT_NULL)
        s_files_monitor_timer = lv_timer_create(files_monitor_cb, 500U, RT_NULL);
}

static bool files_page_back(void)
{
    if (s_storage_format_from_files && s_storage_confirm_box != RT_NULL &&
        lv_obj_is_valid(s_storage_confirm_box))
    {
        settings_storage_cancel_clicked_cb(RT_NULL);
        return true;
    }
    if (s_files_name_box != RT_NULL && lv_obj_is_valid(s_files_name_box))
    {
        files_close_name_editor();
        return true;
    }
    if (s_files_delete_box != RT_NULL && lv_obj_is_valid(s_files_delete_box))
    {
        files_delete_cancel_cb(RT_NULL);
        return true;
    }
    if (s_files_action_box != RT_NULL && lv_obj_is_valid(s_files_action_box))
    {
        files_close_action_menu();
        return true;
    }
    if (ft_storage_parent_path(s_files_current_path, FT_STORAGE_BROWSE_ROOT))
    {
        files_refresh_view(false);
        return true;
    }
    return false;
}

static void files_page_leave(void)
{
    if (s_storage_format_from_files && s_storage_confirm_box != RT_NULL &&
        lv_obj_is_valid(s_storage_confirm_box))
        settings_storage_cancel_clicked_cb(RT_NULL);
    files_close_action_menu();
    files_close_delete_confirmation();
    files_close_name_editor();
    s_files_delete_path[0] = '\0';
    s_files_delete_name[0] = '\0';
    s_files_suppress_click_row = RT_NULL;
    if (s_files_monitor_timer != RT_NULL)
    {
        lv_timer_delete(s_files_monitor_timer);
        s_files_monitor_timer = RT_NULL;
    }
}

static lv_obj_t *create_files_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *page = create_text_page(parent,
                                      ft_preferences_text("文件", "Files"), FT_ICON_FILES,
                                      ft_preferences_text("内置 Flash 与 SD 卡",
                                                          "Internal Flash and SD card"));
    lv_obj_t *toolbar = lv_obj_create(page);

    rt_strncpy(s_files_current_path,
               s_files_requested_device == FT_STORAGE_DEVICE_FLASH ?
                   FT_STORAGE_FLASH_MOUNT_PATH :
               s_files_requested_device == FT_STORAGE_DEVICE_SD ?
                   FT_STORAGE_SD_MOUNT_PATH : FT_STORAGE_BROWSE_ROOT,
               sizeof(s_files_current_path) - 1U);
    s_files_current_path[sizeof(s_files_current_path) - 1U] = '\0';
    s_files_requested_device = FT_STORAGE_DEVICE_INVALID;
    s_files_last_mounted = false;
    s_files_last_flash_mounted = false;
    s_files_last_sd_mounted = false;

    track_object(&s_files_path_label, lv_label_create(page));
    lv_obj_set_width(s_files_path_label, lv_pct(100));
    lv_label_set_long_mode(s_files_path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_files_path_label, ft_layout_font(18), LV_PART_MAIN);
    ft_ui_register_accent(s_files_path_label, FT_ACCENT_TEXT);

    track_object(&s_files_status_label, lv_label_create(page));
    lv_obj_set_width(s_files_status_label, lv_pct(100));
    lv_label_set_long_mode(s_files_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_files_status_label, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_files_status_label, ft_layout_font(12), LV_PART_MAIN);

    style_layout_container(toolbar);
    lv_obj_set_size(toolbar, lv_pct(100), layout->control_height);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(toolbar, layout->section_gap, LV_PART_MAIN);
    track_object(&s_files_up_button,
                 create_flat_button(toolbar,
                                    ft_preferences_text("上一级", "Up"),
                                    files_up_cb, RT_NULL));
    lv_obj_set_width(s_files_up_button, lv_pct(38));
    track_object(&s_files_refresh_button,
                 create_icon_button(toolbar, FT_ICON_REFRESH,
                                    ft_preferences_text("刷新", "Refresh"),
                                    files_refresh_cb, RT_NULL,
                                    RT_NULL, RT_NULL));
    lv_obj_set_width(s_files_refresh_button, lv_pct(58));

    track_object(&s_files_list, lv_obj_create(page));
    style_layout_container(s_files_list);
    lv_obj_set_width(s_files_list, lv_pct(100));
    lv_obj_set_height(s_files_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_files_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_files_list, ft_layout_px(3), LV_PART_MAIN);
    lv_obj_add_event_cb(s_files_list, files_list_long_pressed_cb,
                        LV_EVENT_LONG_PRESSED, RT_NULL);
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

bool ft_pages_media_cover_ready(void)
{
    size_t i;
    if (s_media_cover_flow == RT_NULL || !lv_obj_is_valid(s_media_cover_flow) ||
        s_media_cover_state != FT_MEDIA_COVER_IDLE ||
        s_media_cover_visual_offset != 0 || s_media_cover_visual_dirty ||
        s_media_cover_cell_width <= 0 || s_media_cover_max_size <= 0 ||
        s_media_cover_min_width <= 0 || s_media_cover_min_height <= 0 ||
        s_media_cover_tracks[FT_MEDIA_FLOW_CENTER] !=
        media_track_wrap(s_media_track)) return false;
    lv_obj_update_layout(s_media_cover_flow);
    for (i = 0U; i < FT_MEDIA_FLOW_CELL_COUNT; i++)
        if (s_media_cover_cells[i] == RT_NULL ||
            s_media_cover_cards[i] == RT_NULL ||
            s_media_cover_titles[i] == RT_NULL ||
            !lv_obj_is_valid(s_media_cover_cells[i]) ||
            !lv_obj_is_valid(s_media_cover_cards[i]) ||
            !lv_obj_is_valid(s_media_cover_titles[i])) return false;
    return lv_obj_get_width(s_media_cover_cards[FT_MEDIA_FLOW_CENTER]) ==
               s_media_cover_max_size &&
           lv_obj_get_width(s_media_cover_cards[FT_MEDIA_FLOW_CENTER - 1U]) <
               s_media_cover_max_size &&
           lv_obj_get_width(s_media_cover_cards[FT_MEDIA_FLOW_CENTER + 1U]) <
               s_media_cover_max_size &&
           s_media_cover_perspective[FT_MEDIA_FLOW_CENTER] == 0 &&
           s_media_cover_perspective[FT_MEDIA_FLOW_CENTER - 1U] < 0 &&
           s_media_cover_perspective[FT_MEDIA_FLOW_CENTER + 1U] > 0;
}

static void media_cover_control_timer_cb(lv_timer_t *timer)
{
    if (timer != s_media_cover_control_timer) return;
    if (s_media_cover_flow == RT_NULL || !lv_obj_is_valid(s_media_cover_flow) ||
        ft_router_current_page() != FT_PAGE_MEDIA)
    {
        if (s_media_cover_stress_active)
            rt_kprintf("[UI-MEDIA-STRESS] aborted completed=%lu\n",
                       (unsigned long)s_media_cover_stress_completed);
        s_media_cover_stress_active = false;
        lv_timer_pause(timer);
        return;
    }

    if (s_media_cover_state == FT_MEDIA_COVER_DRAGGING)
    {
        if (s_media_cover_visual_dirty)
        {
            s_media_cover_visual_dirty = false;
            media_cover_refresh_visuals();
        }
        return;
    }

    if (s_media_cover_state == FT_MEDIA_COVER_ANIMATING)
    {
        uint32_t elapsed = lv_tick_elaps(s_media_cover_state_tick);
        uint32_t progress = elapsed >= FT_MEDIA_FLOW_ANIM_MS ?
                            256U : elapsed * 256U / FT_MEDIA_FLOW_ANIM_MS;
        uint32_t remaining = 256U - progress;
        uint32_t eased = 256U - remaining * remaining / 256U;
        int32_t next_offset = s_media_cover_anim_start_offset +
            (int32_t)(((int64_t)(s_media_cover_anim_target_offset -
                                s_media_cover_anim_start_offset) * eased) / 256);
        if (next_offset != s_media_cover_visual_offset ||
            s_media_cover_visual_dirty)
        {
            s_media_cover_visual_offset = next_offset;
            s_media_cover_visual_dirty = false;
            media_cover_refresh_visuals();
        }
        if (progress == 256U)
        {
            s_media_cover_visual_offset = s_media_cover_anim_target_offset;
            s_media_cover_state = FT_MEDIA_COVER_COMMIT_PENDING;
            s_media_cover_state_tick = lv_tick_get();
        }
        return;
    }

    if (s_media_cover_state == FT_MEDIA_COVER_COMMIT_PENDING)
    {
        int32_t offset = s_media_cover_pending_offset;
        s_media_cover_pending_offset = 0;
        if (offset != 0)
            media_cover_commit_offset(offset);
        media_cover_recenter();
        s_media_cover_visual_dirty = false;
        s_media_cover_state = FT_MEDIA_COVER_SETTLING;
        s_media_cover_state_tick = lv_tick_get();
        return;
    }

    if (s_media_cover_state == FT_MEDIA_COVER_SETTLING)
    {
        if (lv_tick_elaps(s_media_cover_state_tick) < FT_MEDIA_FLOW_SETTLE_MS)
            return;
        s_media_cover_state = FT_MEDIA_COVER_IDLE;
        s_media_cover_state_tick = lv_tick_get();
    }

    if (s_media_cover_visual_dirty)
    {
        s_media_cover_visual_dirty = false;
        media_cover_refresh_visuals();
    }

    if (s_media_cover_stress_active)
    {
        if (s_media_cover_stress_remaining != 0U)
        {
            if (media_cover_start_turn(1))
            {
                s_media_cover_stress_remaining--;
                s_media_cover_stress_completed++;
                if ((s_media_cover_stress_completed % 10U) == 0U)
                    rt_kprintf("[UI-MEDIA-STRESS] progress=%lu remaining=%lu\n",
                               (unsigned long)s_media_cover_stress_completed,
                               (unsigned long)s_media_cover_stress_remaining);
            }
            return;
        }

        {
            bool ready = ft_pages_media_cover_ready();
            s_media_cover_stress_active = false;
            rt_kprintf("[UI-MEDIA-STRESS] complete steps=%lu track=%ld ready=%d\n",
                       (unsigned long)s_media_cover_stress_completed,
                       (long)s_media_track, ready ? 1 : 0);
        }
    }

    lv_timer_pause(timer);
}

bool ft_pages_media_cover_stress_start(uint32_t steps)
{
    if (steps == 0U || s_media_cover_flow == RT_NULL ||
        !lv_obj_is_valid(s_media_cover_flow) ||
        ft_router_current_page() != FT_PAGE_MEDIA ||
        s_media_cover_control_timer == RT_NULL) return false;
    s_media_cover_stress_active = true;
    s_media_cover_stress_remaining = steps;
    s_media_cover_stress_completed = 0U;
    media_cover_wake_controller();
    lv_timer_ready(s_media_cover_control_timer);
    return true;
}

bool ft_pages_benchmark_set_keyboard_visible(bool visible)
{
    ft_page_id_t page = ft_router_current_page();

    if (page == FT_PAGE_SEARCH && s_search_keyboard_tray != RT_NULL &&
        lv_obj_is_valid(s_search_keyboard_tray))
    {
        search_keyboard_set_visible(visible);
        return true;
    }
    if (page == FT_PAGE_SETTINGS && s_settings_keyboard_tray != RT_NULL &&
        lv_obj_is_valid(s_settings_keyboard_tray))
    {
        settings_keyboard_set_visible(visible);
        return true;
    }
    return false;
}

bool ft_pages_benchmark_set_media_playing(bool playing)
{
    if (ft_router_current_page() != FT_PAGE_MEDIA || s_media_button == RT_NULL ||
        !lv_obj_is_valid(s_media_button)) return false;
    if (s_media_playing != playing)
        (void)lv_obj_send_event(s_media_button, LV_EVENT_CLICKED, RT_NULL);
    return s_media_playing == playing;
}

bool ft_pages_benchmark_open_media_folder(void)
{
    if (ft_router_current_page() != FT_PAGE_MEDIA ||
        s_media_directory_label == RT_NULL ||
        !lv_obj_is_valid(s_media_directory_label)) return false;
    media_folder_open_cb(RT_NULL);
    return s_media_folder_box != RT_NULL && lv_obj_is_valid(s_media_folder_box);
}

bool ft_pages_benchmark_open_file_action(void)
{
    const ft_ui_preferences_t *preferences;
    const char *path;
    const char *name;

    if (ft_router_current_page() != FT_PAGE_FILES) return false;
    preferences = ft_preferences_get();
    path = preferences->wallpaper_path[0] != '\0' ?
           preferences->wallpaper_path : FT_STORAGE_FLASH_MOUNT_PATH "/Pictures/02.jpg";
    name = strrchr(path, '/');
    name = name != RT_NULL && name[1] != '\0' ? name + 1 : path;
    files_show_action_menu(name, path, false, false);
    return s_files_action_box != RT_NULL && lv_obj_is_valid(s_files_action_box);
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
    if (FT_ICON_WALLPAPER >= FT_ICON_COUNT || used[FT_ICON_WALLPAPER])
        return false;
    used[FT_ICON_WALLPAPER] = true;
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
lv_obj_t *ft_pages_test_get_audio_output_slider(void)
{ return s_audio_output_slider; }
lv_obj_t *ft_pages_test_get_audio_input_slider(void)
{ return s_audio_input_slider; }
lv_obj_t *ft_pages_test_get_audio_rate_button(size_t index)
{ return index < FT_AUDIO_RATE_COUNT ? s_audio_rate_buttons[index] : RT_NULL; }
lv_obj_t *ft_pages_test_get_audio_bits_button(size_t index)
{ return index < FT_AUDIO_BITS_COUNT ? s_audio_bits_buttons[index] : RT_NULL; }
lv_obj_t *ft_pages_test_get_audio_channel_button(size_t index)
{ return index < FT_AUDIO_CHANNEL_COUNT ? s_audio_channel_buttons[index] : RT_NULL; }
bool ft_pages_test_audio_state_valid(void)
{
    ft_audio_status_t status;
    size_t i;

    if (!tracked_object_is_type(&s_audio_output_slider, &lv_slider_class) ||
        !tracked_object_is_type(&s_audio_output_value, &lv_label_class) ||
        !tracked_object_is_type(&s_audio_output_status, &lv_label_class) ||
        !tracked_object_is_type(&s_audio_output_details, &lv_label_class) ||
        !tracked_object_is_type(&s_audio_input_slider, &lv_slider_class) ||
        !tracked_object_is_type(&s_audio_input_value, &lv_label_class) ||
        !tracked_object_is_type(&s_audio_input_status, &lv_label_class) ||
        !tracked_object_is_type(&s_audio_input_details, &lv_label_class) ||
        !tracked_object_is_type(&s_audio_analog_status, &lv_label_class) ||
        ft_audio_get_status(&status) != RT_EOK)
        return false;
    for (i = 0U; i < FT_AUDIO_RATE_COUNT; i++)
        if (!tracked_object_is_type(&s_audio_rate_buttons[i],
                                    &lv_button_class)) return false;
    for (i = 0U; i < FT_AUDIO_BITS_COUNT; i++)
        if (!tracked_object_is_type(&s_audio_bits_buttons[i],
                                    &lv_button_class)) return false;
    for (i = 0U; i < FT_AUDIO_CHANNEL_COUNT; i++)
        if (!tracked_object_is_type(&s_audio_channel_buttons[i],
                                    &lv_button_class)) return false;
    return status.output_registered && status.input_registered &&
           status.output_ready && status.input_ready &&
           !status.analog_input_supported &&
           !lv_obj_has_state(s_audio_output_slider, LV_STATE_DISABLED) &&
           !lv_obj_has_state(s_audio_input_slider, LV_STATE_DISABLED) &&
           lv_slider_get_min_value(s_audio_output_slider) == 0 &&
           lv_slider_get_max_value(s_audio_output_slider) == 100 &&
           lv_slider_get_min_value(s_audio_input_slider) == 0 &&
           lv_slider_get_max_value(s_audio_input_slider) == 75;
}
lv_obj_t *ft_pages_test_get_usb_role_button(ft_usb_role_t role)
{ return role <= FT_USB_ROLE_HOST ? s_usb_role_buttons[role] : RT_NULL; }
lv_obj_t *ft_pages_test_get_usb_function_button(ft_usb_function_t function)
{
    if (function == FT_USB_FUNCTION_STORAGE) return s_usb_function_buttons[0];
    if (function == FT_USB_FUNCTION_AUDIO) return s_usb_function_buttons[1];
    return RT_NULL;
}
lv_obj_t *ft_pages_test_get_usb_stop_button(void) { return s_usb_stop_button; }
bool ft_pages_test_usb_state_valid(void)
{
    ft_usb_status_t status;
    size_t i;
    if (s_usb_monitor_timer == RT_NULL ||
        s_usb_role_buttons[FT_USB_ROLE_DEVICE] == RT_NULL ||
        s_usb_role_buttons[FT_USB_ROLE_HOST] == RT_NULL ||
        s_usb_function_buttons[0] == RT_NULL ||
        s_usb_function_buttons[1] == RT_NULL ||
        s_usb_output_device_buttons[0] == RT_NULL ||
        s_usb_input_device_buttons[0] == RT_NULL ||
        s_usb_input_device_buttons[1] == RT_NULL ||
        s_usb_stop_button == RT_NULL || s_usb_status_label == RT_NULL)
        return false;
    for (i = 0U; i < FT_AUDIO_RATE_COUNT; i++)
        if (s_usb_output_rate_buttons[i] == RT_NULL ||
            s_usb_input_rate_buttons[i] == RT_NULL) return false;
    for (i = 0U; i < FT_AUDIO_BITS_COUNT; i++)
        if (s_usb_output_bits_buttons[i] == RT_NULL ||
            s_usb_input_bits_buttons[i] == RT_NULL) return false;
    for (i = 0U; i < FT_AUDIO_CHANNEL_COUNT; i++)
        if (s_usb_output_channel_buttons[i] == RT_NULL ||
            s_usb_input_channel_buttons[i] == RT_NULL) return false;
    ft_usb_get_status(&status);
    return !lv_obj_has_state(s_usb_role_buttons[FT_USB_ROLE_DEVICE], LV_STATE_DISABLED) &&
           lv_obj_has_state(s_usb_role_buttons[FT_USB_ROLE_HOST], LV_STATE_DISABLED) &&
           lv_obj_has_state(s_usb_function_buttons[1], LV_STATE_DISABLED) ==
               !status.audio_supported &&
           lv_obj_has_state(s_usb_function_buttons[0], LV_STATE_DISABLED) ==
               (!status.sd_present && !status.active) &&
           lv_obj_has_state(s_usb_input_rate_buttons[0], LV_STATE_DISABLED) &&
           lv_obj_has_state(s_usb_input_bits_buttons[0], LV_STATE_DISABLED) &&
           lv_obj_has_state(s_usb_input_channel_buttons[1], LV_STATE_DISABLED) &&
           lv_obj_has_state(s_usb_stop_button, LV_STATE_DISABLED) == !status.active;
}
lv_obj_t *ft_pages_test_get_storage_format_button(void)
{ return s_storage_format_button; }
lv_obj_t *ft_pages_test_get_storage_browse_button(void)
{ return s_storage_browse_button; }
lv_obj_t *ft_pages_test_get_storage_device_button(size_t index)
{
    return index < FT_STORAGE_DEVICE_COUNT ?
           s_storage_device_buttons[index] : RT_NULL;
}
size_t ft_pages_test_storage_device_count(void)
{ return FT_STORAGE_DEVICE_COUNT; }
size_t ft_pages_test_storage_selected_device(void)
{ return (size_t)s_storage_selected_device; }
size_t ft_pages_test_storage_action_target(void)
{
    return s_storage_format_target < FT_STORAGE_DEVICE_INVALID ?
           (size_t)s_storage_format_target : FT_STORAGE_DEVICE_COUNT;
}
lv_obj_t *ft_pages_test_get_storage_capacity_track(void)
{ return s_storage_capacity_track; }
lv_obj_t *ft_pages_test_get_storage_confirm_cancel(void)
{ return s_storage_confirm_cancel; }
lv_obj_t *ft_pages_test_get_storage_confirm_continue(void)
{ return s_storage_confirm_continue; }
uint8_t ft_pages_test_storage_confirm_stage(void)
{ return s_storage_confirm_stage; }
bool ft_pages_test_storage_confirm_fonts(void)
{
    lv_obj_t *buttons[] =
    {
        s_storage_confirm_cancel, s_storage_confirm_continue,
    };
    size_t index;
    for (index = 0U; index < sizeof(buttons) / sizeof(buttons[0]); index++)
    {
        lv_obj_t *label;
        if (buttons[index] == RT_NULL || !lv_obj_is_valid(buttons[index]) ||
            lv_obj_get_child_count(buttons[index]) == 0U)
            return false;
        label = lv_obj_get_child(buttons[index], 0U);
        if (label == RT_NULL || !lv_obj_check_type(label, &lv_label_class) ||
            lv_obj_get_style_text_font(label, LV_PART_MAIN) != ft_layout_font(14))
            return false;
    }
    return true;
}
bool ft_pages_test_storage_visual_valid(void)
{
    ft_storage_device_info_t info;
    lv_area_t track;
    lv_area_t fill;
    uint64_t total;
    uint64_t used;
    uint32_t expected_percent = 0U;
    int32_t expected_width;
    int32_t actual_width;
    int result;

    if (s_storage_capacity_track == RT_NULL ||
        !lv_obj_is_valid(s_storage_capacity_track) ||
        s_storage_capacity_fill == RT_NULL ||
        !lv_obj_is_valid(s_storage_capacity_fill))
        return false;
    rt_memset(&info, 0, sizeof(info));
    result = settings_storage_get_info(s_storage_selected_device, &info);
    lv_obj_update_layout(s_storage_capacity_track);
    lv_obj_get_coords(s_storage_capacity_track, &track);
    lv_obj_get_coords(s_storage_capacity_fill, &fill);
    if (track.x2 < track.x1 || track.y2 < track.y1 ||
        fill.x1 < track.x1 || fill.y1 < track.y1 ||
        fill.x2 > track.x2 || fill.y2 > track.y2)
        return false;
    total = result == RT_EOK && info.mounted ?
            info.volume_total_bytes : 0U;
    used = total >= info.volume_free_bytes ?
           total - info.volume_free_bytes : 0U;
    if (total > 0U)
    {
        expected_percent = used >= total ? 100U :
                           (uint32_t)((used * 100U) / total);
        if (used > 0U && expected_percent == 0U) expected_percent = 1U;
    }
    expected_width = lv_obj_get_content_width(s_storage_capacity_track) *
                     (int32_t)expected_percent / 100;
    actual_width = lv_obj_get_width(s_storage_capacity_fill);
    return actual_width >= expected_width - 2 &&
           actual_width <= expected_width + 2;
}
bool ft_pages_test_storage_state_valid(void)
{
    ft_storage_device_info_t info;
    int result;
    size_t index;
    if (s_storage_selected_device >= FT_STORAGE_DEVICE_INVALID ||
        s_storage_detail_title == RT_NULL ||
        !lv_obj_is_valid(s_storage_detail_title) ||
        s_storage_detail_state == RT_NULL ||
        !lv_obj_is_valid(s_storage_detail_state) ||
        s_storage_capacity_total == RT_NULL ||
        !lv_obj_is_valid(s_storage_capacity_total) ||
        s_storage_used_label == RT_NULL ||
        !lv_obj_is_valid(s_storage_used_label) ||
        s_storage_free_label == RT_NULL ||
        !lv_obj_is_valid(s_storage_free_label) ||
        s_storage_volume_label == RT_NULL ||
        !lv_obj_is_valid(s_storage_volume_label) ||
        s_storage_browse_button == RT_NULL ||
        !lv_obj_is_valid(s_storage_browse_button) ||
        s_storage_format_button == RT_NULL ||
        !lv_obj_is_valid(s_storage_format_button) ||
        s_storage_monitor_timer == RT_NULL ||
        lv_label_get_text(s_storage_detail_title)[0] == '\0' ||
        lv_label_get_text(s_storage_capacity_total)[0] == '\0' ||
        !ft_pages_test_storage_visual_valid())
        return false;
    for (index = 0U; index < FT_STORAGE_DEVICE_COUNT; index++)
    {
        if (s_storage_device_buttons[index] == RT_NULL ||
            !lv_obj_is_valid(s_storage_device_buttons[index]) ||
            s_storage_device_icons[index] == RT_NULL ||
            !lv_obj_is_valid(s_storage_device_icons[index]) ||
            s_storage_device_capacity[index] == RT_NULL ||
            !lv_obj_is_valid(s_storage_device_capacity[index]) ||
            s_storage_device_state[index] == RT_NULL ||
            !lv_obj_is_valid(s_storage_device_state[index]))
            return false;
    }
    result = settings_storage_get_info(s_storage_selected_device, &info);
    return lv_obj_has_state(s_storage_format_button, LV_STATE_DISABLED) ==
               (result != RT_EOK || !info.can_format ||
                s_storage_format_state == FT_STORAGE_FORMAT_RUNNING) &&
           lv_obj_has_state(s_storage_browse_button, LV_STATE_DISABLED) ==
               (result != RT_EOK || !info.mounted || info.usb_exported ||
                s_storage_format_state == FT_STORAGE_FORMAT_RUNNING);
}
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
lv_obj_t *ft_pages_test_get_media_directory_label(void)
{
    return s_media_directory_label;
}
lv_obj_t *ft_pages_test_get_media_loop_button(void)
{
    return s_media_loop_button;
}
const char *ft_pages_test_get_media_label(void)
{ return s_media_label != RT_NULL && lv_obj_is_valid(s_media_label) ? lv_label_get_text(s_media_label) : RT_NULL; }
bool ft_pages_test_media_is_playing(void) { return s_media_playing; }
int32_t ft_pages_test_media_track(void) { return s_media_track; }
size_t ft_pages_test_media_track_count(void) { return media_track_count(); }
int32_t ft_pages_test_media_volume(void)
{ return s_media_volume != RT_NULL && lv_obj_is_valid(s_media_volume) ? lv_slider_get_value(s_media_volume) : -1; }
bool ft_pages_test_media_cover_ready(void)
{
    return ft_pages_media_cover_ready();
}
lv_obj_t *ft_pages_test_get_files_refresh_button(void) { return s_files_refresh_button; }
lv_obj_t *ft_pages_test_get_files_up_button(void) { return s_files_up_button; }
lv_obj_t *ft_pages_test_get_files_first_entry(void)
{
    uint32_t index;
    uint32_t count;
    lv_obj_t *sd_entry = RT_NULL;
    if (s_files_list == RT_NULL || !lv_obj_is_valid(s_files_list) ||
        lv_obj_get_child_count(s_files_list) == 0U)
        return RT_NULL;
    count = lv_obj_get_child_count(s_files_list);
    for (index = 0U; index < count; index++)
    {
        lv_obj_t *row = lv_obj_get_child(s_files_list, index);
        lv_obj_t *label;
        const char *name;
        if (row == RT_NULL || lv_obj_get_child_count(row) == 0U) continue;
        label = lv_obj_get_child(row, 0U);
        if (label == RT_NULL || !lv_obj_check_type(label, &lv_label_class)) continue;
        name = lv_label_get_text(label);
        if (name != RT_NULL && strcmp(name, "flash") == 0) return row;
        if (name != RT_NULL && strcmp(name, "sdcard") == 0) sd_entry = row;
    }
    return sd_entry;
}
lv_obj_t *ft_pages_test_get_files_first_content_entry(void)
{
    if (s_files_list == RT_NULL || !lv_obj_is_valid(s_files_list) ||
        lv_obj_get_child_count(s_files_list) == 0U)
        return RT_NULL;
    return lv_obj_get_child(s_files_list, 0U);
}
lv_obj_t *ft_pages_test_get_files_first_directory_entry(void)
{
    if (s_files_directory_count == 0U || s_files_list == RT_NULL ||
        !lv_obj_is_valid(s_files_list) ||
        lv_obj_get_child_count(s_files_list) == 0U)
        return RT_NULL;
    return lv_obj_get_child(s_files_list, 0U);
}
lv_obj_t *ft_pages_test_get_files_list(void) { return s_files_list; }
lv_obj_t *ft_pages_test_get_files_action_delete(void)
{ return s_files_action_delete; }
lv_obj_t *ft_pages_test_get_files_action_cancel(void)
{ return s_files_action_cancel; }
lv_obj_t *ft_pages_test_get_files_action_paste(void)
{ return s_files_action_paste; }
lv_obj_t *ft_pages_test_get_files_action_rename(void)
{ return s_files_action_rename; }
lv_obj_t *ft_pages_test_get_files_action_new_folder(void)
{ return s_files_action_new_folder; }
lv_obj_t *ft_pages_test_get_files_delete_cancel(void)
{ return s_files_delete_cancel; }
lv_obj_t *ft_pages_test_get_files_name_cancel(void)
{ return s_files_name_cancel; }
bool ft_pages_test_files_action_visible(void)
{
    return s_files_action_box != RT_NULL && lv_obj_is_valid(s_files_action_box) &&
           s_files_action_quick != RT_NULL && lv_obj_is_valid(s_files_action_quick) &&
           s_files_action_copy != RT_NULL && lv_obj_is_valid(s_files_action_copy) &&
           s_files_action_cut != RT_NULL && lv_obj_is_valid(s_files_action_cut) &&
           s_files_action_rename != RT_NULL && lv_obj_is_valid(s_files_action_rename) &&
           s_files_action_new_folder != RT_NULL && lv_obj_is_valid(s_files_action_new_folder) &&
           s_files_action_refresh != RT_NULL && lv_obj_is_valid(s_files_action_refresh) &&
           s_files_action_paste != RT_NULL && lv_obj_is_valid(s_files_action_paste) &&
           s_files_action_delete != RT_NULL && lv_obj_is_valid(s_files_action_delete) &&
           s_files_action_cancel != RT_NULL && lv_obj_is_valid(s_files_action_cancel) &&
           s_files_context_path[0] != '\0';
}
bool ft_pages_test_files_action_fonts(void)
{
    lv_obj_t *buttons[] =
    {
        s_files_action_view, s_files_action_copy, s_files_action_cut,
        s_files_action_rename, s_files_action_new_folder,
        s_files_action_refresh,
        s_files_action_paste, s_files_action_delete, s_files_action_cancel,
    };
    size_t index;
    const lv_font_t *expected = ft_layout_font(14);
    for (index = 0U; index < sizeof(buttons) / sizeof(buttons[0]); index++)
    {
        lv_obj_t *label;
        if (buttons[index] == RT_NULL || !lv_obj_is_valid(buttons[index]) ||
            lv_obj_get_child_count(buttons[index]) == 0U)
            return false;
        label = lv_obj_get_child(buttons[index], 0U);
        if (label == RT_NULL || !lv_obj_check_type(label, &lv_label_class) ||
            lv_obj_get_style_text_font(label, LV_PART_MAIN) != expected)
            return false;
    }
    return true;
}
bool ft_pages_test_files_action_layout(void)
{
    lv_obj_t *all[] =
    {
        s_files_action_cut, s_files_action_copy, s_files_action_rename,
        s_files_action_delete, s_files_action_view, s_files_action_refresh,
        s_files_action_new_folder, s_files_action_paste, s_files_action_cancel,
    };
    lv_area_t box;
    lv_area_t cut;
    lv_area_t copy;
    lv_area_t rename;
    lv_area_t delete_area;
    lv_area_t previous;
    size_t index;
    bool have_previous = false;

    if (!ft_pages_test_files_action_visible()) return false;
    lv_obj_update_layout(s_files_action_box);
    lv_obj_get_coords(s_files_action_box, &box);
    for (index = 0U; index < sizeof(all) / sizeof(all[0]); index++)
    {
        lv_area_t area;
        if (lv_obj_has_flag(all[index], LV_OBJ_FLAG_HIDDEN)) continue;
        lv_obj_get_coords(all[index], &area);
        if (area.x1 < box.x1 || area.x2 > box.x2 ||
            area.y1 < box.y1 || area.y2 > box.y2)
            return false;
    }
    if (!s_files_context_current_folder)
    {
        if (lv_obj_has_flag(s_files_action_quick, LV_OBJ_FLAG_HIDDEN) ||
            !lv_obj_has_flag(s_files_action_refresh, LV_OBJ_FLAG_HIDDEN))
            return false;
        lv_obj_get_coords(s_files_action_cut, &cut);
        lv_obj_get_coords(s_files_action_copy, &copy);
        lv_obj_get_coords(s_files_action_rename, &rename);
        lv_obj_get_coords(s_files_action_delete, &delete_area);
        if (cut.y1 != copy.y1 || cut.y1 != rename.y1 ||
            cut.y1 != delete_area.y1 || cut.x1 >= copy.x1 ||
            copy.x1 >= rename.x1 || rename.x1 >= delete_area.x1)
            return false;
        lv_obj_get_coords(s_files_action_view, &previous);
        return previous.y1 > cut.y2;
    }
    if (!lv_obj_has_flag(s_files_action_quick, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(s_files_action_view, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(s_files_action_refresh, LV_OBJ_FLAG_HIDDEN))
        return false;
    for (index = 5U; index < sizeof(all) / sizeof(all[0]); index++)
    {
        lv_area_t area;
        if (lv_obj_has_flag(all[index], LV_OBJ_FLAG_HIDDEN)) continue;
        lv_obj_get_coords(all[index], &area);
        if (have_previous && area.y1 <= previous.y2) return false;
        previous = area;
        have_previous = true;
    }
    return have_previous;
}
bool ft_pages_test_files_context_is_directory(void)
{ return s_files_context_is_directory; }
bool ft_pages_test_files_name_editor_visible(bool rename_item)
{
    return s_files_name_box != RT_NULL && lv_obj_is_valid(s_files_name_box) &&
           s_files_name_textarea != RT_NULL && lv_obj_is_valid(s_files_name_textarea) &&
           s_files_name_keyboard != RT_NULL && lv_obj_is_valid(s_files_name_keyboard) &&
           s_files_name_cancel != RT_NULL && lv_obj_is_valid(s_files_name_cancel) &&
           s_files_name_confirm != RT_NULL && lv_obj_is_valid(s_files_name_confirm) &&
           s_files_name_target[0] != '\0' &&
           s_files_name_is_rename == rename_item;
}
bool ft_pages_test_files_rows_have_no_permanent_actions(void)
{
    uint32_t index;
    if (s_files_list == RT_NULL || !lv_obj_is_valid(s_files_list)) return false;
    for (index = 0U; index < lv_obj_get_child_count(s_files_list); index++)
    {
        lv_obj_t *row = lv_obj_get_child(s_files_list, index);
        if (row != RT_NULL && lv_obj_check_type(row, &lv_button_class) &&
            lv_obj_get_child_count(row) > 2U)
            return false;
    }
    return true;
}
bool ft_pages_test_files_delete_confirmation_visible(void)
{
    return s_files_delete_box != RT_NULL && lv_obj_is_valid(s_files_delete_box) &&
           s_files_delete_cancel != RT_NULL && lv_obj_is_valid(s_files_delete_cancel) &&
           s_files_delete_confirm != RT_NULL && lv_obj_is_valid(s_files_delete_confirm) &&
           s_files_delete_path[0] != '\0';
}
uint32_t ft_pages_test_files_refresh_count(void) { return s_files_refresh_count; }
bool ft_pages_test_files_browser_ready(void)
{
    return s_files_path_label != RT_NULL && lv_obj_is_valid(s_files_path_label) &&
           s_files_status_label != RT_NULL && lv_obj_is_valid(s_files_status_label) &&
           s_files_up_button != RT_NULL && lv_obj_is_valid(s_files_up_button) &&
           s_files_refresh_button != RT_NULL && lv_obj_is_valid(s_files_refresh_button) &&
           s_files_list != RT_NULL && lv_obj_is_valid(s_files_list) &&
           s_files_monitor_timer != RT_NULL;
}
bool ft_pages_test_files_at_root(void)
{
    return strcmp(s_files_current_path, FT_STORAGE_BROWSE_ROOT) == 0;
}
bool ft_pages_test_files_mounted(void) { return s_files_last_mounted; }
size_t ft_pages_test_files_entry_count(void)
{
    return s_files_directory_count + s_files_file_count;
}
bool ft_pages_test_transient_slots_clear(void)
{
    size_t i;
    if (s_system_status_label != RT_NULL || s_system_metrics_label != RT_NULL ||
        s_search_box != RT_NULL || s_search_keyboard_tray != RT_NULL ||
        s_search_keyboard != RT_NULL || s_search_keyboard_hide != RT_NULL ||
        s_settings_search_box != RT_NULL || s_settings_keyboard_tray != RT_NULL ||
        s_settings_keyboard != RT_NULL || s_settings_keyboard_hide != RT_NULL ||
        s_settings_brightness_slider != RT_NULL || s_settings_brightness_value != RT_NULL ||
        s_audio_output_slider != RT_NULL || s_audio_output_value != RT_NULL ||
        s_audio_output_status != RT_NULL || s_audio_output_details != RT_NULL ||
        s_audio_input_slider != RT_NULL || s_audio_input_value != RT_NULL ||
        s_audio_input_status != RT_NULL || s_audio_input_details != RT_NULL ||
        s_audio_analog_status != RT_NULL ||
        s_settings_radio_status != RT_NULL || s_settings_radio_button != RT_NULL ||
        s_storage_detail_icon != RT_NULL ||
        s_storage_detail_title != RT_NULL ||
        s_storage_detail_state != RT_NULL ||
        s_storage_capacity_caption != RT_NULL ||
        s_storage_capacity_total != RT_NULL ||
        s_storage_capacity_track != RT_NULL ||
        s_storage_capacity_fill != RT_NULL ||
        s_storage_used_label != RT_NULL ||
        s_storage_free_label != RT_NULL ||
        s_storage_volume_label != RT_NULL ||
        s_storage_browse_button != RT_NULL ||
        s_storage_format_button != RT_NULL ||
        s_storage_confirm_box != RT_NULL ||
        s_storage_confirm_cancel != RT_NULL ||
        s_storage_confirm_continue != RT_NULL ||
        s_storage_monitor_timer != RT_NULL ||
        s_usb_stop_button != RT_NULL || s_usb_status_label != RT_NULL ||
        s_usb_output_device_buttons[0] != RT_NULL ||
        s_usb_input_device_buttons[0] != RT_NULL ||
        s_usb_input_device_buttons[1] != RT_NULL ||
        s_usb_monitor_timer != RT_NULL ||
        s_timezone_dropdown != RT_NULL || s_time_preview != RT_NULL ||
        s_media_prev_button != RT_NULL || s_media_button != RT_NULL ||
        s_media_next_button != RT_NULL || s_media_label != RT_NULL ||
        s_media_state_icon != RT_NULL || s_media_track_label != RT_NULL ||
        s_media_artist_label != RT_NULL || s_media_album_label != RT_NULL ||
        s_media_progress_label != RT_NULL || s_media_progress_bar != RT_NULL ||
        s_media_volume != RT_NULL || s_media_cover_flow != RT_NULL ||
        s_media_cover_control_timer != RT_NULL || s_media_monitor_timer != RT_NULL ||
        s_files_refresh_button != RT_NULL ||
        s_files_status_label != RT_NULL || s_files_path_label != RT_NULL ||
        s_files_up_button != RT_NULL || s_files_list != RT_NULL ||
        s_files_action_box != RT_NULL || s_files_action_view != RT_NULL ||
        s_files_action_quick != RT_NULL ||
        s_files_action_copy != RT_NULL || s_files_action_cut != RT_NULL ||
        s_files_action_rename != RT_NULL || s_files_action_new_folder != RT_NULL ||
        s_files_action_refresh != RT_NULL ||
        s_files_action_paste != RT_NULL || s_files_action_delete != RT_NULL ||
        s_files_action_cancel != RT_NULL || s_files_delete_box != RT_NULL ||
        s_files_delete_cancel != RT_NULL || s_files_delete_confirm != RT_NULL ||
        s_files_name_box != RT_NULL || s_files_name_textarea != RT_NULL ||
        s_files_name_error != RT_NULL || s_files_name_keyboard != RT_NULL ||
        s_files_name_cancel != RT_NULL || s_files_name_confirm != RT_NULL ||
        s_files_monitor_timer != RT_NULL ||
        !ft_recorder_page_test_slots_clear()) return false;
    for (i = 0U; i < FT_MEDIA_FLOW_CELL_COUNT; i++)
        if (s_media_cover_cells[i] != RT_NULL ||
            s_media_cover_cards[i] != RT_NULL ||
            s_media_cover_bands[i] != RT_NULL ||
            s_media_cover_shades[i] != RT_NULL ||
            s_media_cover_discs[i] != RT_NULL ||
            s_media_cover_dots[i] != RT_NULL ||
            s_media_cover_titles[i] != RT_NULL) return false;
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
    for (i = 0U; i < FT_AUDIO_RATE_COUNT; i++)
        if (s_audio_rate_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_AUDIO_BITS_COUNT; i++)
        if (s_audio_bits_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_AUDIO_CHANNEL_COUNT; i++)
        if (s_audio_channel_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_TIME_FORMAT_COUNT; i++)
        if (s_time_format_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_LANGUAGE_COUNT; i++)
        if (s_language_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < 2U; i++)
        if (s_usb_role_buttons[i] != RT_NULL ||
            s_usb_function_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_AUDIO_RATE_COUNT; i++)
        if (s_usb_output_rate_buttons[i] != RT_NULL ||
            s_usb_input_rate_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_AUDIO_BITS_COUNT; i++)
        if (s_usb_output_bits_buttons[i] != RT_NULL ||
            s_usb_input_bits_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_AUDIO_CHANNEL_COUNT; i++)
        if (s_usb_output_channel_buttons[i] != RT_NULL ||
            s_usb_input_channel_buttons[i] != RT_NULL) return false;
    for (i = 0U; i < FT_STORAGE_DEVICE_COUNT; i++)
        if (s_storage_device_buttons[i] != RT_NULL ||
            s_storage_device_icons[i] != RT_NULL ||
            s_storage_device_capacity[i] != RT_NULL ||
            s_storage_device_state[i] != RT_NULL) return false;
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
bool ft_pages_test_home_swipe_ready(void)
{
    lv_dir_t direction;
    if (s_home_tileview == RT_NULL || !lv_obj_is_valid(s_home_tileview) ||
        !lv_obj_has_flag(s_home_tileview, LV_OBJ_FLAG_SCROLLABLE))
        return false;
    direction = lv_obj_get_scroll_dir(s_home_tileview);
    return (direction & (LV_DIR_LEFT | LV_DIR_RIGHT)) != 0;
}
size_t ft_pages_test_accent_count(void) { return FT_ACCENT_COUNT; }
uint32_t ft_pages_test_accent_rgb(size_t i) { return i < FT_ACCENT_COUNT ? s_accent_rgb[i] : 0U; }
#endif
