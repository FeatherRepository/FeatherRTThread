#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <rtthread.h>
#include <board.h>
#include <feathertalk/ipc_protocol.h>

#include "drv_lcd.h"
#include "feather_ui.h"
#include "feather_ui_components.h"
#include "feather_ui_icons.h"
#include "feathertalk_audio.h"
#include "feathertalk_recorder.h"
#include "feathertalk_storage.h"
#include "feathertalk_usb.h"
#include "ipc/feathertalk_ipc.h"
#include "feathertalk_ui_notifications.h"
#include "feathertalk_ui_preferences_store.h"
#include "feathertalk_gpu_image.h"
#include "feathertalk_gpu_scene.h"

#define FT_ROUTE_DEPTH    8U
#define FT_FILE_CAPACITY  24U
#define FT_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define FT_PERCENT_MAX                  100U
#define FT_REFERENCE_WIDTH             480U
#define FT_REFERENCE_HEIGHT            800U
#define FT_KEYBOARD_REFERENCE_HEIGHT   322U
#define FT_SHADE_REFERENCE_HEIGHT      492U
#define FT_TILE_COLUMNS                 4U

/* Product design metrics.  They are scaled through the runtime layout before
 * use; hardware/protocol limits below remain absolute by definition. */
#define FT_REFERENCE_STATUS_HEIGHT       36U
#define FT_REFERENCE_NAVIGATION_HEIGHT   60U
#define FT_REFERENCE_MARGIN              18U
#define FT_REFERENCE_ROW_HEIGHT          58U
#define FT_REFERENCE_ROW_GAP              6U
#define FT_REFERENCE_LIST_TOP            76U
#define FT_REFERENCE_TILE_TOP            42U
#define FT_REFERENCE_TILE_HEIGHT        104U
#define FT_REFERENCE_TILE_GAP            10U
#define FT_SHADE_NOTIFICATION_TOP       278U
#define FT_SHADE_NOTIFICATION_STEP       86U
#define FT_QUICK_CARD_COUNT               4U
#define FT_SELECT_MAX_OPTIONS             16U

typedef enum
{
    FT_KEYBOARD_SEARCH = 0,
    FT_KEYBOARD_RENAME,
    FT_KEYBOARD_NEW_FOLDER
} ft_keyboard_mode_t;

typedef enum
{
    FT_AUDIO_ROW_OUTPUT_VOLUME = 0,
    FT_AUDIO_ROW_INPUT_GAIN,
    FT_AUDIO_ROW_SAMPLE_RATE,
    FT_AUDIO_ROW_SAMPLE_DEPTH,
    FT_AUDIO_ROW_CHANNELS,
    FT_AUDIO_ROW_OUTPUT_DEVICE,
    FT_AUDIO_ROW_INPUT_DEVICE,
    FT_AUDIO_ROW_COUNT
} ft_audio_row_t;

typedef enum
{
    FT_USB_ROW_ROLE = 0,
    FT_USB_ROW_STORAGE,
    FT_USB_ROW_AUDIO,
    FT_USB_ROW_OUTPUT_RATE,
    FT_USB_ROW_OUTPUT_DEPTH,
    FT_USB_ROW_OUTPUT_CHANNELS,
    FT_USB_ROW_INPUT_FORMAT,
    FT_USB_ROW_STATUS,
    FT_USB_ROW_COUNT
} ft_usb_row_t;

typedef enum
{
    FT_GALLERY_ACTION_PREVIOUS = 0,
    FT_GALLERY_ACTION_NEXT,
    FT_GALLERY_ACTION_WALLPAPER,
    FT_GALLERY_ACTION_CLOSE,
    FT_GALLERY_ACTION_COUNT
} ft_gallery_action_t;

typedef enum
{
    FT_FILE_ACTION_OPEN = 0,
    FT_FILE_ACTION_CUT,
    FT_FILE_ACTION_COPY,
    FT_FILE_ACTION_PASTE,
    FT_FILE_ACTION_RENAME,
    FT_FILE_ACTION_DELETE,
    FT_FILE_ACTION_NEW_FOLDER,
    FT_FILE_ACTION_COUNT
} ft_file_action_t;

typedef enum
{
    FT_DIALOG_VIEW_CONFIRM = 0,
    FT_DIALOG_VIEW_FINAL_CONFIRM,
    FT_DIALOG_VIEW_MESSAGE
} ft_dialog_view_t;

typedef enum
{
    FT_DIALOG_ACTION_NONE = 0,
    FT_DIALOG_ACTION_FORMAT_FLASH,
    FT_DIALOG_ACTION_FORMAT_SD,
    FT_DIALOG_ACTION_DELETE_FILE,
    FT_DIALOG_ACTION_DELETE_GALLERY,
    FT_DIALOG_ACTION_RECORDER_START,
    FT_DIALOG_ACTION_RECORDER_STOP,
    FT_DIALOG_ACTION_RECORDER_DEVICE,
    FT_DIALOG_ACTION_FILE_PASTE,
    FT_DIALOG_ACTION_FILE_RENAME,
    FT_DIALOG_ACTION_FILE_CREATE
} ft_dialog_action_t;

typedef enum
{
    FT_SELECT_NONE = 0,
    FT_SELECT_AUDIO_RATE,
    FT_SELECT_AUDIO_DEPTH,
    FT_SELECT_AUDIO_CHANNELS,
    FT_SELECT_USB_RATE,
    FT_SELECT_USB_DEPTH,
    FT_SELECT_USB_CHANNELS,
    FT_SELECT_TIMEZONE,
    FT_SELECT_LANGUAGE,
    FT_SELECT_BACKGROUND
} ft_select_kind_t;

enum
{
    FT_BACKGROUND_BLACK = 0,
    FT_BACKGROUND_DARK,
    FT_BACKGROUND_ACCENT,
    FT_BACKGROUND_WALLPAPER
};

#define FT_DEFAULT_TIMEZONE_MINUTES       (8 * 60)
#define FT_DEFAULT_AUDIO_VOLUME_PERCENT   70U
#define FT_DEFAULT_AUDIO_GAIN_PERCENT     53U

#define FT_MIN_SURFACE_WIDTH             240U
#define FT_MIN_SURFACE_HEIGHT            320U
#define FT_MAX_SURFACE_WIDTH            1200U
#define FT_MAX_SURFACE_HEIGHT           1600U

#define FT_ANIMATION_SCALE_BASE         1000
#define FT_TILE_PULSE_SCALE              970
#define FT_TILE_PULSE_DURATION_MS        420U
#define FT_TILE_SETTLE_MIN_MS            120U
#define FT_TILE_SETTLE_MAX_MS            280U
#define FT_SHADE_SETTLE_MIN_MS           120U
#define FT_SHADE_SETTLE_MAX_MS           260U

typedef enum
{
    FT_ANIMATION_TILE_X = 1,
    FT_ANIMATION_TILE_Y,
    FT_ANIMATION_TILE_WIDTH,
    FT_ANIMATION_TILE_HEIGHT,
    FT_ANIMATION_TILE_SCALE,
    FT_ANIMATION_SHADE_Y
} ft_animation_property_t;

typedef struct
{
    int16_t screen_width;
    int16_t screen_height;
    int16_t status_height;
    int16_t navigation_y;
    int16_t navigation_height;
    int16_t content_height;
    int16_t margin;
    int16_t row_width;
    int16_t row_height;
    int16_t row_gap;
    int16_t list_top;
    int16_t shade_height;
    int16_t tile_top;
    int16_t tile_cell_width;
    int16_t tile_height;
    int16_t tile_gap;
    int16_t keyboard_y;
    int16_t keyboard_height;
    int16_t touch_slop;
    int16_t horizontal_swipe;
    int16_t shade_release_distance;
} ft_scene_layout_t;

typedef struct
{
    fui_option_t options[FT_SELECT_MAX_OPTIONS];
    char labels[FT_SELECT_MAX_OPTIONS][28];
    uint8_t count;
    uint8_t selected;
    const char *title;
    fui_select_popup_t popup;
} ft_select_model_t;

typedef struct
{
    fui_context_menu_t menu;
    fui_context_menu_item_t items[FT_FILE_ACTION_COUNT];
} ft_file_menu_model_t;

static ft_scene_layout_t s_layout;

#define FT_SCREEN_W       (s_layout.screen_width)
#define FT_SCREEN_H       (s_layout.screen_height)
#define FT_STATUS_H       (s_layout.status_height)
#define FT_NAV_Y          (s_layout.navigation_y)
#define FT_NAV_H          (s_layout.navigation_height)
#define FT_CONTENT_Y      (s_layout.status_height)
#define FT_CONTENT_H      (s_layout.content_height)
#define FT_ROW_X          (s_layout.margin)
#define FT_ROW_W          (s_layout.row_width)
#define FT_ROW_H          (s_layout.row_height)
#define FT_ROW_GAP        (s_layout.row_gap)
#define FT_LIST_TOP       (s_layout.list_top)
#define FT_SHADE_H        (s_layout.shade_height)

#define C_BG       FUI_RGB(0x0d, 0x0d, 0x0d)
#define C_PANEL    FUI_RGB(0x21, 0x21, 0x21)
#define C_PANEL_2  FUI_RGB(0x2d, 0x2d, 0x2d)
#define C_TEXT     FUI_RGB(0xf4, 0xf4, 0xf4)
#define C_MUTED    FUI_RGB(0xa8, 0xa8, 0xa8)
#define C_ACCENT   FUI_RGB(0x00, 0x78, 0xd7)
#define C_OK       FUI_RGB(0x10, 0x7c, 0x41)
#define C_WARN     FUI_RGB(0xd8, 0x3b, 0x01)
#define C_OFF      FUI_RGB(0x3b, 0x3b, 0x3b)

typedef struct
{
    const char *name_en;
    const char *name_zh;
    ft_gpu_page_t page;
    fui_icon_id_t icon;
    fui_color_t color;
    uint8_t grid_column;
    uint8_t grid_row;
    uint8_t column_span;
    uint8_t row_span;
    fui_rect_t rect;
} ft_app_t;

typedef struct
{
    ft_gpu_page_t route[FT_ROUTE_DEPTH];
    uint8_t depth;
    int16_t scroll_y;
    int16_t scroll_limit;
    int16_t touch_down_x;
    int16_t touch_down_y;
    int16_t touch_last_x;
    int16_t touch_last_y;
    bool scrolling;
    bool desktop_edit;
    int8_t selected_tile;
    bool tile_drag;
    uint8_t tile_resize_corner;
    int16_t tile_pointer_x;
    int16_t tile_pointer_y;
    fui_rect_t tile_pointer_rect;
    int16_t tile_scale;
    int16_t shade_y;
    int16_t shade_target_y;
    int16_t shade_drag_offset;
    bool shade_visible;
    bool shade_drag;
    bool shade_pointer_down;
    uint32_t shade_drag_start_ms;
    uint8_t brightness;
    bool brightness_valid;
    uint32_t fps;
    uint32_t last_stats_ms;
    uint32_t last_frame_count;
    uint32_t last_service_ms;
    uint32_t notification_revision;
    feathertalk_quick_status_t quick;
    bool quick_valid;
    ft_preferences_store_payload_t prefs;
    ft_audio_status_t audio;
    ft_recorder_status_t recorder;
    ft_recorder_device_info_t recorder_devices[FT_RECORDER_DEVICE_COUNT];
    size_t recorder_device_count;
    ft_usb_status_t usb;
    ft_storage_device_info_t flash;
    ft_storage_device_info_t sd;
    uint8_t storage_selected;
    char file_path[FT_STORAGE_PATH_MAX];
    ft_storage_entry_t files[FT_FILE_CAPACITY];
    uint8_t file_count;
    int8_t file_selected;
    bool file_menu;
    bool root_format_menu;
    bool gallery_viewer;
    bool dialog_visible;
    bool dialog_success;
    ft_dialog_view_t dialog_view;
    ft_dialog_action_t dialog_action;
    int dialog_result;
    char dialog_target[FT_STORAGE_PATH_MAX];
    bool toast_visible;
    uint32_t toast_until_ms;
    char toast_message[96];
    bool keyboard_visible;
    ft_keyboard_mode_t keyboard_mode;
    uint8_t keyboard_length;
    char keyboard_input[32];
    char search_text[32];
    ft_select_kind_t select_kind;
    char clipboard_path[FT_STORAGE_PATH_MAX];
    bool clipboard_move;
    uint8_t media_state;
    uint8_t media_track;
    uint8_t media_volume;
    uint8_t test_pass;
    uint8_t test_fail;
} ft_scene_t;

static ft_scene_t s;

static void gallery_set_source(bool sd);
static void gallery_restore_wallpaper(void);
static int16_t tile_handle_size(void);
static void preferences_save(void);

static ft_app_t s_apps[] =
{
    {"SETTINGS", "设置", FT_GPU_PAGE_SETTINGS, FUI_ICON_APP_SETTINGS,
        C_ACCENT, 0, 0, 2, 1, {0}},
    {"FILES", "文件", FT_GPU_PAGE_FILES, FUI_ICON_APP_FILES,
        C_OK, 2, 0, 1, 1, {0}},
    {"GALLERY", "相册", FT_GPU_PAGE_GALLERY, FUI_ICON_APP_GALLERY,
        FUI_RGB(0x88, 0x17, 0x98),
        3, 0, 1, 2, {0}},
    {"AUDIO", "声音", FT_GPU_PAGE_SETTINGS_AUDIO, FUI_ICON_APP_AUDIO,
        C_WARN, 0, 1, 1, 1, {0}},
    {"RECORDER", "录音", FT_GPU_PAGE_RECORDER, FUI_ICON_APP_RECORDER,
        FUI_RGB(0x00, 0x99, 0xbc),
        1, 1, 2, 1, {0}},
    {"SYSTEM", "系统", FT_GPU_PAGE_SYSTEM, FUI_ICON_APP_SYSTEM,
        FUI_RGB(0x49, 0x8d, 0x00),
        0, 2, 2, 1, {0}},
    {"USB / SD", "USB / SD", FT_GPU_PAGE_SETTINGS_USB, FUI_ICON_APP_USB_SD,
        FUI_RGB(0x51, 0x55, 0x9b),
        2, 2, 2, 1, {0}},
    {"WI-FI", "WI-FI", FT_GPU_PAGE_SETTINGS_WIFI, FUI_ICON_APP_WIFI,
        C_ACCENT, 0, 3, 1, 1, {0}},
    {"BLUETOOTH", "蓝牙", FT_GPU_PAGE_SETTINGS_BLUETOOTH,
        FUI_ICON_APP_BLUETOOTH, C_ACCENT,
        1, 3, 1, 1, {0}},
    {"MEDIA", "媒体", FT_GPU_PAGE_MEDIA, FUI_ICON_APP_MEDIA,
        FUI_RGB(0xa2, 0x00, 0x25),
        2, 3, 2, 1, {0}}
};

#define FT_APP_COUNT ((uint8_t)FT_ARRAY_COUNT(s_apps))

static uint8_t default_tile_row_count(void)
{
    size_t i;
    uint8_t rows = 0U;
    for (i = 0U; i < FT_APP_COUNT; i++)
    {
        uint8_t bottom = (uint8_t)(s_apps[i].grid_row + s_apps[i].row_span);
        if (bottom > rows) rows = bottom;
    }
    return rows == 0U ? 1U : rows;
}

typedef struct
{
    const char *name_en;
    const char *name_zh;
} ft_media_track_t;

static const ft_media_track_t s_media_tracks[] =
{
    {"FEATHER THEME", "Feather 主题曲"},
    {"EDGE SIGNAL", "边缘信号"},
    {"NIGHT SKY", "夜空"}
};

#define FT_MEDIA_TRACK_COUNT ((uint8_t)FT_ARRAY_COUNT(s_media_tracks))

static const uint32_t s_accent_palette[] =
{
    0x0078D7U, 0x0099BCU, 0x107C41U, 0x881798U, 0xD83B01U
};

typedef struct
{
    const char *name_en;
    const char *name_zh;
} ft_background_descriptor_t;

static const ft_background_descriptor_t s_backgrounds[] =
{
    {"BLACK", "黑色"},
    {"DARK", "深色"},
    {"ACCENT", "强调色"},
    {"WALLPAPER", "壁纸"}
};

static const int16_t s_timezone_minutes[] =
{
    -720, -480, -300, 0, 60, 180, 330, 480,
    540, 570, 600, 660, 720, 840
};

#define FT_BACKGROUND_COUNT ((uint8_t)FT_ARRAY_COUNT(s_backgrounds))

static const ft_background_descriptor_t s_file_action_labels[] =
{
    {"OPEN", "打开"},
    {"CUT", "剪切"},
    {"COPY", "复制"},
    {"PASTE HERE", "粘贴到此处"},
    {"RENAME", "重命名"},
    {"DELETE", "删除"},
    {"NEW FOLDER", "新建文件夹"}
};

#define FT_ACCENT_COUNT ((uint8_t)FT_ARRAY_COUNT(s_accent_palette))
#define FT_TILE_OPACITY_STEP             32U
#define FT_TILE_OPACITY_MIN              96U

static int16_t clamp_i16(int32_t value, int16_t minimum, int16_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return (int16_t)value;
}

static uint8_t value_as_percent(uint8_t value, uint8_t maximum)
{
    if (maximum == 0U) return 0U;
    if (value >= maximum) return FT_PERCENT_MAX;
    return (uint8_t)(((uint32_t)value * FT_PERCENT_MAX + maximum / 2U) /
                     maximum);
}

static uint8_t percent_as_value(uint8_t percent, uint8_t maximum)
{
    if (percent >= FT_PERCENT_MAX) return maximum;
    return (uint8_t)(((uint32_t)percent * maximum + FT_PERCENT_MAX / 2U) /
                     FT_PERCENT_MAX);
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static uint32_t motion_duration_ms(int32_t distance, int32_t reference,
                                   uint32_t minimum, uint32_t maximum)
{
    uint32_t duration;
    if (reference <= 0 || distance <= 0) return minimum;
    if (distance > reference) distance = reference;
    duration = minimum + (uint32_t)(((uint64_t)distance *
               (maximum - minimum)) / (uint32_t)reference);
    return duration > maximum ? maximum : duration;
}

static void scene_animation_apply(void *target, uint16_t property,
                                  int32_t value)
{
    if (property == FT_ANIMATION_TILE_SCALE)
    {
        s.tile_scale = (int16_t)value;
        return;
    }
    if (property == FT_ANIMATION_SHADE_Y)
    {
        s.shade_y = (int16_t)value;
        return;
    }
    if (target != RT_NULL)
    {
        ft_app_t *tile = (ft_app_t *)target;
        switch (property)
        {
        case FT_ANIMATION_TILE_X: tile->rect.x = (int16_t)value; break;
        case FT_ANIMATION_TILE_Y: tile->rect.y = (int16_t)value; break;
        case FT_ANIMATION_TILE_WIDTH: tile->rect.width = (int16_t)value; break;
        case FT_ANIMATION_TILE_HEIGHT: tile->rect.height = (int16_t)value; break;
        default: break;
        }
    }
}

static void scene_animation_complete(void *target, uint16_t property)
{
    (void)target;
    if (property == FT_ANIMATION_SHADE_Y && s.shade_y <= -FT_SHADE_H)
        s.shade_visible = false;
}

static void start_value_animation(void *target, uint16_t property,
                                  int32_t from, int32_t to,
                                  uint32_t duration_ms, fui_easing_t easing,
                                  uint16_t repeat_count, bool autoreverse,
                                  uint32_t now_ms)
{
    fui_animation_spec_t animation;
    if (from == to)
    {
        scene_animation_apply(target, property, to);
        return;
    }
    memset(&animation, 0, sizeof(animation));
    animation.target = target;
    animation.property = property;
    animation.from = from;
    animation.to = to;
    animation.duration_ms = duration_ms;
    animation.easing = easing;
    animation.repeat_count = repeat_count;
    animation.autoreverse = autoreverse;
    animation.apply = scene_animation_apply;
    animation.complete = scene_animation_complete;
    if (fui_animation_start(&animation, now_ms) != 0)
        scene_animation_apply(target, property, to);
}

static void start_tile_pulse(uint32_t now_ms)
{
    s.tile_scale = FT_ANIMATION_SCALE_BASE;
    start_value_animation(&s, FT_ANIMATION_TILE_SCALE,
                          FT_ANIMATION_SCALE_BASE, FT_TILE_PULSE_SCALE,
                          FT_TILE_PULSE_DURATION_MS,
                          FUI_EASING_IN_OUT_CUBIC,
                          FUI_ANIMATION_REPEAT_FOREVER, true, now_ms);
}

static void stop_tile_pulse(void)
{
    fui_animation_cancel(&s, FT_ANIMATION_TILE_SCALE);
    s.tile_scale = FT_ANIMATION_SCALE_BASE;
}

static void start_shade_settle(int16_t target, uint32_t now_ms)
{
    int32_t distance = abs_i32((int32_t)target - s.shade_y);
    uint32_t duration = motion_duration_ms(distance, FT_SHADE_H,
                                            FT_SHADE_SETTLE_MIN_MS,
                                            FT_SHADE_SETTLE_MAX_MS);
    s.shade_target_y = target;
    if (target > -FT_SHADE_H) s.shade_visible = true;
    start_value_animation(&s, FT_ANIMATION_SHADE_Y, s.shade_y, target,
                          duration, FUI_EASING_OUT_CUBIC, 0U, false, now_ms);
}

static void scene_layout_init(uint16_t width, uint16_t height)
{
    int16_t content_width;
    s_layout.screen_width = (int16_t)width;
    s_layout.screen_height = (int16_t)height;
    s_layout.status_height = clamp_i16(((int32_t)height * FT_REFERENCE_STATUS_HEIGHT +
        FT_REFERENCE_HEIGHT / 2) / FT_REFERENCE_HEIGHT, 28, 48);
    s_layout.navigation_height = clamp_i16(((int32_t)height * FT_REFERENCE_NAVIGATION_HEIGHT +
        FT_REFERENCE_HEIGHT / 2) / FT_REFERENCE_HEIGHT, 48, 80);
    s_layout.navigation_y = (int16_t)(height - s_layout.navigation_height);
    s_layout.content_height = (int16_t)(s_layout.navigation_y - s_layout.status_height);
    s_layout.margin = clamp_i16(((int32_t)width * FT_REFERENCE_MARGIN +
        FT_REFERENCE_WIDTH / 2) / FT_REFERENCE_WIDTH, 10, 32);
    s_layout.row_width = (int16_t)(width - s_layout.margin * 2);
    s_layout.row_height = clamp_i16(((int32_t)height * FT_REFERENCE_ROW_HEIGHT +
        FT_REFERENCE_HEIGHT / 2) / FT_REFERENCE_HEIGHT, 48, 72);
    s_layout.row_gap = clamp_i16(((int32_t)height * FT_REFERENCE_ROW_GAP +
        FT_REFERENCE_HEIGHT / 2) / FT_REFERENCE_HEIGHT, 4, 10);
    s_layout.list_top = (int16_t)(s_layout.status_height +
        clamp_i16(((int32_t)height * FT_REFERENCE_LIST_TOP + FT_REFERENCE_HEIGHT / 2) /
                  FT_REFERENCE_HEIGHT, 62, 96));
    s_layout.shade_height = clamp_i16(((int32_t)height * FT_SHADE_REFERENCE_HEIGHT +
                                       FT_REFERENCE_HEIGHT / 2) /
                                      FT_REFERENCE_HEIGHT, 220, 720);
    if (s_layout.shade_height > s_layout.content_height)
        s_layout.shade_height = s_layout.content_height;
    s_layout.tile_gap = clamp_i16(((int32_t)width * FT_REFERENCE_TILE_GAP +
        FT_REFERENCE_WIDTH / 2) / FT_REFERENCE_WIDTH, 6, 14);
    content_width = (int16_t)(width - s_layout.margin * 2);
    s_layout.tile_top = (int16_t)(s_layout.status_height +
        clamp_i16(((int32_t)height * FT_REFERENCE_TILE_TOP + FT_REFERENCE_HEIGHT / 2) /
                  FT_REFERENCE_HEIGHT, 30, 56));
    s_layout.tile_cell_width = (int16_t)((content_width + s_layout.tile_gap) /
                                          FT_TILE_COLUMNS);
    s_layout.tile_height = clamp_i16(((int32_t)height * FT_REFERENCE_TILE_HEIGHT +
        FT_REFERENCE_HEIGHT / 2) / FT_REFERENCE_HEIGHT, 42, 132);
    {
        uint8_t default_rows = default_tile_row_count();
        int16_t fitted_tile_height = (int16_t)((s_layout.navigation_y - s_layout.margin -
            s_layout.tile_top - (default_rows - 1U) * s_layout.tile_gap) /
            default_rows);
        if (s_layout.tile_height > fitted_tile_height)
            s_layout.tile_height = fitted_tile_height;
        if (s_layout.tile_height < 42) s_layout.tile_height = 42;
    }
    s_layout.keyboard_height = clamp_i16(((int32_t)height *
        FT_KEYBOARD_REFERENCE_HEIGHT + FT_REFERENCE_HEIGHT / 2) /
        FT_REFERENCE_HEIGHT,
                                         220, s_layout.content_height);
    s_layout.keyboard_y = (int16_t)(s_layout.navigation_y -
                                     s_layout.keyboard_height);
    s_layout.touch_slop = clamp_i16((int32_t)width / 60, 5, 12);
    s_layout.horizontal_swipe = clamp_i16((int32_t)width / 8, 40, 100);
    s_layout.shade_release_distance =
        clamp_i16((int32_t)height * 7 / 80, 36, 110);
}

static void scene_tiles_reset_geometry(void)
{
    size_t i;
    for (i = 0U; i < FT_APP_COUNT; i++)
    {
        const ft_app_t *descriptor = &s_apps[i];
        s_apps[i].rect.x = (int16_t)(s_layout.margin +
            descriptor->grid_column * s_layout.tile_cell_width);
        s_apps[i].rect.y = (int16_t)(s_layout.tile_top +
            descriptor->grid_row * (s_layout.tile_height + s_layout.tile_gap));
        s_apps[i].rect.width = (int16_t)(descriptor->column_span *
            s_layout.tile_cell_width - s_layout.tile_gap);
        s_apps[i].rect.height = (int16_t)(descriptor->row_span *
            s_layout.tile_height + (descriptor->row_span - 1U) *
            s_layout.tile_gap);
    }
}

typedef struct
{
    const char *name_en;
    const char *name_zh;
    const char *detail_en;
    const char *detail_zh;
    ft_gpu_page_t page;
    fui_icon_id_t icon;
} ft_setting_descriptor_t;

static const ft_setting_descriptor_t s_settings[] =
{
    {"DISPLAY", "显示", "BRIGHTNESS AND SCREEN", "亮度和屏幕方向",
        FT_GPU_PAGE_SETTINGS_DISPLAY, FUI_ICON_SETTING_DISPLAY},
    {"AUDIO", "声音", "INPUT OUTPUT FORMAT", "输入输出与音频格式",
        FT_GPU_PAGE_SETTINGS_AUDIO, FUI_ICON_SETTING_AUDIO},
    {"WI-FI", "WI-FI", "RADIO AND CONNECTION", "无线网络和连接",
        FT_GPU_PAGE_SETTINGS_WIFI, FUI_ICON_SETTING_WIFI},
    {"BLUETOOTH", "蓝牙", "RADIO AND DEVICES", "蓝牙和设备",
        FT_GPU_PAGE_SETTINGS_BLUETOOTH, FUI_ICON_SETTING_BLUETOOTH},
    {"STORAGE", "存储", "FLASH SD AND FORMAT", "Flash、SD 卡与格式化",
        FT_GPU_PAGE_SETTINGS_STORAGE, FUI_ICON_SETTING_STORAGE},
    {"USB", "USB", "DEVICE ROLE MSC UAC", "设备模式、MSC 和 UAC",
        FT_GPU_PAGE_SETTINGS_USB, FUI_ICON_SETTING_USB},
    {"TIME & LANGUAGE", "时间和语言", "CLOCK ZONE LANGUAGE", "时钟、时区和语言",
        FT_GPU_PAGE_SETTINGS_TIME_LANGUAGE, FUI_ICON_SETTING_TIME_LANGUAGE},
    {"PERSONALIZATION", "个性化", "COLOR TILES WALLPAPER", "配色、磁贴与壁纸",
        FT_GPU_PAGE_SETTINGS_PERSONALIZATION, FUI_ICON_SETTING_PERSONALIZATION},
    {"SYSTEM INFORMATION", "系统信息", "SOC MEMORY PERIPHERALS", "SoC、存储和外设",
        FT_GPU_PAGE_SYSTEM, FUI_ICON_SETTING_SYSTEM},
    {"ABOUT", "关于", "SOFTWARE AND LICENSES", "软件版本和许可",
        FT_GPU_PAGE_ABOUT, FUI_ICON_SETTING_ABOUT}
};

#define FT_SETTING_COUNT ((uint8_t)FT_ARRAY_COUNT(s_settings))

static const char *tr(const char *english, const char *chinese)
{
    return s.prefs.language == 0U ? chinese : english;
}

static const char *app_name(const ft_app_t *app)
{
    return tr(app->name_en, app->name_zh);
}

static const char *setting_name(uint8_t index)
{
    return tr(s_settings[index].name_en, s_settings[index].name_zh);
}

static const char *setting_detail(uint8_t index)
{
    return tr(s_settings[index].detail_en, s_settings[index].detail_zh);
}

static const char *page_title(ft_gpu_page_t page)
{
    static const char *titles_en[FT_GPU_PAGE_COUNT] =
    {
        "START", "SEARCH", "SYSTEM INFORMATION", "SETTINGS", "MEDIA",
        "RECORDER", "GALLERY", "FILES", "ABOUT", "DISPLAY", "AUDIO",
        "WI-FI", "BLUETOOTH", "STORAGE", "USB", "TIME & LANGUAGE",
        "PERSONALIZATION"
    };
    static const char *titles_zh[FT_GPU_PAGE_COUNT] =
    {
        "首页", "搜索", "系统信息", "设置", "媒体", "录音机", "相册",
        "文件", "关于", "显示", "声音", "WI-FI", "蓝牙", "存储",
        "USB", "时间和语言", "个性化"
    };
    return page < FT_GPU_PAGE_COUNT ?
           tr(titles_en[page], titles_zh[page]) : tr("UNKNOWN", "未知页面");
}

static bool text_contains(const char *text, const char *needle)
{
    size_t i, j;
    if (needle[0] == '\0') return true;
    for (i = 0U; text[i] != '\0'; i++)
    {
        for (j = 0U; needle[j] != '\0' && text[i + j] != '\0'; j++)
            if (toupper((unsigned char)text[i + j]) !=
                toupper((unsigned char)needle[j])) break;
        if (needle[j] == '\0') return true;
    }
    return false;
}

static fui_color_t accent(void)
{
    return FUI_RGB((s.prefs.accent_rgb >> 16) & 0xffU,
                   (s.prefs.accent_rgb >> 8) & 0xffU,
                   s.prefs.accent_rgb & 0xffU);
}

static fui_component_style_t component_style(void)
{
    fui_component_style_t style = {
        .surface = C_PANEL,
        .surface_alt = C_PANEL_2,
        .surface_pressed = FUI_RGB(0x3a, 0x3a, 0x3a),
        .surface_selected = accent(),
        .surface_disabled = FUI_RGB(0x18, 0x18, 0x18),
        .foreground = C_TEXT,
        .foreground_muted = C_MUTED,
        .foreground_disabled = FUI_RGB(0x72, 0x72, 0x72),
        .accent = accent(),
        .danger = C_WARN,
        .track = C_OFF,
        .knob = C_TEXT,
        .outline = C_MUTED,
        .padding = clamp_i16(FT_ROW_H / 5, 8, 14),
        .radius = 5U
    };
    return style;
}

static void select_open(ft_select_kind_t kind)
{
    s.select_kind = kind;
    s.keyboard_visible = false;
    s.file_menu = false;
}

static void select_close(void)
{
    s.select_kind = FT_SELECT_NONE;
}

static void select_add_option(ft_select_model_t *model, const char *label,
                              bool enabled, bool selected)
{
    uint8_t index;
    if (model == RT_NULL || model->count >= FT_SELECT_MAX_OPTIONS) return;
    index = model->count++;
    snprintf(model->labels[index], sizeof(model->labels[index]), "%s", label);
    model->options[index].label = model->labels[index];
    model->options[index].state = enabled ? FUI_COMPONENT_STATE_DEFAULT :
                                           FUI_COMPONENT_STATE_DISABLED;
    if (selected) model->selected = index;
}

static void select_add_u32(ft_select_model_t *model, uint32_t value,
                           const char *suffix, bool enabled, bool selected)
{
    char label[28];
    snprintf(label, sizeof(label), "%lu %s", (unsigned long)value, suffix);
    select_add_option(model, label, enabled, selected);
}

static bool select_model_build(ft_select_model_t *model)
{
    uint8_t i;
    int16_t row_height;
    int16_t height;
    if (model == RT_NULL || s.select_kind == FT_SELECT_NONE) return false;
    memset(model, 0, sizeof(*model));
    switch (s.select_kind)
    {
    case FT_SELECT_AUDIO_RATE:
        model->title = tr("SAMPLE RATE", "采样率");
        for (i = 0U; i < s.audio.output_sample_rate_count; i++)
        {
            uint32_t value = s.audio.output_sample_rates[i];
            select_add_u32(model, value, "HZ",
                ft_audio_output_format_supported(value,
                    s.audio.output_sample_bits, s.audio.output_channels),
                value == s.audio.output_sample_rate);
        }
        break;
    case FT_SELECT_AUDIO_DEPTH:
        model->title = tr("SAMPLE DEPTH", "采样深度");
        for (i = 0U; i < s.audio.output_sample_bits_count; i++)
        {
            uint8_t value = s.audio.output_sample_bits_supported[i];
            select_add_u32(model, value, "BIT",
                ft_audio_output_format_supported(s.audio.output_sample_rate,
                    value, s.audio.output_channels),
                value == s.audio.output_sample_bits);
        }
        break;
    case FT_SELECT_AUDIO_CHANNELS:
        model->title = tr("CHANNELS", "声道数");
        for (i = 0U; i < s.audio.output_channel_count; i++)
        {
            uint8_t value = s.audio.output_channels_supported[i];
            select_add_u32(model, value, "CH",
                ft_audio_output_format_supported(s.audio.output_sample_rate,
                    s.audio.output_sample_bits, value),
                value == s.audio.output_channels);
        }
        break;
    case FT_SELECT_USB_RATE:
        model->title = tr("UAC OUTPUT RATE", "UAC 输出采样率");
        for (i = 0U; i < s.audio.output_sample_rate_count; i++)
        {
            uint32_t value = s.audio.output_sample_rates[i];
            select_add_u32(model, value, "HZ",
                ft_usb_uac_output_supported(value,
                    s.usb.uac_output_sample_bits, s.usb.uac_output_channels),
                value == s.usb.uac_output_sample_rate);
        }
        break;
    case FT_SELECT_USB_DEPTH:
        model->title = tr("UAC OUTPUT DEPTH", "UAC 输出采样深度");
        for (i = 0U; i < s.audio.output_sample_bits_count; i++)
        {
            uint8_t value = s.audio.output_sample_bits_supported[i];
            select_add_u32(model, value, "BIT",
                ft_usb_uac_output_supported(s.usb.uac_output_sample_rate,
                    value, s.usb.uac_output_channels),
                value == s.usb.uac_output_sample_bits);
        }
        break;
    case FT_SELECT_USB_CHANNELS:
        model->title = tr("UAC OUTPUT CHANNELS", "UAC 输出声道数");
        for (i = 0U; i < s.audio.output_channel_count; i++)
        {
            uint8_t value = s.audio.output_channels_supported[i];
            select_add_u32(model, value, "CH",
                ft_usb_uac_output_supported(s.usb.uac_output_sample_rate,
                    s.usb.uac_output_sample_bits, value),
                value == s.usb.uac_output_channels);
        }
        break;
    case FT_SELECT_TIMEZONE:
        model->title = tr("TIME ZONE", "时区");
        for (i = 0U; i < FT_ARRAY_COUNT(s_timezone_minutes); i++)
        {
            int minutes = s_timezone_minutes[i];
            char zone[28];
            snprintf(zone, sizeof(zone), "UTC%c%02d:%02d",
                     minutes < 0 ? '-' : '+',
                     (minutes < 0 ? -minutes : minutes) / 60,
                     (minutes < 0 ? -minutes : minutes) % 60);
            select_add_option(model, zone, true,
                              minutes == s.prefs.timezone_offset_minutes);
        }
        break;
    case FT_SELECT_LANGUAGE:
        model->title = tr("LANGUAGE", "语言");
        select_add_option(model, "简体中文", true, s.prefs.language == 0U);
        select_add_option(model, "ENGLISH", true, s.prefs.language == 1U);
        break;
    case FT_SELECT_BACKGROUND:
        model->title = tr("BACKGROUND", "背景");
        for (i = 0U; i < FT_BACKGROUND_COUNT; i++)
            select_add_option(model,
                tr(s_backgrounds[i].name_en, s_backgrounds[i].name_zh),
                i != FT_BACKGROUND_WALLPAPER || s.prefs.wallpaper_path[0] != '\0',
                i == s.prefs.background);
        break;
    default: break;
    }
    if (model->count == 0U) return false;
    row_height = clamp_i16(FT_ROW_H * 3 / 4, 34, 46);
    height = (int16_t)((model->count + 1U) * row_height);
    if (height > FT_CONTENT_H - s_layout.margin * 2)
    {
        row_height = (int16_t)((FT_CONTENT_H - s_layout.margin * 2) /
                               (model->count + 1U));
        height = (int16_t)((model->count + 1U) * row_height);
    }
    model->popup.bounds = (fui_rect_t){FT_ROW_X,
        (int16_t)(FT_CONTENT_Y + (FT_CONTENT_H - height) / 2), FT_ROW_W, height};
    model->popup.title = model->title;
    model->popup.options = model->options;
    model->popup.option_count = model->count;
    model->popup.selected_index = model->selected;
    model->popup.state = FUI_COMPONENT_STATE_DEFAULT;
    model->popup.text_scale = row_height >= 40 ? 2U : 1U;
    model->popup.row_height = row_height;
    return true;
}

static bool select_apply(uint8_t index)
{
    int result = RT_EOK;
    switch (s.select_kind)
    {
    case FT_SELECT_AUDIO_RATE:
        result = ft_audio_set_output_format(s.audio.output_sample_rates[index],
            s.audio.output_sample_bits, s.audio.output_channels);
        break;
    case FT_SELECT_AUDIO_DEPTH:
        result = ft_audio_set_output_format(s.audio.output_sample_rate,
            s.audio.output_sample_bits_supported[index], s.audio.output_channels);
        break;
    case FT_SELECT_AUDIO_CHANNELS:
        result = ft_audio_set_output_format(s.audio.output_sample_rate,
            s.audio.output_sample_bits, s.audio.output_channels_supported[index]);
        break;
    case FT_SELECT_USB_RATE:
        result = ft_usb_set_uac_output_format(s.audio.output_sample_rates[index],
            s.usb.uac_output_sample_bits, s.usb.uac_output_channels);
        break;
    case FT_SELECT_USB_DEPTH:
        result = ft_usb_set_uac_output_format(s.usb.uac_output_sample_rate,
            s.audio.output_sample_bits_supported[index], s.usb.uac_output_channels);
        break;
    case FT_SELECT_USB_CHANNELS:
        result = ft_usb_set_uac_output_format(s.usb.uac_output_sample_rate,
            s.usb.uac_output_sample_bits, s.audio.output_channels_supported[index]);
        break;
    case FT_SELECT_TIMEZONE:
        if (index >= FT_ARRAY_COUNT(s_timezone_minutes)) return false;
        s.prefs.timezone_offset_minutes = s_timezone_minutes[index];
        preferences_save();
        break;
    case FT_SELECT_LANGUAGE:
        if (index > 1U) return false;
        s.prefs.language = index;
        preferences_save();
        break;
    case FT_SELECT_BACKGROUND:
        if (index >= FT_BACKGROUND_COUNT) return false;
        if (index == FT_BACKGROUND_WALLPAPER && s.prefs.wallpaper_path[0] == '\0')
            return false;
        s.prefs.background = index;
        preferences_save();
        break;
    default: return false;
    }
    if (result != RT_EOK) return false;
    if (s.select_kind >= FT_SELECT_AUDIO_RATE &&
        s.select_kind <= FT_SELECT_AUDIO_CHANNELS)
    {
        (void)ft_audio_get_status(&s.audio);
        s.prefs.audio_output_sample_rate = s.audio.output_sample_rate;
        s.prefs.audio_output_sample_bits = s.audio.output_sample_bits;
        s.prefs.audio_output_channels = s.audio.output_channels;
        preferences_save();
    }
    else if (s.select_kind >= FT_SELECT_USB_RATE &&
             s.select_kind <= FT_SELECT_USB_CHANNELS)
    {
        ft_usb_refresh();
        ft_usb_get_status(&s.usb);
    }
    return true;
}

static void draw_select_overlay(fui_painter_t *p)
{
    ft_select_model_t model;
    fui_component_style_t style;
    if (!select_model_build(&model)) return;
    style = component_style();
    style.surface = C_PANEL_2;
    style.surface_alt = C_PANEL;
    style.surface_selected = accent();
    (void)fui_painter_rect(p, (fui_rect_t){0, 0, FT_SCREEN_W, FT_SCREEN_H},
                           0U, FUI_ARGB(0xa0, 0, 0, 0));
    (void)fui_component_select_popup(p, &model.popup, &style);
}

static bool select_handle_event(const fui_event_t *event)
{
    ft_select_model_t model;
    int index;
    if (event == RT_NULL || s.select_kind == FT_SELECT_NONE) return false;
    if (event->type != FUI_EVENT_TAP) return true;
    if (!select_model_build(&model))
    {
        select_close();
        return true;
    }
    index = fui_component_select_index_from_point(&model.popup,
                                                   event->x, event->y);
    if (index >= 0 && (uint8_t)index < model.count &&
        (model.options[index].state & FUI_COMPONENT_STATE_DISABLED) == 0U)
        (void)select_apply((uint8_t)index);
    select_close();
    return true;
}

static int16_t row_y(uint8_t index)
{
    return (int16_t)(FT_LIST_TOP + index * (FT_ROW_H + FT_ROW_GAP) + s.scroll_y);
}

static void set_scroll_content_rows(uint8_t rows)
{
    int32_t content_bottom;
    int32_t viewport_bottom = FT_NAV_Y - s_layout.margin;
    if (rows == 0U)
    {
        s.scroll_limit = 0;
        return;
    }
    content_bottom = FT_LIST_TOP + rows * (FT_ROW_H + FT_ROW_GAP) - FT_ROW_GAP;
    s.scroll_limit = content_bottom > viewport_bottom ?
        (int16_t)(viewport_bottom - content_bottom) : 0;
}

static fui_rect_t search_box_rect(void)
{
    int16_t width = clamp_i16(FT_ROW_W * 41 / 100,
                              FT_ROW_W / 3, FT_ROW_W * 2 / 3);
    int16_t height = clamp_i16(FT_ROW_H / 2, 28, 40);
    return (fui_rect_t){(int16_t)(FT_SCREEN_W - s_layout.margin - width),
                        s_layout.tile_top, width, height};
}

static void draw_search_field(fui_painter_t *p)
{
    fui_component_style_t style = component_style();
    fui_text_field_t field = {
        .bounds = search_box_rect(),
        .text = s.search_text,
        .placeholder = tr("TAP TO FILTER", "点击搜索"),
        .state = s.keyboard_visible && s.keyboard_mode == FT_KEYBOARD_SEARCH ?
                 FUI_COMPONENT_STATE_FOCUSED : FUI_COMPONENT_STATE_DEFAULT,
        .text_scale = 1U
    };
    style.padding = 10;
    style.radius = 4U;
    (void)fui_component_text_field(p, &field, &style);
}

static int16_t row_meter_y(uint8_t index)
{
    return (int16_t)(row_y(index) + FT_ROW_H -
                     clamp_i16(FT_ROW_H / 3, 16, 22));
}

static fui_rect_t row_switch_rect(uint8_t index)
{
    const int16_t width = 48;
    const int16_t height = 24;
    return (fui_rect_t){(int16_t)(FT_ROW_X + FT_ROW_W - width -
                                  clamp_i16(FT_ROW_H / 5, 8, 14)),
                        (int16_t)(row_y(index) + (FT_ROW_H - height) / 2),
                        width, height};
}

static uint8_t row_index_after_y(int16_t y)
{
    int32_t distance = y - FT_LIST_TOP - s.scroll_y;
    int32_t step = FT_ROW_H + FT_ROW_GAP;
    if (distance <= 0) return 0U;
    return (uint8_t)((distance + step - 1) / step);
}

static fui_rect_t media_panel_rect(void)
{
    fui_rect_t rect;
    rect.x = (int16_t)(s_layout.margin * 2);
    rect.y = (int16_t)(FT_LIST_TOP + 3 * FT_ROW_GAP);
    rect.width = (int16_t)(FT_SCREEN_W - rect.x * 2);
    rect.height = (int16_t)(((int32_t)(FT_NAV_Y - rect.y) * 42) / 100);
    rect.height = clamp_i16(rect.height, FT_ROW_H * 3, FT_CONTENT_H / 2);
    return rect;
}

static fui_rect_t media_controls_rect(void)
{
    fui_rect_t panel = media_panel_rect();
    fui_rect_t controls;
    controls.x = panel.x;
    controls.y = (int16_t)(panel.y + panel.height * 7 / 10);
    controls.width = panel.width;
    controls.height = (int16_t)(panel.y + panel.height - controls.y);
    return controls;
}

static uint8_t media_volume_row(void)
{
    fui_rect_t panel = media_panel_rect();
    return row_index_after_y((int16_t)(panel.y + panel.height + FT_ROW_GAP));
}

static int16_t gallery_source_height(void)
{
    return clamp_i16(FT_ROW_H / 2, 28, 38);
}

static fui_rect_t gallery_source_rect(bool sd)
{
    int16_t height = gallery_source_height();
    int16_t width = clamp_i16(FT_ROW_W / 5, 72, 104);
    int16_t gap = (int16_t)(FT_ROW_GAP + 4);
    int16_t right = (int16_t)(FT_SCREEN_W - s_layout.margin);
    fui_rect_t rect;
    rect.x = sd ? (int16_t)(right - width) :
                  (int16_t)(right - width * 2 - gap);
    rect.y = (int16_t)(FT_STATUS_H + (FT_LIST_TOP - FT_STATUS_H - height) / 2);
    rect.width = width;
    rect.height = height;
    return rect;
}

static fui_rect_t gallery_view_panel_rect(void)
{
    int16_t action_height = clamp_i16(FT_ROW_H * 3 / 4, 30, 54);
    int16_t reserved_height;
    int16_t maximum_height;
    fui_rect_t rect;
    rect.x = (int16_t)(s_layout.margin * 2);
    rect.y = (int16_t)(FT_LIST_TOP + FT_ROW_GAP);
    rect.width = (int16_t)(FT_SCREEN_W - rect.x * 2);
    rect.height = (int16_t)(((int32_t)(FT_NAV_Y - rect.y) * 63) / 100);
    reserved_height = (int16_t)(action_height * 3 + FT_ROW_GAP + 6 +
                                 s_layout.margin);
    maximum_height = (int16_t)(FT_NAV_Y - rect.y - reserved_height);
    if (rect.height > maximum_height) rect.height = maximum_height;
    if (rect.height < 30) rect.height = 30;
    return rect;
}

static fui_rect_t gallery_image_bounds(void)
{
    fui_rect_t rect = gallery_view_panel_rect();
    int16_t inset = clamp_i16(s_layout.margin / 2, 6, 14);
    rect.x = (int16_t)(rect.x + inset);
    rect.y = (int16_t)(rect.y + inset);
    rect.width = (int16_t)(rect.width - inset * 2);
    rect.height = (int16_t)(rect.height - inset * 2 - 18);
    return rect;
}

static int16_t gallery_action_height(void)
{
    return clamp_i16(FT_ROW_H * 3 / 4, 30, 54);
}

static int16_t gallery_action_y(void)
{
    fui_rect_t panel = gallery_view_panel_rect();
    return (int16_t)(panel.y + panel.height + gallery_action_height());
}

static fui_rect_t gallery_action_rect(uint8_t index)
{
    fui_rect_t panel = gallery_view_panel_rect();
    int16_t gap = (int16_t)(FT_ROW_GAP + 2);
    int16_t available = (int16_t)(panel.width -
                                  gap * (FT_GALLERY_ACTION_COUNT - 1U));
    int16_t widths[FT_GALLERY_ACTION_COUNT];
    fui_rect_t rect;
    uint8_t i;
    widths[0] = (int16_t)(available * 24 / 100);
    widths[1] = (int16_t)(available * 24 / 100);
    widths[2] = (int16_t)(available * 33 / 100);
    widths[3] = (int16_t)(available - widths[0] - widths[1] - widths[2]);
    rect.x = panel.x;
    for (i = 0U; i < index && i < FT_GALLERY_ACTION_COUNT; i++)
        rect.x = (int16_t)(rect.x + widths[i] + gap);
    rect.y = gallery_action_y();
    rect.width = widths[index < FT_GALLERY_ACTION_COUNT ?
                        index : FT_GALLERY_ACTION_COUNT - 1U];
    rect.height = gallery_action_height();
    return rect;
}

static fui_rect_t gallery_delete_rect(void)
{
    fui_rect_t panel = gallery_view_panel_rect();
    fui_rect_t rect;
    rect.x = panel.x;
    rect.y = (int16_t)(gallery_action_y() + gallery_action_height() +
                       FT_ROW_GAP + 4);
    rect.width = panel.width;
    rect.height = (int16_t)(gallery_action_height() + 2);
    return rect;
}

static int16_t file_menu_action_height(void)
{
    int16_t desired = clamp_i16(FT_ROW_H * 2 / 3, 28, 44);
    int16_t maximum = (int16_t)((FT_CONTENT_H - s_layout.margin * 2 - 18) / 8);
    if (desired > maximum) desired = maximum;
    return clamp_i16(desired, 24, 44);
}

static fui_rect_t file_menu_rect(bool root_format)
{
    int16_t action_height = file_menu_action_height();
    int16_t action_count = root_format ? 2 : FT_FILE_ACTION_COUNT;
    int16_t header_height = (int16_t)(action_height + (root_format ? 14 : 8));
    fui_rect_t rect;
    rect.width = clamp_i16(FT_ROW_W * 86 / 100, 210,
                           (int16_t)(FT_SCREEN_W - s_layout.margin * 2));
    rect.height = (int16_t)(header_height + action_count * action_height + 10);
    rect.x = (int16_t)((FT_SCREEN_W - rect.width) / 2);
    rect.y = (int16_t)(FT_NAV_Y - s_layout.margin - rect.height);
    if (rect.y < FT_CONTENT_Y + s_layout.margin)
        rect.y = (int16_t)(FT_CONTENT_Y + s_layout.margin);
    return rect;
}

static bool file_menu_model_build(ft_file_menu_model_t *model,
                                  bool root_format)
{
    uint8_t action;
    int16_t row_height = file_menu_action_height();
    if (model == RT_NULL || s.file_selected < 0 ||
        s.file_selected >= s.file_count)
        return false;
    memset(model, 0, sizeof(*model));
    model->menu.bounds = file_menu_rect(root_format);
    model->menu.title = root_format ?
        (s.file_selected == 0 ?
         tr("FORMAT INTERNAL FLASH?", "格式化内部 Flash？") :
         tr("FORMAT SD CARD?", "格式化 SD 卡？")) :
        s.files[s.file_selected].name;
    model->menu.items = model->items;
    model->menu.item_count = root_format ? 2U : FT_FILE_ACTION_COUNT;
    model->menu.state = FUI_COMPONENT_STATE_DEFAULT;
    model->menu.text_scale = 2U;
    model->menu.row_height = row_height;
    model->menu.header_height = (int16_t)(row_height +
                                          (root_format ? 14 : 8));
    model->menu.leading_size = 0;
    if (root_format)
    {
        model->items[0].label = tr("CANCEL", "取消");
        model->items[0].variant = FUI_BUTTON_SECONDARY;
        model->items[1].label = tr("FORMAT - ERASE ALL DATA",
                                   "格式化并清除全部数据");
        model->items[1].variant = FUI_BUTTON_DANGER;
        if ((s.file_selected == 0 &&
             (!s.flash.can_format || s.flash.usb_exported)) ||
            (s.file_selected == 1 &&
             (!s.sd.can_format || s.sd.usb_exported)))
            model->items[1].state = FUI_COMPONENT_STATE_DISABLED;
        return true;
    }
    for (action = 0U; action < FT_FILE_ACTION_COUNT; action++)
    {
        model->items[action].label = tr(s_file_action_labels[action].name_en,
                                        s_file_action_labels[action].name_zh);
        model->items[action].variant = action == FT_FILE_ACTION_DELETE ?
                                       FUI_BUTTON_DANGER :
                                       FUI_BUTTON_SECONDARY;
        if (action == FT_FILE_ACTION_PASTE && s.clipboard_path[0] == '\0')
            model->items[action].state = FUI_COMPONENT_STATE_DISABLED;
    }
    return true;
}

static int16_t keyboard_x(int16_t design_x)
{
    return (int16_t)(((int32_t)design_x * FT_SCREEN_W +
                      FT_REFERENCE_WIDTH / 2) / FT_REFERENCE_WIDTH);
}

static int16_t keyboard_y(int16_t design_offset)
{
    return (int16_t)(s_layout.keyboard_y +
        ((int32_t)design_offset * s_layout.keyboard_height +
         FT_KEYBOARD_REFERENCE_HEIGHT / 2) / FT_KEYBOARD_REFERENCE_HEIGHT);
}

static int16_t keyboard_width(int16_t design_width)
{
    return (int16_t)(((int32_t)design_width * FT_SCREEN_W +
                      FT_REFERENCE_WIDTH / 2) / FT_REFERENCE_WIDTH);
}

static int16_t shade_offset(int16_t design_offset)
{
    return (int16_t)(((int32_t)design_offset * FT_SHADE_H +
                      FT_SHADE_REFERENCE_HEIGHT / 2) /
                     FT_SHADE_REFERENCE_HEIGHT);
}

static uint8_t shade_notification_capacity(void)
{
    int16_t first = shade_offset(FT_SHADE_NOTIFICATION_TOP);
    int16_t step = shade_offset(FT_SHADE_NOTIFICATION_STEP);
    int16_t available = (int16_t)(FT_SHADE_H - s_layout.margin - first);
    if (step <= 0 || available <= 0) return 0U;
    return (uint8_t)(available / step);
}

static void draw_component_icon(fui_painter_t *p, fui_rect_t bounds,
                                fui_color_t color, void *context)
{
    (void)fui_icon_draw(p, (fui_icon_id_t)(uintptr_t)context, bounds, color);
}

static void draw_row(fui_painter_t *p, uint8_t index, fui_icon_id_t icon,
                     const char *title, const char *detail, bool enabled)
{
    int16_t y = row_y(index);
    fui_component_style_t style = component_style();
    fui_list_row_t row;
    if (y + FT_ROW_H < FT_CONTENT_Y || y >= FT_NAV_Y) return;
    memset(&row, 0, sizeof(row));
    row.bounds = (fui_rect_t){FT_ROW_X, y, FT_ROW_W, FT_ROW_H};
    row.title = title;
    row.detail = detail;
    row.state = enabled ? FUI_COMPONENT_STATE_DEFAULT :
                          FUI_COMPONENT_STATE_DISABLED;
    row.title_scale = 2U;
    row.detail_scale = 1U;
    row.show_chevron = true;
    row.leading_size = 24;
    row.leading_draw = draw_component_icon;
    row.leading_context = (void *)(uintptr_t)icon;
    (void)fui_component_list_row(p, &row, &style);
}

static void draw_switch(fui_painter_t *p, int16_t x, int16_t y, bool on,
                        bool enabled)
{
    fui_component_style_t style = component_style();
    fui_switch_t control = {
        .bounds = {x, y, 48, 24},
        .state = enabled ? FUI_COMPONENT_STATE_DEFAULT :
                           FUI_COMPONENT_STATE_DISABLED,
        .checked = on
    };
    (void)fui_component_switch(p, &control, &style);
}

static fui_rect_t meter_rect(int16_t y)
{
    return (fui_rect_t){(int16_t)(FT_ROW_X + 12), (int16_t)(y - 4),
                        (int16_t)(FT_ROW_W - 24), 20};
}

static void draw_meter(fui_painter_t *p, int16_t y, uint8_t value,
                       fui_color_t color)
{
    fui_component_style_t style = component_style();
    fui_slider_t slider = {
        .bounds = meter_rect(y),
        .state = FUI_COMPONENT_STATE_DEFAULT,
        .minimum = 0,
        .maximum = FT_PERCENT_MAX,
        .value = value
    };
    style.accent = color;
    (void)fui_component_slider(p, &slider, &style);
}

static uint8_t meter_value_from_x(int16_t x, uint8_t maximum)
{
    fui_slider_t slider = {
        .bounds = meter_rect(0),
        .state = FUI_COMPONENT_STATE_DEFAULT,
        .minimum = 0,
        .maximum = maximum,
        .value = 0
    };
    return (uint8_t)fui_component_slider_value_from_x(&slider, x);
}

static bool set_brightness(uint8_t value)
{
    uint8_t actual;
    if (!s.brightness_valid) return false;
    if (lcd_backlight_set_percent(value) != RT_EOK) return false;
    if (lcd_backlight_get_percent(&actual) == RT_EOK) s.brightness = actual;
    else s.brightness = value;
    return true;
}

static void draw_header(fui_painter_t *p, const char *subtitle)
{
    fui_painter_text(p, s_layout.margin, FT_STATUS_H + 15, 3, C_TEXT,
                     page_title(s.route[s.depth - 1U]));
    if (subtitle != RT_NULL)
        fui_painter_text(p, s_layout.margin + 2, FT_LIST_TOP - 26, 1, C_MUTED, subtitle);
}

static void draw_status(fui_painter_t *p)
{
    char text[48];
    int16_t text_width;
    int16_t text_x;
    int16_t icon_x;
    int16_t brand_right;
    fui_engine_stats_t stats;
    size_t unread = ft_notifications_unread_count();
    fui_painter_rect(p, (fui_rect_t){0, 0, FT_SCREEN_W, FT_STATUS_H}, 0,
                     FUI_RGB(0x07, 0x07, 0x07));
    fui_painter_text(p, s_layout.margin, (FT_STATUS_H - 16) / 2, 2,
                     accent(), "FEATHER");
    fui_engine_get_stats(&stats);
    snprintf(text, sizeof(text), "%luFPS %luK N%u",
             (unsigned long)s.fps,
             (unsigned long)(stats.gpu_submit_bytes_last / 1024U),
             (unsigned int)unread);
    text_width = fui_component_text_width(text, 1U);
    text_x = (int16_t)(FT_SCREEN_W - s_layout.margin - text_width);
    fui_painter_text(p, text_x, (FT_STATUS_H - 8) / 2, 1, C_TEXT, text);
    icon_x = (int16_t)(text_x - 60);
    brand_right = (int16_t)(s_layout.margin + strlen("FEATHER") * 12U);
    if (icon_x > brand_right + 4)
    {
        (void)fui_icon_draw(p, FUI_ICON_STATUS_WIFI,
            (fui_rect_t){icon_x, (int16_t)((FT_STATUS_H - 24) / 2), 24, 24},
            s.quick_valid ? C_TEXT : C_MUTED);
        (void)fui_icon_draw(p, FUI_ICON_STATUS_BLUETOOTH,
            (fui_rect_t){(int16_t)(icon_x + 32),
                         (int16_t)((FT_STATUS_H - 24) / 2), 24, 24},
            s.quick_valid ? C_TEXT : C_MUTED);
    }
}

static void draw_nav(fui_painter_t *p)
{
    int16_t center_y = (int16_t)(FT_NAV_Y + FT_NAV_H / 2);
    int16_t back_x = (int16_t)(FT_SCREEN_W / 6);
    int16_t home_x = (int16_t)(FT_SCREEN_W / 2);
    int16_t search_x = (int16_t)(FT_SCREEN_W * 5 / 6);
    fui_painter_rect(p, (fui_rect_t){0, FT_NAV_Y, FT_SCREEN_W, FT_NAV_H}, 0,
                     FUI_RGB(0x04, 0x04, 0x04));
    fui_painter_line(p, back_x - 16, center_y, back_x + 2, center_y - 17, 3, C_TEXT);
    fui_painter_line(p, back_x - 16, center_y, back_x + 2, center_y + 17, 3, C_TEXT);
    fui_painter_line(p, back_x - 15, center_y, back_x + 17, center_y, 3, C_TEXT);
    fui_painter_rect(p, (fui_rect_t){home_x - 19, center_y - 14, 16, 14}, 0, C_TEXT);
    fui_painter_rect(p, (fui_rect_t){home_x, center_y - 14, 16, 14}, 0, C_TEXT);
    fui_painter_rect(p, (fui_rect_t){home_x - 19, center_y + 3, 16, 14}, 0, C_TEXT);
    fui_painter_rect(p, (fui_rect_t){home_x, center_y + 3, 16, 14}, 0, C_TEXT);
    fui_painter_rect(p, (fui_rect_t){search_x - 14, center_y - 17, 23, 23}, 11,
                     FUI_ARGB(0, 0, 0, 0));
    fui_painter_line(p, search_x - 10, center_y + 3, search_x + 7, center_y - 14, 3, C_TEXT);
    fui_painter_line(p, search_x + 7, center_y - 14, search_x + 14, center_y - 21, 3, C_TEXT);
}

static fui_rect_t tile_visual_rect(const ft_app_t *app, bool selected)
{
    fui_rect_t visual = app->rect;
    if (selected)
    {
        int16_t scale = clamp_i16(s.tile_scale, 900,
                                  FT_ANIMATION_SCALE_BASE);
        int16_t inset_x = (int16_t)(((int32_t)visual.width *
            (FT_ANIMATION_SCALE_BASE - scale)) /
            (FT_ANIMATION_SCALE_BASE * 2));
        int16_t inset_y = (int16_t)(((int32_t)visual.height *
            (FT_ANIMATION_SCALE_BASE - scale)) /
            (FT_ANIMATION_SCALE_BASE * 2));
        visual.x = (int16_t)(visual.x + inset_x);
        visual.y = (int16_t)(visual.y + inset_y);
        visual.width = (int16_t)(visual.width - inset_x * 2);
        visual.height = (int16_t)(visual.height - inset_y * 2);
    }
    return visual;
}

static void draw_home_tile(fui_painter_t *p, size_t index, bool selected)
{
    const ft_app_t *app = &s_apps[index];
    fui_rect_t visual = tile_visual_rect(app, selected);
    fui_color_t color = (app->color & 0x00ffffffU) |
                        ((uint32_t)s.prefs.tile_opa << 24);
    fui_painter_rect(p, visual, 3, color);
    (void)fui_icon_draw(p, app->icon,
        (fui_rect_t){(int16_t)(visual.x + visual.width / 2 - 12),
                     (int16_t)(visual.y + visual.height / 2 - 22), 24, 24},
        C_TEXT);
    fui_painter_text(p, visual.x + 8, visual.y + visual.height - 20,
                     1, C_TEXT, app_name(app));
    if (selected)
    {
        int16_t handle = tile_handle_size();
        int16_t inset = clamp_i16(handle / 5, 7, 10);
        int16_t arm = clamp_i16(handle / 3, 12, 17);
        int16_t left = (int16_t)(app->rect.x + inset);
        int16_t top = (int16_t)(app->rect.y + inset);
        int16_t right = (int16_t)(app->rect.x + app->rect.width - inset - 1);
        int16_t bottom = (int16_t)(app->rect.y + app->rect.height - inset - 1);
        fui_painter_rect(p, (fui_rect_t){app->rect.x + 3, app->rect.y + 3,
                                         app->rect.width - 6,
                                         app->rect.height - 6}, 2,
                         FUI_ARGB(0x42, 0xff, 0xff, 0xff));
        /* Handles stay in the logical tile bounds while only its body pulses. */
        fui_painter_line(p, left, (int16_t)(top + arm), left, top, 3, C_TEXT);
        fui_painter_line(p, left, top, (int16_t)(left + arm), top, 3, C_TEXT);
        fui_painter_line(p, (int16_t)(right - arm), top, right, top, 3, C_TEXT);
        fui_painter_line(p, right, top, right, (int16_t)(top + arm), 3, C_TEXT);
        fui_painter_line(p, left, (int16_t)(bottom - arm), left, bottom, 3, C_TEXT);
        fui_painter_line(p, left, bottom, (int16_t)(left + arm), bottom, 3, C_TEXT);
        fui_painter_line(p, (int16_t)(right - arm), bottom, right, bottom, 3, C_TEXT);
        fui_painter_line(p, right, (int16_t)(bottom - arm), right, bottom, 3, C_TEXT);
    }
}

static void draw_home(fui_painter_t *p)
{
    size_t i;
    fui_painter_text(p, s_layout.margin, FT_STATUS_H + 15, 3, C_TEXT,
                     page_title(FT_GPU_PAGE_HOME));
    for (i = 0U; i < FT_APP_COUNT; i++)
        if (!s.desktop_edit || s.selected_tile != (int8_t)i)
            draw_home_tile(p, i, false);
    /* The selected tile is always emitted last, giving it the highest layer. */
    if (s.desktop_edit && s.selected_tile >= 0 &&
        s.selected_tile < (int8_t)FT_APP_COUNT)
        draw_home_tile(p, (size_t)s.selected_tile, true);
    if (s.desktop_edit)
        fui_painter_text(p, s_layout.margin,
                         s_layout.tile_top + default_tile_row_count() *
                         (s_layout.tile_height + s_layout.tile_gap) +
                         16, 1, accent(),
                         tr("EDIT: DRAG TILE / CORNER RESIZE",
                            "编辑：拖动磁贴，拖动四角缩放"));
}

static void draw_search(fui_painter_t *p)
{
    uint8_t i, out = 0U;
    draw_header(p, tr("ALL APPS", "全部应用"));
    draw_search_field(p);
    for (i = 0U; i < FT_APP_COUNT; i++)
        if (text_contains(s_apps[i].name_en, s.search_text) ||
            text_contains(s_apps[i].name_zh, s.search_text))
            draw_row(p, out++, s_apps[i].icon, app_name(&s_apps[i]),
                     page_title(s_apps[i].page), true);
    set_scroll_content_rows(out);
}

static void draw_settings(fui_painter_t *p)
{
    uint8_t i, out = 0U;
    draw_header(p, tr("SEARCH SETTINGS", "搜索设置"));
    draw_search_field(p);
    for (i = 0U; i < FT_SETTING_COUNT; i++)
        if (text_contains(s_settings[i].name_en, s.search_text) ||
            text_contains(s_settings[i].name_zh, s.search_text))
            draw_row(p, out++, s_settings[i].icon,
                     setting_name(i), setting_detail(i), true);
    set_scroll_content_rows(out);
}

static void draw_display(fui_painter_t *p)
{
    char value[20];
    fui_rect_t rotation_switch = row_switch_rect(1U);
    draw_header(p, tr("BACKLIGHT RANGE 50-100% PWM", "背光安全范围：50% 到 100% PWM"));
    if (s.brightness_valid) snprintf(value, sizeof(value), "%u%%", s.brightness);
    else snprintf(value, sizeof(value), "%s", tr("UNAVAILABLE", "不可用"));
    draw_row(p, 0, FUI_ICON_BRIGHTNESS, tr("BRIGHTNESS", "亮度"), value,
             s.brightness_valid);
    draw_meter(p, row_meter_y(0U), s.brightness, accent());
    draw_row(p, 1, FUI_ICON_ROTATION, tr("AUTO ROTATION", "自动旋转"),
             s.quick_valid ? (s.quick.rotation ? tr("LANDSCAPE", "横屏") :
                              tr("PORTRAIT", "竖屏")) : tr("UNAVAILABLE", "不可用"),
             s.quick_valid && (s.quick.capabilities & FEATHERTALK_QUICK_CAP_ROTATION));
    draw_switch(p, rotation_switch.x, rotation_switch.y,
                (s.quick.enabled & FEATHERTALK_QUICK_CAP_ROTATION) != 0U,
                s.quick_valid && (s.quick.capabilities & FEATHERTALK_QUICK_CAP_ROTATION));
    set_scroll_content_rows(2U);
}

static void draw_audio(fui_painter_t *p)
{
    char text[48];
    uint8_t output_percent = value_as_percent(s.audio.output_volume,
                                               s.audio.output_volume_max);
    uint8_t input_percent = value_as_percent(s.audio.input_gain,
                                              s.audio.input_gain_max);
    draw_header(p, tr("LOCAL CODEC AND SPEAKER", "本地编解码器、麦克风和扬声器"));
    snprintf(text, sizeof(text), "%u%%", output_percent);
    draw_row(p, FT_AUDIO_ROW_OUTPUT_VOLUME, FUI_ICON_OUTPUT_VOLUME,
             tr("OUTPUT VOLUME", "输出音量"),
             text, s.audio.output_registered);
    draw_meter(p, row_meter_y(FT_AUDIO_ROW_OUTPUT_VOLUME),
               output_percent, accent());
    snprintf(text, sizeof(text), "%u%%", input_percent);
    draw_row(p, FT_AUDIO_ROW_INPUT_GAIN, FUI_ICON_INPUT_GAIN,
             tr("INPUT GAIN", "输入增益"),
             text, s.audio.input_registered);
    draw_meter(p, row_meter_y(FT_AUDIO_ROW_INPUT_GAIN), input_percent, C_OK);
    snprintf(text, sizeof(text), "%lu HZ", (unsigned long)s.audio.output_sample_rate);
    draw_row(p, FT_AUDIO_ROW_SAMPLE_RATE, FUI_ICON_SAMPLE_RATE,
             tr("SAMPLE RATE", "采样率"),
             text, s.audio.output_registered);
    snprintf(text, sizeof(text), "%u BIT", s.audio.output_sample_bits);
    draw_row(p, FT_AUDIO_ROW_SAMPLE_DEPTH, FUI_ICON_SAMPLE_DEPTH,
             tr("SAMPLE DEPTH", "采样深度"),
             text, s.audio.output_registered);
    snprintf(text, sizeof(text), "%u CH", s.audio.output_channels);
    draw_row(p, FT_AUDIO_ROW_CHANNELS, FUI_ICON_CHANNELS,
             tr("CHANNELS", "声道数"),
             text, s.audio.output_registered);
    draw_row(p, FT_AUDIO_ROW_OUTPUT_DEVICE, FUI_ICON_SPEAKER,
             tr("SPEAKER / DAC", "扬声器 / DAC"),
             s.audio.output_ready ? tr("READY", "就绪") : tr("NOT READY", "未就绪"),
             s.audio.output_registered);
    draw_row(p, FT_AUDIO_ROW_INPUT_DEVICE, FUI_ICON_MICROPHONE,
             tr("MICROPHONE", "麦克风"),
             s.audio.input_ready ? tr("READY", "就绪") : tr("NOT READY", "未就绪"),
             s.audio.input_registered);
    set_scroll_content_rows(FT_AUDIO_ROW_COUNT);
}

static void draw_radio_page(fui_painter_t *p, bool bluetooth)
{
    uint8_t cap = bluetooth ? FEATHERTALK_QUICK_CAP_BLUETOOTH : FEATHERTALK_QUICK_CAP_WIFI;
    bool available = s.quick_valid && (s.quick.capabilities & cap) != 0U;
    bool on = (s.quick.enabled & cap) != 0U;
    char detail[40];
    fui_rect_t radio_switch = row_switch_rect(0U);
    draw_header(p, available ? tr("M33 RADIO SERVICE", "M33 无线服务") :
                               tr("M33 DRIVER UNAVAILABLE", "M33 驱动不可用"));
    if (!bluetooth && available)
        snprintf(detail, sizeof(detail), on ? tr("ON / SIGNAL %u%%", "已开启 / 信号 %u%%") :
                                              tr("OFF", "已关闭"),
                 s.quick.wifi_signal_percent);
    else snprintf(detail, sizeof(detail), "%s", on ? tr("ON", "已开启") :
                                                     tr("OFF", "已关闭"));
    draw_row(p, 0, bluetooth ? FUI_ICON_BLUETOOTH : FUI_ICON_WIFI,
             bluetooth ? tr("BLUETOOTH", "蓝牙") : "WI-FI",
             detail, available);
    draw_switch(p, radio_switch.x, radio_switch.y, on, available);
    draw_row(p, 1, bluetooth ? FUI_ICON_PAIRED_DEVICES : FUI_ICON_NETWORK_SCAN,
             bluetooth ? tr("PAIRED DEVICES", "已配对设备") :
                         tr("AVAILABLE NETWORKS", "可用网络"),
             available ? tr("SCAN SERVICE PENDING", "扫描服务待接入") :
                         tr("NO HARDWARE STATUS", "无硬件状态"), available);
    set_scroll_content_rows(2U);
}

static const char *bytes_text(uint64_t bytes, char *buffer, size_t size)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        snprintf(buffer, size, "%lu.%lu GB", (unsigned long)(bytes >> 30),
                 (unsigned long)((bytes >> 20) % 1024U * 10U / 1024U));
    else snprintf(buffer, size, "%lu MB", (unsigned long)(bytes >> 20));
    return buffer;
}

static void draw_storage(fui_painter_t *p)
{
    ft_storage_device_info_t *d = s.storage_selected == 0U ? &s.flash : &s.sd;
    char total[24], free_space[24], detail[48];
    char flash_label[32], sd_label[32];
    fui_component_style_t style = component_style();
    fui_option_t devices[2];
    fui_segmented_control_t selector;
    uint8_t used = 0U;
    draw_header(p, tr("SELECT A DEVICE THEN OPERATE", "选择存储设备后独立操作"));
    snprintf(flash_label, sizeof(flash_label), "FLASH %s",
             s.flash.present ? tr("READY", "就绪") : tr("MISSING", "缺失"));
    snprintf(sd_label, sizeof(sd_label), "SD %s",
             s.sd.present ? tr("READY", "就绪") : tr("MISSING", "缺失"));
    devices[0] = (fui_option_t){flash_label, FUI_COMPONENT_STATE_DEFAULT};
    devices[1] = (fui_option_t){sd_label, FUI_COMPONENT_STATE_DEFAULT};
    selector = (fui_segmented_control_t){
        .bounds = {FT_ROW_X, row_y(0U), FT_ROW_W, FT_ROW_H},
        .options = devices,
        .option_count = 2U,
        .selected_index = s.storage_selected,
        .state = FUI_COMPONENT_STATE_DEFAULT,
        .text_scale = 1U
    };
    (void)fui_component_segmented_control(p, &selector, &style);
    if (d->volume_total_bytes != 0U)
        used = (uint8_t)(((d->volume_total_bytes - d->volume_free_bytes) * 100U) /
                         d->volume_total_bytes);
    snprintf(detail, sizeof(detail), tr("%s TOTAL / %s FREE", "总计 %s / 可用 %s"),
             bytes_text(d->volume_total_bytes, total, sizeof(total)),
             bytes_text(d->volume_free_bytes, free_space, sizeof(free_space)));
    draw_row(p, 1, FUI_ICON_CAPACITY,
             s.storage_selected ? tr("SD CAPACITY", "SD 卡容量") :
                                           tr("FLASH CAPACITY", "Flash 容量"),
             detail, d->present);
    draw_meter(p, row_meter_y(1U), used, s.storage_selected ? C_OK : accent());
    draw_row(p, 2, FUI_ICON_BROWSE, tr("BROWSE FILES", "浏览文件"),
             d->mounted ? d->filesystem : tr("NOT MOUNTED", "未挂载"), d->mounted);
    draw_row(p, 3, FUI_ICON_FORMAT, tr("FORMAT DEVICE", "格式化设备"),
             tr("REQUIRES TWO CONFIRMATIONS", "需要两次确认"),
             d->can_format && !d->usb_exported);
    set_scroll_content_rows(4U);
}

static void draw_usb(fui_painter_t *p)
{
    char text[48];
    draw_header(p, s.usb.connected ? tr("HOST CONNECTED", "USB 主机已连接") :
                                     tr("WAITING FOR USB HOST", "等待 USB 主机"));
    draw_row(p, FT_USB_ROW_ROLE, FUI_ICON_USB_ROLE, tr("USB ROLE", "USB 角色"),
             tr("DEVICE / HOST MODE UNAVAILABLE", "设备模式 / 主机模式不可用"), true);
    draw_row(p, FT_USB_ROW_STORAGE, FUI_ICON_USB_STORAGE,
             tr("MASS STORAGE", "大容量存储"),
             s.usb.function == FT_USB_FUNCTION_STORAGE ? tr("SELECTED", "已选择") :
                                                         "FLASH + SD LUN",
             s.usb.storage_supported);
    draw_row(p, FT_USB_ROW_AUDIO, FUI_ICON_USB_AUDIO,
             tr("USB AUDIO (UAC)", "USB 音频（UAC）"),
             s.usb.function == FT_USB_FUNCTION_AUDIO ? tr("SELECTED", "已选择") :
                                                       tr("INPUT + OUTPUT", "输入 + 输出"),
             s.usb.audio_supported);
    snprintf(text, sizeof(text), "%lu HZ", (unsigned long)s.usb.uac_output_sample_rate);
    draw_row(p, FT_USB_ROW_OUTPUT_RATE, FUI_ICON_USB_OUTPUT,
             tr("UAC OUTPUT RATE", "UAC 输出采样率"), text,
             s.usb.function == FT_USB_FUNCTION_AUDIO);
    snprintf(text, sizeof(text), "%u BIT", s.usb.uac_output_sample_bits);
    draw_row(p, FT_USB_ROW_OUTPUT_DEPTH, FUI_ICON_SAMPLE_DEPTH,
             tr("UAC OUTPUT DEPTH", "UAC 输出采样深度"), text,
             s.usb.function == FT_USB_FUNCTION_AUDIO);
    snprintf(text, sizeof(text), "%u CH", s.usb.uac_output_channels);
    draw_row(p, FT_USB_ROW_OUTPUT_CHANNELS, FUI_ICON_CHANNELS,
             tr("UAC OUTPUT CHANNELS", "UAC 输出声道数"), text,
             s.usb.function == FT_USB_FUNCTION_AUDIO);
    snprintf(text, sizeof(text), "%lu HZ / %u BIT / %u CH",
             (unsigned long)s.usb.uac_input_sample_rate,
             s.usb.uac_input_sample_bits, s.usb.uac_input_channels);
    draw_row(p, FT_USB_ROW_INPUT_FORMAT, FUI_ICON_USB_INPUT,
             tr("UAC INPUT (HOST CONTROLLED)", "UAC 输入（主机控制）"), text,
             s.usb.function == FT_USB_FUNCTION_AUDIO);
    snprintf(text, sizeof(text), "%s / GEN %lu",
             s.usb.configured ? tr("CONFIGURED", "已配置") : tr("IDLE", "空闲"),
             (unsigned long)s.usb.uac_sync_generation);
    draw_row(p, FT_USB_ROW_STATUS, FUI_ICON_USB_STATUS,
             tr("USB STATUS", "USB 状态"), text, true);
    set_scroll_content_rows(FT_USB_ROW_COUNT);
}

static void draw_time_language(fui_painter_t *p)
{
    char zone[32];
    int minutes = s.prefs.timezone_offset_minutes;
    fui_rect_t clock_switch = row_switch_rect(0U);
    snprintf(zone, sizeof(zone), "UTC%c%02d:%02d", minutes < 0 ? '-' : '+',
             (minutes < 0 ? -minutes : minutes) / 60,
             (minutes < 0 ? -minutes : minutes) % 60);
    draw_header(p, tr("CLOCK AND LOCALIZATION", "时钟与本地化"));
    draw_row(p, 0, FUI_ICON_CLOCK, tr("24-HOUR CLOCK", "24 小时制"),
             s.prefs.use_24_hour ? tr("ON", "开启") : tr("OFF", "关闭"), true);
    draw_switch(p, clock_switch.x, clock_switch.y,
                s.prefs.use_24_hour, true);
    draw_row(p, 1, FUI_ICON_TIMEZONE, tr("TIME ZONE", "时区"), zone, true);
    draw_row(p, 2, FUI_ICON_LANGUAGE, tr("LANGUAGE", "语言"),
             s.prefs.language == 0U ? "简体中文" : "ENGLISH", true);
    draw_row(p, 3, FUI_ICON_KEYBOARD, tr("KEYBOARD", "键盘"),
             tr("LANGUAGE-AWARE ON-SCREEN INPUT", "跟随语言的屏幕键盘"), true);
    set_scroll_content_rows(4U);
}

static void draw_personalization(fui_painter_t *p)
{
    char text[24];
    uint8_t background = s.prefs.background < FT_BACKGROUND_COUNT ?
                         s.prefs.background : FT_BACKGROUND_BLACK;
    draw_header(p, tr("PER-DEVICE SETTINGS IN FLASH", "本机配置保存在 Flash 中"));
    snprintf(text, sizeof(text), "#%06lX", (unsigned long)s.prefs.accent_rgb);
    draw_row(p, 0, FUI_ICON_ACCENT, tr("ACCENT COLOR", "强调色"), text, true);
    snprintf(text, sizeof(text), "%u / 255", s.prefs.tile_opa);
    draw_row(p, 1, FUI_ICON_OPACITY, tr("TILE OPACITY", "磁贴透明度"), text, true);
    draw_row(p, 2, FUI_ICON_BACKGROUND, tr("BACKGROUND", "背景"),
             tr(s_backgrounds[background].name_en,
                s_backgrounds[background].name_zh), true);
    draw_row(p, 3, FUI_ICON_WALLPAPER, tr("WALLPAPER", "壁纸"),
             s.prefs.wallpaper_path[0] ? s.prefs.wallpaper_path : tr("DEFAULT", "默认"), true);
    set_scroll_content_rows(4U);
}

static void draw_system(fui_painter_t *p)
{
    char text[48];
    fui_engine_stats_t stats;
    draw_header(p, tr("PSOC EDGE E84 RESOURCE SUMMARY", "PSoC Edge E84 资源摘要"));
    snprintf(text, sizeof(text), tr("CM55 %lu MHZ / CM33 SERVICE CORE",
                                    "CM55 %lu MHz / CM33 服务核心"),
             (unsigned long)(SystemCoreClock / 1000000U));
    draw_row(p, 0, FUI_ICON_PROCESSOR, tr("PROCESSORS", "处理器"), text, true);
    draw_row(p, 1, FUI_ICON_ONCHIP_MEMORY, tr("ON-CHIP MEMORY", "片上存储"),
             "M55 DTCM 256 KB / GFX SRAM 3 MB", true);
    draw_row(p, 2, FUI_ICON_EXTERNAL_MEMORY, tr("EXTERNAL MEMORY", "片外存储"),
             "HyperRAM 16 MB / xSPI Flash", true);
    snprintf(text, sizeof(text), "Flash %s / SD %s",
             s.flash.present ? tr("READY", "就绪") : tr("MISSING", "缺失"),
             s.sd.present ? tr("READY", "就绪") : tr("MISSING", "缺失"));
    draw_row(p, 3, FUI_ICON_SETTING_STORAGE, tr("STORAGE", "存储设备"), text, true);
    fui_engine_get_stats(&stats);
    snprintf(text, sizeof(text), tr("%lu CMDS / %lu US GPU / 1 SUBMIT",
                                    "%lu 命令 / GPU %lu 微秒 / 单次提交"),
             (unsigned long)stats.commands_last, (unsigned long)stats.gpu_busy_us_last);
    draw_row(p, 4, FUI_ICON_GPU2D, "GPU2D", text, true);
    draw_row(p, 5, FUI_ICON_PERIPHERALS, tr("PERIPHERALS", "外设"),
             tr("MIPI DSI / SDHC / USB HS / AUDIO / RADIO",
                "MIPI DSI / SDHC / USB HS / 音频 / 无线"), true);
    set_scroll_content_rows(6U);
}

static void draw_about(fui_painter_t *p)
{
    char version[48];

    draw_header(p, tr("FEATHERTALK GPU-NATIVE SHELL", "FeatherTalk GPU 原生界面"));
    draw_row(p, 0, FUI_ICON_PRODUCT, tr("PRODUCT", "产品"),
             "Edgi Talk / PSoC Edge E84", true);
    snprintf(version, sizeof(version), "FeatherUI %u.%u.%u / LVGL Disabled",
             FUI_VERSION_MAJOR, FUI_VERSION_MINOR, FUI_VERSION_PATCH);
    draw_row(p, 1, FUI_ICON_UI_ENGINE, tr("UI ENGINE", "UI 引擎"), version, true);
    draw_row(p, 2, FUI_ICON_RENDER_CONTRACT, tr("RENDER CONTRACT", "渲染约束"),
             tr("ONE GPU COMMAND LIST PER FRAME", "每帧一条 GPU 命令链"), true);
    draw_row(p, 3, FUI_ICON_SOFTWARE, tr("SOFTWARE", "软件"),
             "RT-Thread + Infineon PDL", true);
    set_scroll_content_rows(4U);
}

static void draw_media(fui_painter_t *p)
{
    fui_rect_t panel = media_panel_rect();
    fui_rect_t controls = media_controls_rect();
    int16_t third = (int16_t)(controls.width / 3);
    uint8_t volume_row = media_volume_row();
    uint8_t output_row = (uint8_t)(volume_row + 1U);
    char volume[20];
    draw_header(p, tr("LOCAL AND USB AUDIO PLAYBACK", "本地与 USB 音频播放"));
    fui_painter_rect(p, panel, 8, C_PANEL);
    fui_painter_text(p, panel.x + s_layout.margin,
                     panel.y + panel.height * 13 / 100, 3, C_TEXT,
                     tr("NOW PLAYING", "正在播放"));
    fui_painter_text(p, panel.x + s_layout.margin,
                     panel.y + panel.height * 31 / 100, 2, accent(),
                      s.media_state ? tr("PLAYING", "播放中") : tr("PAUSED", "已暂停"));
    fui_painter_text(p, panel.x + s_layout.margin,
                     panel.y + panel.height * 45 / 100, 2, C_TEXT,
                     tr(s_media_tracks[s.media_track].name_en,
                        s_media_tracks[s.media_track].name_zh));
    fui_painter_text(p, controls.x + third / 2 - 12, controls.y, 3, C_TEXT, "<<");
    fui_painter_text(p, controls.x + third + third / 2 - 8, controls.y, 3,
                     C_TEXT, s.media_state ? "||" : ">");
    fui_painter_text(p, controls.x + third * 2 + third / 2 - 12,
                     controls.y, 3, C_TEXT, ">>");
    snprintf(volume, sizeof(volume), "%u%%",
             value_as_percent(s.media_volume, s.audio.output_volume_max));
    draw_row(p, volume_row, FUI_ICON_OUTPUT_VOLUME,
             tr("PLAYBACK VOLUME", "播放音量"), volume, true);
    draw_meter(p, row_meter_y(volume_row),
               value_as_percent(s.media_volume, s.audio.output_volume_max), accent());
    draw_row(p, output_row, FUI_ICON_SPEAKER, tr("OUTPUT DEVICE", "输出设备"),
             tr("LOCAL SPEAKER / UAC", "本地扬声器 / UAC"), true);
    set_scroll_content_rows((uint8_t)(output_row + 1U));
}

static uint8_t recorder_device_rows(void)
{
    size_t count = s.recorder_device_count;
    if (count > FT_RECORDER_DEVICE_COUNT) count = FT_RECORDER_DEVICE_COUNT;
    return (uint8_t)count;
}

static void draw_recorder(fui_painter_t *p)
{
    char text[48];
    size_t i;
    uint8_t device_rows = recorder_device_rows();
    uint8_t control_row = device_rows;
    uint8_t level_row = (uint8_t)(control_row + 1U);
    uint8_t saved_row = (uint8_t)(level_row + 1U);
    draw_header(p, tr("RECORD WAV TO FLASH OR SD", "录制 WAV 到 Flash 或 SD 卡"));
    for (i = 0U; i < device_rows; i++)
    {
        snprintf(text, sizeof(text), "%lu HZ / %u BIT / %u CH%s",
                 (unsigned long)s.recorder_devices[i].sample_rate,
                 s.recorder_devices[i].sample_bits, s.recorder_devices[i].channels,
                 s.recorder.selected_device == i ? tr(" / SELECTED", " / 已选择") : "");
        draw_row(p, (uint8_t)i,
                 i == 0U ? FUI_ICON_MICROPHONE : FUI_ICON_INPUT_GAIN,
                 s.recorder_devices[i].device_name, text,
                 s.recorder_devices[i].registered);
    }
    snprintf(text, sizeof(text), "%lu MS / %lu BYTES",
             (unsigned long)s.recorder.duration_ms,
             (unsigned long)s.recorder.data_bytes);
    draw_row(p, control_row, FUI_ICON_APP_RECORDER,
             s.recorder.state == FT_RECORDER_RECORDING ? tr("STOP & SAVE", "停止并保存") :
                                                         tr("START RECORDING", "开始录音"),
             text, s.recorder.state == FT_RECORDER_RECORDING || ft_recorder_can_start());
    draw_row(p, level_row, FUI_ICON_SAMPLE_RATE,
             tr("INPUT LEVEL", "输入电平"), RT_NULL,
             s.recorder.state == FT_RECORDER_RECORDING);
    draw_meter(p, row_meter_y(level_row),
               (uint8_t)(s.recorder.peak_per_mille / 10U), C_WARN);
    draw_row(p, saved_row, FUI_ICON_RECORDINGS,
             tr("SAVED RECORDINGS", "已保存的录音"),
             s.recorder.file_path[0] ? s.recorder.file_path :
                                       tr("NO NEW RECORDING", "暂无新录音"), true);
    set_scroll_content_rows((uint8_t)(saved_row + 1U));
}

static bool collect_file(const ft_storage_entry_t *entry, void *context)
{
    (void)context;
    if (s.file_count >= FT_FILE_CAPACITY) return false;
    s.files[s.file_count++] = *entry;
    return true;
}

static void reload_files(void)
{
    s.file_count = 0U;
    if (strcmp(s.file_path, "/") == 0)
    {
        memset(&s.files[0], 0, sizeof(s.files[0]));
        s.files[0].type = FT_STORAGE_ENTRY_DIRECTORY;
        strcpy(s.files[0].name, "flash");
        memset(&s.files[1], 0, sizeof(s.files[1]));
        s.files[1].type = FT_STORAGE_ENTRY_DIRECTORY;
        strcpy(s.files[1].name, "sdcard");
        s.file_count = 2U;
    }
    else (void)ft_storage_list(s.file_path, FT_STORAGE_ENTRY_ANY, collect_file, RT_NULL);
}

static bool image_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == RT_NULL) return false;
    return strcmp(dot, ".png") == 0 || strcmp(dot, ".PNG") == 0 ||
           strcmp(dot, ".jpg") == 0 || strcmp(dot, ".JPG") == 0 ||
           strcmp(dot, ".jpeg") == 0 || strcmp(dot, ".JPEG") == 0 ||
           strcmp(dot, ".bmp") == 0 || strcmp(dot, ".BMP") == 0;
}

static void draw_files(fui_painter_t *p, bool gallery)
{
    uint8_t i, out = 0U;
    char detail[40];
    draw_header(p, gallery ? tr("PHOTO LIBRARY", "照片库") : s.file_path);
    if (gallery)
    {
        bool sd = strncmp(s.file_path, "/sdcard", 7U) == 0;
        fui_rect_t flash_button = gallery_source_rect(false);
        fui_rect_t sd_button = gallery_source_rect(true);
        fui_component_style_t style = component_style();
        fui_option_t sources[2] = {
            {"FLASH", FUI_COMPONENT_STATE_DEFAULT},
            {"SD", s.sd.present ? FUI_COMPONENT_STATE_DEFAULT :
                                  FUI_COMPONENT_STATE_DISABLED}
        };
        fui_segmented_control_t source = {
            .bounds = {flash_button.x, flash_button.y,
                       (int16_t)(sd_button.x + sd_button.width - flash_button.x),
                       flash_button.height},
            .options = sources,
            .option_count = 2U,
            .selected_index = sd ? 1U : 0U,
            .state = FUI_COMPONENT_STATE_DEFAULT,
            .text_scale = 1U
        };
        style.radius = 4U;
        style.surface = C_PANEL_2;
        (void)fui_component_segmented_control(p, &source, &style);
    }
    if (gallery && s.gallery_viewer && s.file_selected >= 0)
    {
        const ft_storage_entry_t *entry = &s.files[s.file_selected];
        ft_gpu_image_info_t image;
        fui_rect_t panel = gallery_view_panel_rect();
        fui_rect_t image_bounds = gallery_image_bounds();
        fui_rect_t actions[FT_GALLERY_ACTION_COUNT];
        fui_rect_t delete_button = gallery_delete_rect();
        memset(&image, 0, sizeof(image));
        for (i = 0U; i < FT_GALLERY_ACTION_COUNT; i++)
            actions[i] = gallery_action_rect(i);
        fui_painter_rect(p, panel, 8, C_PANEL);
        if (ft_gpu_image_get(FT_GPU_IMAGE_GALLERY, &image) &&
            image.state == FT_GPU_IMAGE_READY)
        {
            int16_t x = (int16_t)(image_bounds.x +
                (image_bounds.width - image.image.width) / 2);
            int16_t y = (int16_t)(image_bounds.y +
                (image_bounds.height - image.image.height) / 2);
            fui_painter_image_rgb565(p, x, y, &image.image);
            snprintf(detail, sizeof(detail), "%ux%u / %lu MS",
                     image.source_width, image.source_height,
                     (unsigned long)image.decode_ms);
            fui_painter_text(p, panel.x + s_layout.margin,
                             panel.y + panel.height + FT_ROW_H / 2,
                             1, C_MUTED, detail);
        }
        else if (image.state == FT_GPU_IMAGE_LOADING)
        {
            fui_component_style_t style = component_style();
            fui_spinner_t spinner = {
                .bounds = {(int16_t)(panel.x + panel.width / 2 - 14),
                           (int16_t)(panel.y + panel.height / 2 - 28), 28, 28},
                .state = FUI_COMPONENT_STATE_DEFAULT,
                .phase = (uint8_t)((rt_tick_get_millisecond() / 100U) & 7U)
            };
            (void)fui_component_spinner(p, &spinner, &style);
            fui_painter_text(p, (int16_t)(panel.x + panel.width / 2 - 64),
                             (int16_t)(panel.y + panel.height / 2 + 22),
                             2, C_TEXT, tr("LOADING...", "正在加载…"));
        }
        else
        {
            (void)fui_icon_draw(p, FUI_ICON_ERROR,
                (fui_rect_t){(int16_t)(panel.x + panel.width / 2 - 12),
                             (int16_t)(panel.y + panel.height / 2 - 24), 24, 24},
                C_WARN);
            fui_painter_text(p, panel.x + s_layout.margin,
                             (int16_t)(panel.y + panel.height / 2 + 22), 1, C_WARN,
                             image.error[0] ? image.error :
                             tr("IMAGE NOT AVAILABLE", "图片不可用"));
        }
        fui_painter_text(p, panel.x + s_layout.margin,
                         panel.y + panel.height + 8, 2, C_TEXT, entry->name);
        {
            fui_component_style_t style = component_style();
            fui_button_t buttons[FT_GALLERY_ACTION_COUNT] = {
                {actions[FT_GALLERY_ACTION_PREVIOUS], tr("PREV", "上一张"),
                 FUI_COMPONENT_STATE_DEFAULT, FUI_BUTTON_SECONDARY, 1U},
                {actions[FT_GALLERY_ACTION_NEXT], tr("NEXT", "下一张"),
                 FUI_COMPONENT_STATE_DEFAULT, FUI_BUTTON_SECONDARY, 1U},
                {actions[FT_GALLERY_ACTION_WALLPAPER], tr("WALLPAPER", "设为壁纸"),
                 FUI_COMPONENT_STATE_DEFAULT, FUI_BUTTON_PRIMARY, 1U},
                {actions[FT_GALLERY_ACTION_CLOSE], tr("CLOSE", "关闭"),
                 FUI_COMPONENT_STATE_DEFAULT, FUI_BUTTON_SECONDARY, 1U}
            };
            fui_button_t remove = {delete_button,
                tr("DELETE PHOTO", "删除照片"),
                FUI_COMPONENT_STATE_DEFAULT, FUI_BUTTON_DANGER, 1U};
            for (i = 0U; i < FT_GALLERY_ACTION_COUNT; i++)
                (void)fui_component_button(p, &buttons[i], &style);
            (void)fui_component_button(p, &remove, &style);
        }
        return;
    }
    for (i = 0U; i < s.file_count; i++)
    {
        const ft_storage_entry_t *entry = &s.files[i];
        if (gallery && entry->type != FT_STORAGE_ENTRY_DIRECTORY && !image_name(entry->name))
            continue;
        if (entry->type == FT_STORAGE_ENTRY_DIRECTORY)
            snprintf(detail, sizeof(detail), "%s", tr("FOLDER", "文件夹"));
        else snprintf(detail, sizeof(detail), tr("%lu BYTES", "%lu 字节"),
                      (unsigned long)entry->size_bytes);
        draw_row(p, out++, entry->type == FT_STORAGE_ENTRY_DIRECTORY ?
                 FUI_ICON_FOLDER : FUI_ICON_FILE,
                 entry->name, detail, true);
    }
    if (out == 0U)
        fui_painter_text(p, s_layout.margin,
                         (int16_t)(FT_LIST_TOP + FT_ROW_H / 2), 2, C_MUTED,
                         gallery ? tr("NO PHOTOS", "没有照片") :
                                   tr("EMPTY FOLDER", "文件夹为空"));
    set_scroll_content_rows(out);
    if (s.file_menu && s.file_selected >= 0)
    {
        ft_file_menu_model_t model;
        fui_component_style_t style = component_style();
        if (file_menu_model_build(&model, s.root_format_menu))
        {
            style.surface_alt = FUI_RGB(0x35, 0x35, 0x35);
            style.surface_selected = accent();
            style.radius = 8U;
            (void)fui_component_context_menu(p, &model.menu, &style);
        }
    }
}

static void draw_shade(fui_painter_t *p)
{
    int16_t y = s.shade_y;
    int16_t gap = s_layout.tile_gap;
    int16_t card_width = (int16_t)((FT_ROW_W -
        (FT_QUICK_CARD_COUNT - 1U) * gap) / FT_QUICK_CARD_COUNT);
    int16_t card_y = (int16_t)(y + shade_offset(58));
    int16_t card_h = shade_offset(94);
    bool wifi = (s.quick.enabled & FEATHERTALK_QUICK_CAP_WIFI) != 0U;
    bool bt = (s.quick.enabled & FEATHERTALK_QUICK_CAP_BLUETOOTH) != 0U;
    ft_notification_t n;
    uint8_t i;
    char bright[20];
    fui_painter_rect(p, (fui_rect_t){0, y, FT_SCREEN_W, FT_SHADE_H}, 0,
                     FUI_RGB(0x14, 0x14, 0x14));
    fui_painter_text(p, s_layout.margin, y + shade_offset(20), 2, C_TEXT,
                     tr("QUICK SETTINGS", "快捷设置"));
    for (i = 0U; i < FT_QUICK_CARD_COUNT; i++)
        fui_painter_rect(p, (fui_rect_t){(int16_t)(FT_ROW_X + i * (card_width + gap)),
                          card_y, card_width, card_h}, 7,
                         i == 0U ? (wifi ? accent() : C_OFF) :
                         i == 1U ? (bt ? accent() : C_OFF) :
                         i == 2U ? (s.brightness_valid ? C_PANEL_2 : C_OFF) :
                                   C_PANEL_2);
    (void)fui_icon_draw(p, FUI_ICON_WIFI,
        (fui_rect_t){(int16_t)(FT_ROW_X + (card_width - 24) / 2),
                     (int16_t)(y + shade_offset(74)), 24, 24}, C_TEXT);
    (void)fui_icon_draw(p, FUI_ICON_BLUETOOTH,
        (fui_rect_t){(int16_t)(FT_ROW_X + card_width + gap +
                               (card_width - 24) / 2),
                     (int16_t)(y + shade_offset(74)), 24, 24}, C_TEXT);
    (void)fui_icon_draw(p, FUI_ICON_QUICK_BRIGHTNESS,
        (fui_rect_t){(int16_t)(FT_ROW_X + 2 * (card_width + gap) +
                               (card_width - 24) / 2),
                     (int16_t)(y + shade_offset(74)), 24, 24},
        s.brightness_valid ? C_TEXT : C_MUTED);
    (void)fui_icon_draw(p, FUI_ICON_QUICK_ROTATION,
        (fui_rect_t){(int16_t)(FT_ROW_X + 3 * (card_width + gap) +
                               (card_width - 24) / 2),
                     (int16_t)(y + shade_offset(74)), 24, 24}, C_TEXT);
    fui_painter_text(p, FT_ROW_X + 12, y + shade_offset(126), 1, C_TEXT, "WI-FI");
    fui_painter_text(p, FT_ROW_X + card_width + gap + 8,
                     y + shade_offset(126), 1, C_TEXT, tr("BLUETOOTH", "蓝牙"));
    fui_painter_text(p, FT_ROW_X + 2 * (card_width + gap) + 8,
                     y + shade_offset(126), 1, C_TEXT, tr("BRIGHTNESS", "亮度"));
    fui_painter_text(p, FT_ROW_X + 3 * (card_width + gap) + 12,
                     y + shade_offset(126), 1, C_TEXT, tr("ROTATION", "旋转"));
    snprintf(bright, sizeof(bright), "%u%%", s.brightness);
    fui_painter_text(p, s_layout.margin, y + shade_offset(178), 2, C_TEXT, bright);
    draw_meter(p, y + shade_offset(208), s.brightness, accent());
    fui_painter_text(p, s_layout.margin, y + shade_offset(244), 2, C_TEXT,
                     tr("NOTIFICATIONS", "通知"));
    fui_painter_text(p, FT_SCREEN_W - s_layout.margin - 76,
                     y + shade_offset(246), 1, accent(), tr("CLEAR ALL", "全部清除"));
    for (i = 0U; i < shade_notification_capacity() &&
                 ft_notifications_get(i, &n); i++)
    {
        int16_t row_step = shade_offset(FT_SHADE_NOTIFICATION_STEP);
        int16_t ny = (int16_t)(y + shade_offset(FT_SHADE_NOTIFICATION_TOP) +
                               i * row_step);
        int16_t notification_h = shade_offset(76);
        fui_painter_rect(p, (fui_rect_t){FT_ROW_X, ny, FT_ROW_W, notification_h}, 6, C_PANEL);
        fui_painter_text(p, FT_ROW_X + 16, ny + shade_offset(12), 1, accent(), n.source);
        fui_painter_text(p, FT_ROW_X + 16, ny + shade_offset(31), 2, C_TEXT, n.title);
        fui_painter_text(p, FT_ROW_X + 16, ny + shade_offset(57), 1, C_MUTED, n.body);
    }
}

static void draw_key(fui_painter_t *p, int16_t x, int16_t y, int16_t width,
                      const char *label, fui_color_t color)
{
    fui_component_style_t style = component_style();
    int16_t height = (int16_t)(keyboard_y(254) - keyboard_y(212));
    fui_button_t button = {
        .bounds = {x, y, width, height},
        .label = label,
        .state = FUI_COMPONENT_STATE_DEFAULT,
        .variant = FUI_BUTTON_SECONDARY,
        .text_scale = 2U
    };
    style.surface = color;
    style.radius = 5U;
    (void)fui_component_button(p, &button, &style);
}

static void draw_keyboard(fui_painter_t *p)
{
    static const char *row0 = "QWERTYUIOP";
    static const char *row1 = "ASDFGHJKL";
    static const char *row2 = "ZXCVBNM";
    char key[2] = {0, 0};
    uint8_t i;
    fui_component_style_t style = component_style();
    fui_text_field_t field;
    fui_painter_rect(p, (fui_rect_t){0, s_layout.keyboard_y, FT_SCREEN_W,
                                      s_layout.keyboard_height}, 0,
                     FUI_RGB(0x18, 0x18, 0x18));
    memset(&field, 0, sizeof(field));
    field.bounds = (fui_rect_t){keyboard_x(18), keyboard_y(10),
                                keyboard_width(444),
                                (int16_t)(keyboard_y(48) - keyboard_y(10))};
    field.text = s.keyboard_input;
    field.placeholder = tr("TYPE HERE", "在此输入");
    field.state = FUI_COMPONENT_STATE_FOCUSED;
    field.text_scale = 2U;
    style.padding = keyboard_width(12);
    (void)fui_component_text_field(p, &field, &style);
    for (i = 0U; i < (uint8_t)strlen(row0); i++)
    {
        key[0] = row0[i];
        draw_key(p, keyboard_x((int16_t)(10 + i * 46)), keyboard_y(58),
                 keyboard_width(42), key, C_PANEL_2);
    }
    for (i = 0U; i < (uint8_t)strlen(row1); i++)
    {
        key[0] = row1[i];
        draw_key(p, keyboard_x((int16_t)(32 + i * 46)), keyboard_y(108),
                 keyboard_width(42), key, C_PANEL_2);
    }
    for (i = 0U; i < (uint8_t)strlen(row2); i++)
    {
        key[0] = row2[i];
        draw_key(p, keyboard_x((int16_t)(78 + i * 46)), keyboard_y(158),
                 keyboard_width(42), key, C_PANEL_2);
    }
    draw_key(p, keyboard_x(20), keyboard_y(212), keyboard_width(218),
             tr("SPACE", "空格"), C_PANEL_2);
    draw_key(p, keyboard_x(248), keyboard_y(212), keyboard_width(62), "<", C_PANEL_2);
    draw_key(p, keyboard_x(320), keyboard_y(212), keyboard_width(62),
             tr("OK", "确定"), accent());
    draw_key(p, keyboard_x(392), keyboard_y(212), keyboard_width(68), "V", C_OFF);
}

static fui_rect_t dialog_panel_rect(void)
{
    fui_rect_t panel;
    panel.width = clamp_i16(FT_SCREEN_W - s_layout.margin * 2,
                            220, 440);
    panel.height = clamp_i16(FT_CONTENT_H * 2 / 5, 190, 270);
    panel.x = (int16_t)((FT_SCREEN_W - panel.width) / 2);
    panel.y = (int16_t)(FT_CONTENT_Y +
                        (FT_CONTENT_H - panel.height) / 2);
    return panel;
}

static fui_rect_t toast_rect(void)
{
    int16_t height = clamp_i16(FT_ROW_H * 4 / 5, 40, 56);
    return (fui_rect_t){s_layout.margin,
                        (int16_t)(FT_NAV_Y - s_layout.margin - height),
                        (int16_t)(FT_SCREEN_W - s_layout.margin * 2), height};
}

static void toast_show(const char *message, uint32_t now_ms)
{
    if (message == RT_NULL) message = "";
    rt_strncpy(s.toast_message, message, sizeof(s.toast_message) - 1U);
    s.toast_message[sizeof(s.toast_message) - 1U] = '\0';
    s.toast_until_ms = now_ms + 2200U;
    s.toast_visible = true;
}

static void draw_toast(fui_painter_t *p)
{
    fui_component_style_t style = component_style();
    fui_toast_t toast = {
        .bounds = toast_rect(),
        .message = s.toast_message,
        .action_label = RT_NULL,
        .state = FUI_COMPONENT_STATE_DEFAULT,
        .text_scale = 2U
    };
    style.surface_alt = FUI_RGB(0x35, 0x35, 0x35);
    style.radius = 8U;
    (void)fui_component_toast(p, &toast, &style);
}

static fui_rect_t dialog_button_rect(bool primary)
{
    fui_dialog_t dialog;
    memset(&dialog, 0, sizeof(dialog));
    dialog.bounds = dialog_panel_rect();
    dialog.content_inset = s_layout.margin;
    dialog.button_height = clamp_i16(s_layout.row_height * 3 / 4, 38, 52);
    dialog.button_gap = s_layout.margin;
    dialog.show_secondary = s.dialog_view != FT_DIALOG_VIEW_MESSAGE;
    return fui_component_dialog_button_rect(&dialog, primary);
}

static const char *dialog_title(void)
{
    switch (s.dialog_action)
    {
    case FT_DIALOG_ACTION_FORMAT_FLASH:
        return tr("FORMAT INTERNAL FLASH", "格式化内部 Flash");
    case FT_DIALOG_ACTION_FORMAT_SD:
        return tr("FORMAT SD CARD", "格式化 SD 卡");
    case FT_DIALOG_ACTION_DELETE_GALLERY:
        return tr("DELETE PHOTO", "删除照片");
    case FT_DIALOG_ACTION_DELETE_FILE:
        return tr("DELETE FILE OR FOLDER", "删除文件或文件夹");
    case FT_DIALOG_ACTION_RECORDER_START:
        return tr("RECORDING COULD NOT START", "无法开始录音");
    case FT_DIALOG_ACTION_RECORDER_STOP:
        return tr("RECORDING COULD NOT STOP", "无法停止录音");
    case FT_DIALOG_ACTION_RECORDER_DEVICE:
        return tr("INPUT DEVICE UNAVAILABLE", "输入设备不可用");
    case FT_DIALOG_ACTION_FILE_PASTE:
        return tr("PASTE FAILED", "粘贴失败");
    case FT_DIALOG_ACTION_FILE_RENAME:
        return tr("RENAME FAILED", "重命名失败");
    case FT_DIALOG_ACTION_FILE_CREATE:
        return tr("CREATE FOLDER FAILED", "新建文件夹失败");
    default:
        return s.dialog_success ? tr("COMPLETED", "操作完成") :
                                  tr("OPERATION FAILED", "操作失败");
    }
}

static const char *dialog_detail(void)
{
    if (s.dialog_view == FT_DIALOG_VIEW_FINAL_CONFIRM)
        return tr("ALL DATA WILL BE ERASED. THIS CANNOT BE UNDONE.",
                  "全部数据将被清除，而且无法撤销。");
    if (s.dialog_view == FT_DIALOG_VIEW_CONFIRM &&
        (s.dialog_action == FT_DIALOG_ACTION_FORMAT_FLASH ||
         s.dialog_action == FT_DIALOG_ACTION_FORMAT_SD))
        return tr("FILES AND PARTITIONS ON THIS DEVICE WILL BE REMOVED.",
                  "这个设备上的文件和分区将被删除。");
    if (s.dialog_view == FT_DIALOG_VIEW_CONFIRM)
        return tr("THE SELECTED ITEM WILL BE PERMANENTLY REMOVED.",
                  "所选项目将被永久删除。");
    return s.dialog_success ? tr("THE OPERATION COMPLETED SUCCESSFULLY.",
                                 "操作已经成功完成。") :
                              tr("NO CHANGES WERE APPLIED.",
                                 "没有应用任何更改。");
}

static const char *dialog_primary_label(void)
{
    if (s.dialog_view == FT_DIALOG_VIEW_MESSAGE)
        return tr("OK", "确定");
    if (s.dialog_view == FT_DIALOG_VIEW_FINAL_CONFIRM)
        return tr("ERASE", "清除");
    if (s.dialog_action == FT_DIALOG_ACTION_FORMAT_FLASH ||
        s.dialog_action == FT_DIALOG_ACTION_FORMAT_SD)
        return tr("CONTINUE", "继续");
    return tr("DELETE", "删除");
}

static void dialog_hide(void)
{
    s.dialog_visible = false;
    s.dialog_action = FT_DIALOG_ACTION_NONE;
    s.dialog_target[0] = '\0';
    s.dialog_result = RT_EOK;
}

static void dialog_show_confirm(ft_dialog_action_t action, const char *target)
{
    s.dialog_visible = true;
    s.dialog_success = false;
    s.dialog_view = FT_DIALOG_VIEW_CONFIRM;
    s.dialog_action = action;
    s.dialog_result = RT_EOK;
    if (target == RT_NULL) target = "";
    rt_strncpy(s.dialog_target, target, sizeof(s.dialog_target) - 1U);
    s.dialog_target[sizeof(s.dialog_target) - 1U] = '\0';
}

static void dialog_show_message(ft_dialog_action_t action, int result,
                                bool success)
{
    s.dialog_visible = true;
    s.dialog_success = success;
    s.dialog_view = FT_DIALOG_VIEW_MESSAGE;
    s.dialog_action = action;
    s.dialog_result = result;
}

static void draw_dialog(fui_painter_t *p)
{
    fui_rect_t panel = dialog_panel_rect();
    fui_component_style_t style = component_style();
    char result[32];
    fui_dialog_t dialog;

    fui_painter_rect(p, (fui_rect_t){0, 0, FT_SCREEN_W, FT_SCREEN_H}, 0,
                     FUI_ARGB(0xb8, 0x00, 0x00, 0x00));
    memset(&dialog, 0, sizeof(dialog));
    dialog.bounds = panel;
    dialog.title = dialog_title();
    dialog.detail = dialog_detail();
    dialog.target = s.dialog_target;
    dialog.primary_label = dialog_primary_label();
    dialog.secondary_label = tr("CANCEL", "取消");
    dialog.state = FUI_COMPONENT_STATE_DEFAULT;
    dialog.title_scale = 3U;
    dialog.text_scale = 1U;
    dialog.button_scale = 2U;
    dialog.content_inset = s_layout.margin;
    dialog.button_height = clamp_i16(s_layout.row_height * 3 / 4, 38, 52);
    dialog.button_gap = s_layout.margin;
    dialog.show_secondary = s.dialog_view != FT_DIALOG_VIEW_MESSAGE;
    dialog.destructive = s.dialog_view != FT_DIALOG_VIEW_MESSAGE;
    if (s.dialog_view == FT_DIALOG_VIEW_MESSAGE && s.dialog_result != RT_EOK)
    {
        snprintf(result, sizeof(result), tr("ERROR %d", "错误 %d"),
                 s.dialog_result);
        dialog.status = result;
        dialog.status_danger = true;
    }
    style.surface = C_PANEL;
    style.surface_alt = C_PANEL_2;
    style.radius = 8U;
    (void)fui_component_dialog(p, &dialog, &style);
}

static void draw_page_scrollbar(fui_painter_t *p)
{
    int16_t viewport = (int16_t)(FT_NAV_Y - s_layout.margin - FT_LIST_TOP);
    fui_component_style_t style;
    fui_scrollbar_t scrollbar;
    if (s.scroll_limit >= 0 || viewport <= 0) return;
    style = component_style();
    style.track = FUI_ARGB(0x70, 0x30, 0x30, 0x30);
    memset(&scrollbar, 0, sizeof(scrollbar));
    scrollbar.bounds = (fui_rect_t){(int16_t)(FT_SCREEN_W - 6), FT_LIST_TOP,
                                    4, viewport};
    scrollbar.viewport_extent = viewport;
    scrollbar.content_extent = viewport - s.scroll_limit;
    scrollbar.offset = -s.scroll_y;
    scrollbar.minimum_thumb = clamp_i16(viewport / 8, 24, 48);
    scrollbar.state = s.scrolling ? FUI_COMPONENT_STATE_PRESSED :
                                    FUI_COMPONENT_STATE_DEFAULT;
    (void)fui_component_scrollbar(p, &scrollbar, &style);
}

void ft_gpu_scene_collect(fui_painter_t *p, void *user_data)
{
    ft_gpu_page_t page = s.route[s.depth - 1U];
    ft_gpu_image_info_t wallpaper;
    (void)user_data;
    fui_painter_clear(p, s.prefs.background == FT_BACKGROUND_ACCENT ?
                         accent() : C_BG);
    if (s.prefs.background == FT_BACKGROUND_WALLPAPER &&
        ft_gpu_image_get(FT_GPU_IMAGE_WALLPAPER, &wallpaper) &&
        wallpaper.state == FT_GPU_IMAGE_READY)
    {
        int16_t x = (int16_t)((FT_SCREEN_W - wallpaper.image.width) / 2);
        int16_t y = (int16_t)(FT_CONTENT_Y +
                    (FT_CONTENT_H - wallpaper.image.height) / 2);
        fui_painter_set_clip(p, (fui_rect_t){0, FT_CONTENT_Y,
                                             FT_SCREEN_W, FT_CONTENT_H});
        fui_painter_image_rgb565(p, x, y, &wallpaper.image);
        fui_painter_reset_clip(p);
    }
    draw_status(p);
    fui_painter_set_clip(p, (fui_rect_t){0, FT_CONTENT_Y, FT_SCREEN_W, FT_CONTENT_H});
    switch (page)
    {
    case FT_GPU_PAGE_HOME: draw_home(p); break;
    case FT_GPU_PAGE_SEARCH: draw_search(p); break;
    case FT_GPU_PAGE_SETTINGS: draw_settings(p); break;
    case FT_GPU_PAGE_SETTINGS_DISPLAY: draw_display(p); break;
    case FT_GPU_PAGE_SETTINGS_AUDIO: draw_audio(p); break;
    case FT_GPU_PAGE_SETTINGS_WIFI: draw_radio_page(p, false); break;
    case FT_GPU_PAGE_SETTINGS_BLUETOOTH: draw_radio_page(p, true); break;
    case FT_GPU_PAGE_SETTINGS_STORAGE: draw_storage(p); break;
    case FT_GPU_PAGE_SETTINGS_USB: draw_usb(p); break;
    case FT_GPU_PAGE_SETTINGS_TIME_LANGUAGE: draw_time_language(p); break;
    case FT_GPU_PAGE_SETTINGS_PERSONALIZATION: draw_personalization(p); break;
    case FT_GPU_PAGE_SYSTEM: draw_system(p); break;
    case FT_GPU_PAGE_ABOUT: draw_about(p); break;
    case FT_GPU_PAGE_MEDIA: draw_media(p); break;
    case FT_GPU_PAGE_RECORDER: draw_recorder(p); break;
    case FT_GPU_PAGE_FILES: draw_files(p, false); break;
    case FT_GPU_PAGE_GALLERY: draw_files(p, true); break;
    default: break;
    }
    draw_page_scrollbar(p);
    fui_painter_reset_clip(p);
    draw_nav(p);
    if (s.keyboard_visible) draw_keyboard(p);
    if (s.shade_visible) draw_shade(p);
    if (s.toast_visible) draw_toast(p);
    if (s.select_kind != FT_SELECT_NONE) draw_select_overlay(p);
    if (s.dialog_visible) draw_dialog(p);
}

static void preferences_save(void)
{
    (void)ft_preferences_store_update(&s.prefs);
}

static void refresh_services(void)
{
    s.quick_valid = feathertalk_ipc_get_quick_status(&s.quick) == RT_EOK;
    (void)ft_audio_get_status(&s.audio);
    (void)ft_recorder_get_status(&s.recorder);
    (void)ft_recorder_get_devices(s.recorder_devices, FT_RECORDER_DEVICE_COUNT,
                                  &s.recorder_device_count);
    ft_usb_refresh();
    ft_usb_get_status(&s.usb);
    (void)ft_storage_get_flash_info(&s.flash);
    (void)ft_storage_get_device_info(&s.sd);
}

static void reset_page_state(void)
{
    s.scroll_y = 0;
    s.scroll_limit = 0;
    s.scrolling = false;
    s.file_menu = false;
    s.root_format_menu = false;
    s.gallery_viewer = false;
    s.file_selected = -1;
    s.keyboard_visible = false;
    select_close();
}

ft_gpu_page_t ft_gpu_scene_current_page(void)
{
    return s.route[s.depth - 1U];
}

uint8_t ft_gpu_scene_route_depth(void)
{
    return s.depth;
}

bool ft_gpu_scene_select_visible(void)
{
    return s.select_kind != FT_SELECT_NONE;
}

bool ft_gpu_scene_dialog_visible(void)
{
    return s.dialog_visible;
}

static int scene_leave_page(ft_gpu_page_t page)
{
    int result;
    if (page != FT_GPU_PAGE_RECORDER) return RT_EOK;
    (void)ft_recorder_get_status(&s.recorder);
    if (s.recorder.state != FT_RECORDER_STARTING &&
        s.recorder.state != FT_RECORDER_RECORDING)
        return RT_EOK;
    result = ft_recorder_stop();
    (void)ft_recorder_get_status(&s.recorder);
    if (result != RT_EOK)
    {
        dialog_show_message(FT_DIALOG_ACTION_RECORDER_STOP, result, false);
        fui_engine_invalidate();
    }
    return result;
}

int ft_gpu_scene_open(ft_gpu_page_t page)
{
    ft_gpu_page_t current;
    int leave_result;
    if (page >= FT_GPU_PAGE_COUNT) return -RT_EINVAL;
    current = s.depth == 0U ? FT_GPU_PAGE_HOME : ft_gpu_scene_current_page();
    if (s.depth != 0U && page != current)
    {
        leave_result = scene_leave_page(current);
        if (leave_result != RT_EOK) return leave_result;
    }
    if (page != FT_GPU_PAGE_HOME && s.desktop_edit)
    {
        s.desktop_edit = false;
        s.selected_tile = -1;
        s.tile_drag = false;
        s.tile_resize_corner = 0U;
        stop_tile_pulse();
    }
    if (s.depth != 0U && ft_gpu_scene_current_page() == FT_GPU_PAGE_GALLERY &&
        s.gallery_viewer && page != FT_GPU_PAGE_GALLERY)
        gallery_restore_wallpaper();
    if (page == FT_GPU_PAGE_HOME)
    {
        s.depth = 1U;
        s.route[0] = FT_GPU_PAGE_HOME;
    }
    else if (s.route[s.depth - 1U] != page)
    {
        if (s.depth == FT_ROUTE_DEPTH)
            memmove(&s.route[1], &s.route[2], (FT_ROUTE_DEPTH - 2U) * sizeof(s.route[0]));
        else s.depth++;
        s.route[s.depth - 1U] = page;
    }
    reset_page_state();
    if (page == FT_GPU_PAGE_GALLERY)
        gallery_set_source(false);
    else if (page == FT_GPU_PAGE_FILES)
        reload_files();
    fui_engine_invalidate();
    return RT_EOK;
}

static void route_back(void)
{
    ft_gpu_page_t page = ft_gpu_scene_current_page();
    if (page == FT_GPU_PAGE_GALLERY && s.gallery_viewer)
    {
        s.gallery_viewer = false;
        ft_gpu_image_clear(FT_GPU_IMAGE_GALLERY);
        gallery_restore_wallpaper();
        return;
    }
    if ((page == FT_GPU_PAGE_FILES || page == FT_GPU_PAGE_GALLERY) &&
        strcmp(s.file_path, "/") != 0)
    {
        const char *mount;
        if (page == FT_GPU_PAGE_GALLERY)
            mount = strncmp(s.file_path, "/flash", 6U) == 0 ?
                    "/flash/Pictures" : "/sdcard/Pictures";
        else
            mount = strncmp(s.file_path, "/flash", 6U) == 0 ?
                    "/flash" : "/sdcard";
        if (strcmp(s.file_path, mount) == 0)
        {
            if (s.depth > 1U) s.depth--;
            reset_page_state();
            return;
        }
        if (!ft_storage_parent_path(s.file_path, mount) && page == FT_GPU_PAGE_FILES)
            strcpy(s.file_path, "/");
        reload_files();
        reset_page_state();
        return;
    }
    if (s.depth > 1U)
    {
        if (scene_leave_page(page) != RT_EOK) return;
        s.depth--;
    }
    reset_page_state();
}

static int visible_file_index(uint8_t row, bool gallery)
{
    uint8_t i, visible = 0U;
    for (i = 0U; i < s.file_count; i++)
    {
        if (gallery && s.files[i].type != FT_STORAGE_ENTRY_DIRECTORY &&
            !image_name(s.files[i].name)) continue;
        if (visible == row) return i;
        visible++;
    }
    return -1;
}

static int row_at(int16_t y)
{
    int32_t local = y - FT_LIST_TOP - s.scroll_y;
    if (local < 0) return -1;
    if ((local % (FT_ROW_H + FT_ROW_GAP)) >= FT_ROW_H) return -1;
    return (int)(local / (FT_ROW_H + FT_ROW_GAP));
}

static void open_file_index(int index, bool gallery)
{
    char path[FT_STORAGE_PATH_MAX];
    if (index < 0 || index >= s.file_count) return;
    if (strcmp(s.file_path, "/") == 0)
    {
        if (strcmp(s.files[index].name, "flash") == 0) strcpy(path, "/flash");
        else strcpy(path, "/sdcard");
    }
    else if (ft_storage_join_path(s.file_path, s.files[index].name,
                                  path, sizeof(path)) != RT_EOK) return;
    if (s.files[index].type == FT_STORAGE_ENTRY_DIRECTORY)
    {
        strncpy(s.file_path, path, sizeof(s.file_path) - 1U);
        s.file_path[sizeof(s.file_path) - 1U] = '\0';
        reload_files();
        s.scroll_y = 0;
        s.file_selected = -1;
    }
    else if (gallery || image_name(s.files[index].name))
    {
        if (!gallery)
        {
            char directory[FT_STORAGE_PATH_MAX];
            char name[FT_STORAGE_NAME_MAX];
            uint8_t i;
            rt_strncpy(directory, s.file_path, sizeof(directory) - 1U);
            directory[sizeof(directory) - 1U] = '\0';
            rt_strncpy(name, s.files[index].name, sizeof(name) - 1U);
            name[sizeof(name) - 1U] = '\0';
            (void)ft_gpu_scene_open(FT_GPU_PAGE_GALLERY);
            rt_strncpy(s.file_path, directory, sizeof(s.file_path) - 1U);
            s.file_path[sizeof(s.file_path) - 1U] = '\0';
            reload_files();
            for (i = 0U; i < s.file_count; i++)
                if (strcmp(s.files[i].name, name) == 0)
                {
                    open_file_index(i, true);
                    break;
                }
            return;
        }
        s.file_selected = (int8_t)index;
        s.gallery_viewer = true;
        {
            fui_rect_t bounds = gallery_image_bounds();
            (void)ft_gpu_image_request(FT_GPU_IMAGE_GALLERY, path,
                                       (uint16_t)bounds.width,
                                       (uint16_t)bounds.height);
        }
    }
}

static void gallery_set_source(bool sd)
{
    char created[FT_STORAGE_PATH_MAX];
    const char *mount = sd ? "/sdcard" : "/flash";
    const ft_storage_device_info_t *device = sd ? &s.sd : &s.flash;
    if (device->mounted)
        (void)ft_storage_create_directory(mount, "Pictures", created,
                                          sizeof(created));
    rt_snprintf(s.file_path, sizeof(s.file_path), "%s/Pictures", mount);
    s.file_path[sizeof(s.file_path) - 1U] = '\0';
    reload_files();
    s.scroll_y = 0;
    s.file_selected = -1;
    s.gallery_viewer = false;
    ft_gpu_image_clear(FT_GPU_IMAGE_GALLERY);
}

static void gallery_restore_wallpaper(void)
{
    ft_gpu_image_clear(FT_GPU_IMAGE_GALLERY);
    if (s.prefs.background == FT_BACKGROUND_WALLPAPER &&
        s.prefs.wallpaper_path[0] != '\0')
        (void)ft_gpu_image_request(FT_GPU_IMAGE_WALLPAPER,
                                   s.prefs.wallpaper_path,
                                   FT_SCREEN_W, FT_CONTENT_H);
}

static void gallery_select_relative(int direction)
{
    int start;
    int step;
    int index;
    unsigned attempts;
    if (s.file_count == 0U) return;
    start = s.file_selected >= 0 ? s.file_selected : 0;
    step = direction < 0 ? -1 : 1;
    index = start;
    for (attempts = 0U; attempts < s.file_count; attempts++)
    {
        index += step;
        if (index < 0) index = s.file_count - 1;
        if (index >= s.file_count) index = 0;
        if (s.files[index].type != FT_STORAGE_ENTRY_DIRECTORY &&
            image_name(s.files[index].name))
        {
            open_file_index(index, true);
            return;
        }
    }
}

static void gallery_viewer_tap(int16_t x, int16_t y)
{
    char path[FT_STORAGE_PATH_MAX];
    fui_rect_t actions[FT_GALLERY_ACTION_COUNT];
    fui_rect_t delete_button = gallery_delete_rect();
    uint8_t i;
    if (s.file_selected < 0 || s.file_selected >= s.file_count) return;
    for (i = 0U; i < FT_GALLERY_ACTION_COUNT; i++)
        actions[i] = gallery_action_rect(i);
    if (fui_rect_contains(&actions[FT_GALLERY_ACTION_PREVIOUS], x, y))
        gallery_select_relative(-1);
    else if (fui_rect_contains(&actions[FT_GALLERY_ACTION_NEXT], x, y))
        gallery_select_relative(1);
    else if (fui_rect_contains(&actions[FT_GALLERY_ACTION_WALLPAPER], x, y))
    {
        if (ft_storage_join_path(s.file_path,
                                 s.files[s.file_selected].name,
                                 path, sizeof(path)) == RT_EOK)
        {
            rt_strncpy(s.prefs.wallpaper_path, path,
                       sizeof(s.prefs.wallpaper_path) - 1U);
            s.prefs.wallpaper_path[sizeof(s.prefs.wallpaper_path) - 1U] = '\0';
            s.prefs.background = FT_BACKGROUND_WALLPAPER;
            preferences_save();
        }
    }
    else if (fui_rect_contains(&actions[FT_GALLERY_ACTION_CLOSE], x, y))
    {
        s.gallery_viewer = false;
        gallery_restore_wallpaper();
    }
    else if (fui_rect_contains(&delete_button, x, y))
    {
        if (ft_storage_join_path(s.file_path,
                                 s.files[s.file_selected].name,
                                 path, sizeof(path)) == RT_EOK)
            dialog_show_confirm(FT_DIALOG_ACTION_DELETE_GALLERY, path);
    }
}

static void dialog_execute_primary(void)
{
    ft_dialog_action_t action = s.dialog_action;
    int result = RT_EOK;

    if (s.dialog_view == FT_DIALOG_VIEW_MESSAGE)
    {
        dialog_hide();
        return;
    }
    if (s.dialog_view == FT_DIALOG_VIEW_CONFIRM &&
        (action == FT_DIALOG_ACTION_FORMAT_FLASH ||
         action == FT_DIALOG_ACTION_FORMAT_SD))
    {
        s.dialog_view = FT_DIALOG_VIEW_FINAL_CONFIRM;
        return;
    }

    switch (action)
    {
    case FT_DIALOG_ACTION_FORMAT_FLASH:
        result = ft_storage_format_flash();
        break;
    case FT_DIALOG_ACTION_FORMAT_SD:
        result = ft_storage_format_sd();
        break;
    case FT_DIALOG_ACTION_DELETE_FILE:
    case FT_DIALOG_ACTION_DELETE_GALLERY:
        result = ft_storage_delete_path(s.dialog_target);
        break;
    default:
        result = -RT_EINVAL;
        break;
    }

    if (result != RT_EOK)
    {
        dialog_show_message(action, result, false);
        return;
    }

    if (action == FT_DIALOG_ACTION_FORMAT_FLASH ||
        action == FT_DIALOG_ACTION_FORMAT_SD)
    {
        refresh_services();
        if (ft_gpu_scene_current_page() == FT_GPU_PAGE_FILES)
            reload_files();
        dialog_show_message(action, RT_EOK, true);
        return;
    }

    if (action == FT_DIALOG_ACTION_DELETE_GALLERY)
    {
        if (strcmp(s.dialog_target, s.prefs.wallpaper_path) == 0)
        {
            s.prefs.wallpaper_path[0] = '\0';
            if (s.prefs.background == FT_BACKGROUND_WALLPAPER)
                s.prefs.background = FT_BACKGROUND_DARK;
            preferences_save();
            ft_gpu_image_clear(FT_GPU_IMAGE_WALLPAPER);
        }
        s.gallery_viewer = false;
        ft_gpu_image_clear(FT_GPU_IMAGE_GALLERY);
        reload_files();
        gallery_restore_wallpaper();
    }
    else reload_files();
    dialog_hide();
}

static bool dialog_handle_event(const fui_event_t *event)
{
    fui_rect_t cancel;
    fui_rect_t primary;
    if (!s.dialog_visible) return false;
    if (event->type != FUI_EVENT_TAP) return true;

    cancel = dialog_button_rect(false);
    primary = dialog_button_rect(true);
    if (s.dialog_view != FT_DIALOG_VIEW_MESSAGE &&
        fui_rect_contains(&cancel, event->x, event->y))
        dialog_hide();
    else if (fui_rect_contains(&primary, event->x, event->y))
        dialog_execute_primary();
    else if (event->y >= FT_NAV_Y)
        dialog_hide();
    return true;
}

static void keyboard_open(ft_keyboard_mode_t mode, const char *initial)
{
    s.keyboard_mode = mode;
    s.keyboard_visible = true;
    if (initial == RT_NULL) initial = "";
    strncpy(s.keyboard_input, initial, sizeof(s.keyboard_input) - 1U);
    s.keyboard_input[sizeof(s.keyboard_input) - 1U] = '\0';
    s.keyboard_length = (uint8_t)strlen(s.keyboard_input);
}

static void keyboard_sync_search(void)
{
    if (s.keyboard_mode != FT_KEYBOARD_SEARCH) return;
    strncpy(s.search_text, s.keyboard_input, sizeof(s.search_text) - 1U);
    s.search_text[sizeof(s.search_text) - 1U] = '\0';
    s.scroll_y = 0;
}

static void keyboard_commit(void)
{
    char source[FT_STORAGE_PATH_MAX];
    char result[FT_STORAGE_PATH_MAX];
    int operation_result = RT_EOK;
    ft_dialog_action_t action = FT_DIALOG_ACTION_NONE;
    if (s.keyboard_mode == FT_KEYBOARD_RENAME && s.file_selected >= 0 &&
        s.file_selected < s.file_count && s.keyboard_input[0] != '\0' &&
        ft_storage_join_path(s.file_path, s.files[s.file_selected].name,
                             source, sizeof(source)) == RT_EOK)
    {
        action = FT_DIALOG_ACTION_FILE_RENAME;
        operation_result = ft_storage_rename_path(source, s.keyboard_input,
                                                   result, sizeof(result));
    }
    else if (s.keyboard_mode == FT_KEYBOARD_NEW_FOLDER &&
             s.keyboard_input[0] != '\0')
    {
        action = FT_DIALOG_ACTION_FILE_CREATE;
        operation_result = ft_storage_create_directory(s.file_path,
                                                        s.keyboard_input,
                                                        result,
                                                        sizeof(result));
    }
    keyboard_sync_search();
    s.keyboard_visible = false;
    if (s.keyboard_mode != FT_KEYBOARD_SEARCH) reload_files();
    if (operation_result != RT_EOK)
        dialog_show_message(action, operation_result, false);
}

static bool keyboard_tap(int16_t x, int16_t y)
{
    static const char *rows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static const int16_t starts[] = {10, 32, 78};
    static const int16_t y_offsets[] = {58, 108, 158};
    int row;
    if (!s.keyboard_visible || y < s_layout.keyboard_y || y >= FT_NAV_Y) return false;
    for (row = 0; row < 3; row++)
    {
        int key;
        for (key = 0; key < (int)strlen(rows[row]); key++)
        {
            fui_rect_t rect =
            {
                keyboard_x((int16_t)(starts[row] + key * 46)),
                keyboard_y(y_offsets[row]), keyboard_width(42),
                (int16_t)(keyboard_y(y_offsets[row] + 42) -
                          keyboard_y(y_offsets[row]))
            };
            if (!fui_rect_contains(&rect, x, y)) continue;
            if (s.keyboard_length + 1U < sizeof(s.keyboard_input))
            {
                s.keyboard_input[s.keyboard_length++] = rows[row][key];
                s.keyboard_input[s.keyboard_length] = '\0';
                keyboard_sync_search();
            }
            return true;
        }
    }
    if (y >= keyboard_y(212) && y < keyboard_y(254))
    {
        if (x >= keyboard_x(20) && x < keyboard_x(238) &&
            s.keyboard_length + 1U < sizeof(s.keyboard_input))
        {
            s.keyboard_input[s.keyboard_length++] = ' ';
            s.keyboard_input[s.keyboard_length] = '\0';
            keyboard_sync_search();
        }
        else if (x >= keyboard_x(248) && x < keyboard_x(310) &&
                 s.keyboard_length > 0U)
        {
            s.keyboard_input[--s.keyboard_length] = '\0';
            keyboard_sync_search();
        }
        else if (x >= keyboard_x(320) && x < keyboard_x(382)) keyboard_commit();
        else if (x >= keyboard_x(392) && x < keyboard_x(460))
            s.keyboard_visible = false;
        return true;
    }
    return true;
}

static void handle_file_menu_tap(int16_t x, int16_t y, bool gallery)
{
    ft_file_menu_model_t model;
    int action;
    char source[FT_STORAGE_PATH_MAX];
    char result[FT_STORAGE_PATH_MAX];
    const ft_storage_entry_t *entry;
    if (s.file_selected < 0 || s.file_selected >= s.file_count)
    {
        s.file_menu = false;
        return;
    }
    if (!file_menu_model_build(&model, s.root_format_menu))
    {
        s.file_menu = false;
        return;
    }
    action = fui_component_context_menu_index_from_point(&model.menu, x, y);
    if (s.root_format_menu)
    {
        if (action == 1)
        {
            if (s.file_selected == 0 && s.flash.can_format && !s.flash.usb_exported)
                dialog_show_confirm(FT_DIALOG_ACTION_FORMAT_FLASH,
                                    tr("INTERNAL FLASH", "内部 Flash"));
            else if (s.file_selected == 1 && s.sd.can_format && !s.sd.usb_exported)
                dialog_show_confirm(FT_DIALOG_ACTION_FORMAT_SD,
                                    tr("SD CARD", "SD 卡"));
        }
        s.file_menu = false;
        s.root_format_menu = false;
        reload_files();
        return;
    }
    entry = &s.files[s.file_selected];
    if (ft_storage_join_path(s.file_path, entry->name, source, sizeof(source)) != RT_EOK)
    {
        s.file_menu = false;
        return;
    }
    switch (action)
    {
    case FT_FILE_ACTION_OPEN:
        s.file_menu = false;
        open_file_index(s.file_selected, gallery);
        return;
    case FT_FILE_ACTION_CUT:
        strncpy(s.clipboard_path, source, sizeof(s.clipboard_path) - 1U);
        s.clipboard_path[sizeof(s.clipboard_path) - 1U] = '\0';
        s.clipboard_move = true;
        toast_show(tr("READY TO MOVE", "已准备移动"),
                   rt_tick_get_millisecond());
        break;
    case FT_FILE_ACTION_COPY:
        strncpy(s.clipboard_path, source, sizeof(s.clipboard_path) - 1U);
        s.clipboard_path[sizeof(s.clipboard_path) - 1U] = '\0';
        s.clipboard_move = false;
        toast_show(tr("COPIED", "已复制"), rt_tick_get_millisecond());
        break;
    case FT_FILE_ACTION_PASTE:
        if (s.clipboard_path[0] != '\0')
        {
            int paste_result = ft_storage_paste_path(s.clipboard_path,
                                                      s.file_path,
                                                      s.clipboard_move,
                                                      result,
                                                      sizeof(result));
            if (paste_result == RT_EOK)
            {
                if (s.clipboard_move) s.clipboard_path[0] = '\0';
                toast_show(tr("PASTED", "已粘贴"),
                           rt_tick_get_millisecond());
            }
            else dialog_show_message(FT_DIALOG_ACTION_FILE_PASTE,
                                     paste_result, false);
        }
        break;
    case FT_FILE_ACTION_RENAME:
        keyboard_open(FT_KEYBOARD_RENAME, entry->name);
        s.file_menu = false;
        return;
    case FT_FILE_ACTION_DELETE:
        dialog_show_confirm(gallery ? FT_DIALOG_ACTION_DELETE_GALLERY :
                                      FT_DIALOG_ACTION_DELETE_FILE,
                            source);
        break;
    case FT_FILE_ACTION_NEW_FOLDER:
        keyboard_open(FT_KEYBOARD_NEW_FOLDER, "NEW_FOLDER");
        s.file_menu = false;
        return;
    default: break;
    }
    s.file_menu = false;
    reload_files();
}

static bool handle_shade(const fui_event_t *event)
{
    if (event->type == FUI_EVENT_TOUCH_DOWN &&
        (s.shade_visible || event->y <= FT_STATUS_H + s_layout.touch_slop))
    {
        fui_animation_cancel(&s, FT_ANIMATION_SHADE_Y);
        s.shade_drag_offset = s.shade_visible ?
                              (int16_t)(event->y - s.shade_y) : FT_SHADE_H;
        s.shade_visible = true;
        s.shade_drag = false;
        s.shade_pointer_down = true;
        s.shade_y = (int16_t)(event->y - s.shade_drag_offset);
        s.shade_target_y = s.shade_y;
        s.touch_down_x = event->x;
        s.touch_down_y = event->y;
        s.shade_drag_start_ms = event->timestamp_ms;
        return true;
    }
    if (!s.shade_visible) return false;
    if (event->type == FUI_EVENT_TOUCH_MOVE && s.shade_pointer_down)
    {
        int16_t dx = (int16_t)(event->x - s.touch_down_x);
        int16_t dy = (int16_t)(event->y - s.touch_down_y);
        if (!s.shade_drag && abs_i32(dy) > s_layout.touch_slop &&
            abs_i32(dy) > abs_i32(dx))
            s.shade_drag = true;
        if (!s.shade_drag) return true;
        int16_t y = (int16_t)(event->y - s.shade_drag_offset);
        if (y > FT_STATUS_H) y = FT_STATUS_H;
        if (y < -FT_SHADE_H) y = -FT_SHADE_H;
        s.shade_y = y;
        return true;
    }
    if (event->type == FUI_EVENT_TOUCH_UP && s.shade_pointer_down)
    {
        int16_t dx = (int16_t)(event->x - s.touch_down_x);
        int16_t dy = (int16_t)(event->y - s.touch_down_y);
        uint32_t elapsed_ms = event->timestamp_ms - s.shade_drag_start_ms;
        int32_t velocity_y = elapsed_ms == 0U ? 0 :
            ((int32_t)dy * 1000) / (int32_t)elapsed_ms;
        int32_t velocity_threshold = FT_SCREEN_H * 3 / 4;
        bool dragged = s.shade_drag;
        s.shade_pointer_down = false;
        s.shade_drag = false;
        if (dragged)
        {
            int16_t target = velocity_y > velocity_threshold ? FT_STATUS_H :
                velocity_y < -velocity_threshold ? -FT_SHADE_H :
                (s.shade_y > -FT_SHADE_H / 2 ||
                 dy > s_layout.shade_release_distance) ?
                FT_STATUS_H : -FT_SHADE_H;
            start_shade_settle(target, event->timestamp_ms);
            return true;
        }
        if (abs_i32(dx) > s_layout.horizontal_swipe &&
            abs_i32(dx) > abs_i32(dy))
        {
            int16_t row_step = shade_offset(FT_SHADE_NOTIFICATION_STEP);
            int row = (event->y - (s.shade_y +
                      shade_offset(FT_SHADE_NOTIFICATION_TOP))) / row_step;
            ft_notification_t n;
            if (row >= 0 && row < shade_notification_capacity() &&
                ft_notifications_get((size_t)row, &n))
                (void)ft_notifications_remove(n.id);
            return true;
        }
        return false; /* Let the following TAP activate a shade control. */
    }
    if (event->type != FUI_EVENT_TAP) return false;
    if (event->y >= s.shade_y + shade_offset(58) &&
        event->y < s.shade_y + shade_offset(152))
    {
        int16_t gap = s_layout.tile_gap;
        int16_t card_width = (int16_t)((FT_ROW_W - 3 * gap) / 4);
        int column = (event->x - FT_ROW_X) / (card_width + gap);
        uint8_t control = column == 0 ? FEATHERTALK_QUICK_WIFI :
                          column == 1 ? FEATHERTALK_QUICK_BLUETOOTH :
                          column == 3 ? FEATHERTALK_QUICK_ROTATION : 0xffU;
        if (column == 2)
        {
            uint8_t value = s.brightness >= 90U ? 20U :
                            (uint8_t)(s.brightness + 20U);
            (void)set_brightness(value);
        }
        else if (control != 0xffU)
        {
            uint8_t mask = (uint8_t)(1U << control);
            if (s.quick_valid && (s.quick.capabilities & mask))
                (void)feathertalk_ipc_set_quick_control(control,
                    (s.quick.enabled & mask) ? 0U : 1U);
        }
        return true;
    }
    if (event->y >= s.shade_y + shade_offset(230) &&
        event->x > FT_SCREEN_W * 3 / 4)
    {
        ft_notifications_clear();
        return true;
    }
    start_shade_settle(-FT_SHADE_H, event->timestamp_ms);
    return true;
}

static void handle_settings_row(int row)
{
    if (row >= 0 && row < (int)FT_SETTING_COUNT)
        (void)ft_gpu_scene_open(s_settings[row].page);
}

static int filtered_app_index(int row)
{
    int visible = 0;
    size_t i;
    for (i = 0U; i < FT_APP_COUNT; i++)
        if (text_contains(s_apps[i].name_en, s.search_text) ||
            text_contains(s_apps[i].name_zh, s.search_text))
        {
            if (visible == row) return (int)i;
            visible++;
        }
    return -1;
}

static int filtered_setting_index(int row)
{
    int visible = 0;
    int i;
    for (i = 0; i < FT_SETTING_COUNT; i++)
        if (text_contains(s_settings[i].name_en, s.search_text) ||
            text_contains(s_settings[i].name_zh, s.search_text))
        {
            if (visible == row) return i;
            visible++;
        }
    return -1;
}

static void handle_audio_row(int row, int16_t x)
{
    if (row == FT_AUDIO_ROW_OUTPUT_VOLUME)
    {
        uint8_t value = meter_value_from_x(x, s.audio.output_volume_max);
        if (ft_audio_set_output_volume(value) == RT_EOK)
        {
            s.prefs.audio_output_volume = value;
            preferences_save();
        }
    }
    else if (row == FT_AUDIO_ROW_INPUT_GAIN)
    {
        uint8_t value = meter_value_from_x(x, s.audio.input_gain_max);
        if (ft_audio_set_input_gain(value) == RT_EOK)
        {
            s.prefs.audio_input_gain = value;
            preferences_save();
        }
    }
    else if (row == FT_AUDIO_ROW_SAMPLE_RATE &&
             s.audio.output_sample_rate_count > 0U)
        select_open(FT_SELECT_AUDIO_RATE);
    else if (row == FT_AUDIO_ROW_SAMPLE_DEPTH)
        select_open(FT_SELECT_AUDIO_DEPTH);
    else if (row == FT_AUDIO_ROW_CHANNELS)
        select_open(FT_SELECT_AUDIO_CHANNELS);
    (void)ft_audio_get_status(&s.audio);
}

static void handle_page_tap(const fui_event_t *event)
{
    ft_gpu_page_t page = ft_gpu_scene_current_page();
    int row = row_at(event->y);
    size_t i;
    if (page == FT_GPU_PAGE_HOME)
    {
        if (s.desktop_edit)
        {
            s.desktop_edit = false;
            s.selected_tile = -1;
            s.tile_drag = false;
            s.tile_resize_corner = 0U;
            stop_tile_pulse();
            return;
        }
        for (i = 0U; i < FT_APP_COUNT; i++)
            if (fui_rect_contains(&s_apps[i].rect, event->x, event->y))
            {
                (void)ft_gpu_scene_open(s_apps[i].page);
                return;
            }
    }
    else if (page == FT_GPU_PAGE_SEARCH)
    {
        int index;
        fui_rect_t search = search_box_rect();
        if (fui_rect_contains(&search, event->x, event->y))
        {
            keyboard_open(FT_KEYBOARD_SEARCH, s.search_text);
            return;
        }
        index = filtered_app_index(row);
        if (index >= 0) (void)ft_gpu_scene_open(s_apps[index].page);
    }
    else if (page == FT_GPU_PAGE_SETTINGS)
    {
        fui_rect_t search = search_box_rect();
        if (fui_rect_contains(&search, event->x, event->y))
            keyboard_open(FT_KEYBOARD_SEARCH, s.search_text);
        else handle_settings_row(filtered_setting_index(row));
    }
    else if (page == FT_GPU_PAGE_SETTINGS_DISPLAY)
    {
        if (row == 0 && s.brightness_valid)
        {
            (void)set_brightness(meter_value_from_x(event->x,
                                                     FT_PERCENT_MAX));
        }
        else if (row == 1 && s.quick_valid)
            (void)feathertalk_ipc_set_quick_control(FEATHERTALK_QUICK_ROTATION,
                (s.quick.enabled & FEATHERTALK_QUICK_CAP_ROTATION) ? 0U : 1U);
    }
    else if (page == FT_GPU_PAGE_SETTINGS_AUDIO) handle_audio_row(row, event->x);
    else if (page == FT_GPU_PAGE_SETTINGS_WIFI || page == FT_GPU_PAGE_SETTINGS_BLUETOOTH)
    {
        uint8_t control = page == FT_GPU_PAGE_SETTINGS_WIFI ? FEATHERTALK_QUICK_WIFI :
                                                              FEATHERTALK_QUICK_BLUETOOTH;
        uint8_t mask = (uint8_t)(1U << control);
        if (row == 0 && s.quick_valid && (s.quick.capabilities & mask))
            (void)feathertalk_ipc_set_quick_control(control,
                (s.quick.enabled & mask) ? 0U : 1U);
    }
    else if (page == FT_GPU_PAGE_SETTINGS_STORAGE)
    {
        ft_storage_device_info_t *d;
        if (row == 0)
        {
            fui_option_t options[2] = {
                {"FLASH", FUI_COMPONENT_STATE_DEFAULT},
                {"SD", FUI_COMPONENT_STATE_DEFAULT}
            };
            fui_segmented_control_t selector = {
                .bounds = {FT_ROW_X, row_y(0U), FT_ROW_W, FT_ROW_H},
                .options = options,
                .option_count = 2U,
                .selected_index = s.storage_selected,
                .state = FUI_COMPONENT_STATE_DEFAULT,
                .text_scale = 1U
            };
            int selected = fui_component_segment_index_from_point(&selector,
                                                                    event->x,
                                                                    event->y);
            if (selected >= 0) s.storage_selected = (uint8_t)selected;
        }
        else if (row == 2)
        {
            d = s.storage_selected ? &s.sd : &s.flash;
            if (d->mounted)
            {
                strcpy(s.file_path, s.storage_selected ? "/sdcard" : "/flash");
                (void)ft_gpu_scene_open(FT_GPU_PAGE_FILES);
            }
        }
        else if (row == 3)
        {
            d = s.storage_selected ? &s.sd : &s.flash;
            if (d->can_format && !d->usb_exported)
                dialog_show_confirm(s.storage_selected ?
                                    FT_DIALOG_ACTION_FORMAT_SD :
                                    FT_DIALOG_ACTION_FORMAT_FLASH,
                                    s.storage_selected ? tr("SD CARD", "SD 卡") :
                                                         tr("INTERNAL FLASH", "内部 Flash"));
        }
    }
    else if (page == FT_GPU_PAGE_SETTINGS_USB)
    {
        if (row == FT_USB_ROW_STORAGE && s.usb.storage_supported)
            (void)ft_usb_set_function(FT_USB_FUNCTION_STORAGE);
        else if (row == FT_USB_ROW_AUDIO && s.usb.audio_supported)
            (void)ft_usb_set_function(FT_USB_FUNCTION_AUDIO);
        else if (row >= FT_USB_ROW_OUTPUT_RATE &&
                 row <= FT_USB_ROW_OUTPUT_CHANNELS &&
                 s.usb.function == FT_USB_FUNCTION_AUDIO)
        {
            if (row == FT_USB_ROW_OUTPUT_RATE) select_open(FT_SELECT_USB_RATE);
            else if (row == FT_USB_ROW_OUTPUT_DEPTH) select_open(FT_SELECT_USB_DEPTH);
            else select_open(FT_SELECT_USB_CHANNELS);
        }
        ft_usb_refresh();
        ft_usb_get_status(&s.usb);
    }
    else if (page == FT_GPU_PAGE_SETTINGS_TIME_LANGUAGE)
    {
        if (row == 0) s.prefs.use_24_hour = !s.prefs.use_24_hour;
        else if (row == 1) select_open(FT_SELECT_TIMEZONE);
        else if (row == 2) select_open(FT_SELECT_LANGUAGE);
        if (row == 0) preferences_save();
    }
    else if (page == FT_GPU_PAGE_SETTINGS_PERSONALIZATION)
    {
        if (row == 0)
        {
            uint8_t i;
            for (i = 0U; i < FT_ACCENT_COUNT &&
                         s_accent_palette[i] != s.prefs.accent_rgb; i++) { }
            s.prefs.accent_rgb = s_accent_palette[(i + 1U) % FT_ACCENT_COUNT];
        }
        else if (row == 1)
            s.prefs.tile_opa = s.prefs.tile_opa <= FT_TILE_OPACITY_MIN ?
                               UINT8_MAX :
                               (uint8_t)(s.prefs.tile_opa - FT_TILE_OPACITY_STEP);
        else if (row == 2) select_open(FT_SELECT_BACKGROUND);
        else if (row == 3)
        {
            (void)ft_gpu_scene_open(FT_GPU_PAGE_GALLERY);
            return;
        }
        if (row == 0 || row == 1) preferences_save();
    }
    else if (page == FT_GPU_PAGE_MEDIA)
    {
        fui_rect_t controls = media_controls_rect();
        uint8_t volume_row = media_volume_row();
        if (fui_rect_contains(&controls, event->x, event->y))
        {
            int16_t relative_x = (int16_t)(event->x - controls.x);
            if (relative_x < controls.width / 3)
                s.media_track = s.media_track == 0U ?
                                (uint8_t)(FT_MEDIA_TRACK_COUNT - 1U) :
                                (uint8_t)(s.media_track - 1U);
            else if (relative_x < controls.width * 2 / 3) s.media_state ^= 1U;
            else s.media_track = (uint8_t)((s.media_track + 1U) %
                                            FT_MEDIA_TRACK_COUNT);
        }
        else if (row == volume_row)
        {
            uint8_t value = meter_value_from_x(event->x,
                                                s.audio.output_volume_max);
            if (ft_audio_set_output_volume(value) == RT_EOK)
            {
                s.media_volume = value;
                s.prefs.audio_output_volume = value;
                preferences_save();
            }
        }
    }
    else if (page == FT_GPU_PAGE_RECORDER)
    {
        uint8_t device_rows = recorder_device_rows();
        uint8_t control_row = device_rows;
        uint8_t saved_row = (uint8_t)(device_rows + 2U);
        if (row >= 0 && row < (int)device_rows)
        {
            int result = ft_recorder_select_device((size_t)row);
            if (result != RT_EOK)
                dialog_show_message(FT_DIALOG_ACTION_RECORDER_DEVICE,
                                    result, false);
        }
        else if (row == control_row)
        {
            int result = RT_EOK;
            ft_dialog_action_t action = FT_DIALOG_ACTION_RECORDER_START;
            if (s.recorder.state == FT_RECORDER_STARTING ||
                s.recorder.state == FT_RECORDER_RECORDING)
            {
                action = FT_DIALOG_ACTION_RECORDER_STOP;
                result = ft_recorder_stop();
            }
            else if (ft_recorder_can_start()) result = ft_recorder_start();
            else result = -RT_EBUSY;
            if (result != RT_EOK)
                dialog_show_message(action, result, false);
        }
        else if (row == saved_row)
        {
            strcpy(s.file_path, s.recorder.storage_mount[0] ? s.recorder.storage_mount : "/");
            (void)ft_gpu_scene_open(FT_GPU_PAGE_FILES);
        }
        (void)ft_recorder_get_status(&s.recorder);
    }
    else if (page == FT_GPU_PAGE_FILES || page == FT_GPU_PAGE_GALLERY)
    {
        bool gallery = page == FT_GPU_PAGE_GALLERY;
        fui_rect_t flash_source = gallery_source_rect(false);
        fui_rect_t sd_source = gallery_source_rect(true);
        if (gallery && !s.gallery_viewer &&
            (fui_rect_contains(&flash_source, event->x, event->y) ||
             fui_rect_contains(&sd_source, event->x, event->y)))
            gallery_set_source(fui_rect_contains(&sd_source, event->x, event->y));
        else if (s.gallery_viewer) gallery_viewer_tap(event->x, event->y);
        else if (s.file_menu) handle_file_menu_tap(event->x, event->y, gallery);
        else
        {
            int index = visible_file_index((uint8_t)row, gallery);
            open_file_index(index, gallery);
        }
    }
}

static bool rect_overlap(const fui_rect_t *a, const fui_rect_t *b)
{
    return a->x < b->x + b->width && a->x + a->width > b->x &&
           a->y < b->y + b->height && a->y + a->height > b->y;
}

static int16_t desktop_grid_y(uint8_t row)
{
    return (int16_t)(s_layout.tile_top +
                     row * (s_layout.tile_height + s_layout.tile_gap));
}

static uint8_t desktop_grid_rows(void)
{
    int32_t available = FT_NAV_Y - s_layout.margin - s_layout.tile_top;
    int32_t step = s_layout.tile_height + s_layout.tile_gap;
    int32_t rows = step > 0 ? (available + step - 1) / step : 1;
    if (rows < 1) rows = 1;
    if (rows > 12) rows = 12;
    return (uint8_t)rows;
}

static int16_t desktop_bottom(void)
{
    int16_t clearance = s_layout.tile_gap / 2;
    if (clearance < 2) clearance = 2;
    return (int16_t)(FT_NAV_Y - clearance);
}

static int16_t tile_handle_size(void)
{
    int16_t unit = s_layout.tile_cell_width < s_layout.tile_height ?
                   s_layout.tile_cell_width : s_layout.tile_height;
    return clamp_i16(unit * 2 / 5, 28, 48);
}

static uint8_t tile_span(int16_t size, int16_t unit, uint8_t maximum)
{
    uint8_t span = (uint8_t)((size + unit / 2) / unit);
    if (span < 1U) span = 1U;
    if (span > maximum) span = maximum;
    return span;
}

static bool tile_candidate_free(size_t moving, const fui_rect_t *candidate)
{
    size_t i;
    for (i = 0U; i < FT_APP_COUNT; i++)
        if (i != moving && rect_overlap(candidate, &s_apps[i].rect)) return false;
    return true;
}

static bool rect_equal(const fui_rect_t *a, const fui_rect_t *b)
{
    return a->x == b->x && a->y == b->y &&
           a->width == b->width && a->height == b->height;
}

static void cancel_tile_motion(ft_app_t *tile)
{
    fui_animation_cancel(tile, FT_ANIMATION_TILE_X);
    fui_animation_cancel(tile, FT_ANIMATION_TILE_Y);
    fui_animation_cancel(tile, FT_ANIMATION_TILE_WIDTH);
    fui_animation_cancel(tile, FT_ANIMATION_TILE_HEIGHT);
}

static void start_tile_motion(ft_app_t *tile, const fui_rect_t *target,
                              uint32_t now_ms)
{
    int32_t distance = abs_i32((int32_t)target->x - tile->rect.x);
    int32_t component = abs_i32((int32_t)target->y - tile->rect.y);
    uint32_t duration;
    if (component > distance) distance = component;
    component = abs_i32((int32_t)target->width - tile->rect.width);
    if (component > distance) distance = component;
    component = abs_i32((int32_t)target->height - tile->rect.height);
    if (component > distance) distance = component;
    duration = motion_duration_ms(distance,
                                  FT_SCREEN_W > FT_SCREEN_H ?
                                  FT_SCREEN_W : FT_SCREEN_H,
                                  FT_TILE_SETTLE_MIN_MS,
                                  FT_TILE_SETTLE_MAX_MS);
    start_value_animation(tile, FT_ANIMATION_TILE_X,
                          tile->rect.x, target->x, duration,
                          FUI_EASING_OUT_CUBIC, 0U, false, now_ms);
    start_value_animation(tile, FT_ANIMATION_TILE_Y,
                          tile->rect.y, target->y, duration,
                          FUI_EASING_OUT_CUBIC, 0U, false, now_ms);
    start_value_animation(tile, FT_ANIMATION_TILE_WIDTH,
                          tile->rect.width, target->width, duration,
                          FUI_EASING_OUT_CUBIC, 0U, false, now_ms);
    start_value_animation(tile, FT_ANIMATION_TILE_HEIGHT,
                          tile->rect.height, target->height, duration,
                          FUI_EASING_OUT_CUBIC, 0U, false, now_ms);
}

static void reflow_tile(size_t moving)
{
    ft_app_t *app = &s_apps[moving];
    uint8_t rows = desktop_grid_rows();
    uint8_t col_span = tile_span(app->rect.width + s_layout.tile_gap,
                                 s_layout.tile_cell_width, FT_TILE_COLUMNS);
    uint8_t row_span = tile_span(app->rect.height + s_layout.tile_gap,
                                 s_layout.tile_height + s_layout.tile_gap,
                                 rows);
    fui_rect_t best = app->rect;
    uint32_t best_distance = 0xffffffffUL;
    uint8_t row, col;
    for (row = 0U; row + row_span <= rows; row++)
    {
        for (col = 0U; col + col_span <= FT_TILE_COLUMNS; col++)
        {
            fui_rect_t candidate;
            int32_t dx, dy;
            uint32_t distance;
            candidate.x = (int16_t)(s_layout.margin +
                                     col * s_layout.tile_cell_width);
            candidate.y = desktop_grid_y(row);
            candidate.width = (int16_t)(col_span * s_layout.tile_cell_width -
                                         s_layout.tile_gap);
            candidate.height = (int16_t)(row_span *
                (s_layout.tile_height + s_layout.tile_gap) - s_layout.tile_gap);
            if (candidate.y + candidate.height > desktop_bottom())
                candidate.y = (int16_t)(desktop_bottom() - candidate.height);
            if (!tile_candidate_free(moving, &candidate)) continue;
            dx = candidate.x - app->rect.x;
            dy = candidate.y - app->rect.y;
            distance = (uint32_t)(dx * dx + dy * dy);
            if (distance < best_distance)
            {
                best = candidate;
                best_distance = distance;
            }
        }
    }
    if (best_distance != 0xffffffffUL) app->rect = best;
}

static void settle_selected_tile(uint32_t now_ms)
{
    ft_app_t *selected;
    fui_rect_t original[FT_APP_COUNT];
    fui_rect_t target[FT_APP_COUNT];
    uint8_t col_span, row_span, col, row, rows;
    size_t i;
    if (s.selected_tile < 0) return;
    for (i = 0U; i < FT_APP_COUNT; i++)
    {
        cancel_tile_motion(&s_apps[i]);
        original[i] = s_apps[i].rect;
    }
    selected = &s_apps[s.selected_tile];
    rows = desktop_grid_rows();
    col_span = tile_span(selected->rect.width + s_layout.tile_gap,
                         s_layout.tile_cell_width, FT_TILE_COLUMNS);
    row_span = tile_span(selected->rect.height + s_layout.tile_gap,
                         s_layout.tile_height + s_layout.tile_gap, rows);
    col = selected->rect.x <= s_layout.margin ? 0U :
          (uint8_t)((selected->rect.x - s_layout.margin +
                     s_layout.tile_cell_width / 2) /
                    s_layout.tile_cell_width);
    if (col + col_span > FT_TILE_COLUMNS)
        col = (uint8_t)(FT_TILE_COLUMNS - col_span);
    row = 0U;
    {
        uint32_t best = 0xffffffffUL;
        uint8_t candidate;
        for (candidate = 0U; candidate + row_span <= rows; candidate++)
        {
            int16_t candidate_y = desktop_grid_y(candidate);
            int16_t candidate_height = (int16_t)(row_span *
                (s_layout.tile_height + s_layout.tile_gap) -
                s_layout.tile_gap);
            int32_t d;
            if (candidate_y + candidate_height > desktop_bottom())
                candidate_y = (int16_t)(desktop_bottom() - candidate_height);
            d = candidate_y - selected->rect.y;
            uint32_t distance = (uint32_t)(d * d);
            if (distance < best) { best = distance; row = candidate; }
        }
    }
    selected->rect.x = (int16_t)(s_layout.margin +
                                  col * s_layout.tile_cell_width);
    selected->rect.y = desktop_grid_y(row);
    selected->rect.width = (int16_t)(col_span * s_layout.tile_cell_width -
                                      s_layout.tile_gap);
    selected->rect.height = (int16_t)(row_span *
        (s_layout.tile_height + s_layout.tile_gap) - s_layout.tile_gap);
    if (selected->rect.y + selected->rect.height > desktop_bottom())
        selected->rect.y = (int16_t)(desktop_bottom() - selected->rect.height);

    /* Only tiles actually covered by the confirmed snap are displaced.  Each
     * displaced tile searches every legal grid pit and picks the nearest one. */
    for (i = 0U; i < FT_APP_COUNT; i++)
        if (i != (size_t)s.selected_tile &&
            rect_overlap(&selected->rect, &s_apps[i].rect)) reflow_tile(i);

    for (i = 0U; i < FT_APP_COUNT; i++) target[i] = s_apps[i].rect;
    for (i = 0U; i < FT_APP_COUNT; i++) s_apps[i].rect = original[i];
    for (i = 0U; i < FT_APP_COUNT; i++)
        if (!rect_equal(&original[i], &target[i]))
            start_tile_motion(&s_apps[i], &target[i], now_ms);
}

static bool desktop_pointer_down(int16_t x, int16_t y)
{
    ft_app_t *tile;
    int16_t right, bottom, handle;
    if (!s.desktop_edit || s.selected_tile < 0) return false;
    tile = &s_apps[s.selected_tile];
    if (!fui_rect_contains(&tile->rect, x, y)) return false;
    cancel_tile_motion(tile);
    s.tile_pointer_x = x;
    s.tile_pointer_y = y;
    s.tile_pointer_rect = tile->rect;
    right = (int16_t)(tile->rect.x + tile->rect.width);
    bottom = (int16_t)(tile->rect.y + tile->rect.height);
    handle = tile_handle_size();
    s.tile_resize_corner = 0U;
    if (x - tile->rect.x <= handle && y - tile->rect.y <= handle)
        s.tile_resize_corner = 1U;
    else if (right - x <= handle && y - tile->rect.y <= handle)
        s.tile_resize_corner = 2U;
    else if (x - tile->rect.x <= handle && bottom - y <= handle)
        s.tile_resize_corner = 3U;
    else if (right - x <= handle && bottom - y <= handle)
        s.tile_resize_corner = 4U;
    s.tile_drag = s.tile_resize_corner == 0U;
    return true;
}

static void desktop_pointer_move(const fui_event_t *event)
{
    ft_app_t *tile;
    int16_t dx;
    int16_t dy;
    int16_t fixed_right;
    int16_t fixed_bottom;
    int16_t min_width = (int16_t)(s_layout.tile_cell_width - s_layout.tile_gap);
    int16_t min_height = s_layout.tile_height;
    if (s.selected_tile < 0) return;
    tile = &s_apps[s.selected_tile];
    dx = (int16_t)(event->x - s.tile_pointer_x);
    dy = (int16_t)(event->y - s.tile_pointer_y);
    fixed_right = (int16_t)(s.tile_pointer_rect.x + s.tile_pointer_rect.width);
    fixed_bottom = (int16_t)(s.tile_pointer_rect.y + s.tile_pointer_rect.height);
    if (s.tile_drag)
    {
        tile->rect.x = (int16_t)(s.tile_pointer_rect.x + dx);
        tile->rect.y = (int16_t)(s.tile_pointer_rect.y + dy);
    }
    else if (s.tile_resize_corner != 0U)
    {
        if (s.tile_resize_corner == 1U || s.tile_resize_corner == 3U)
        {
            tile->rect.x = (int16_t)(s.tile_pointer_rect.x + dx);
            tile->rect.width = (int16_t)(fixed_right - tile->rect.x);
        }
        else tile->rect.width = (int16_t)(s.tile_pointer_rect.width + dx);
        if (s.tile_resize_corner == 1U || s.tile_resize_corner == 2U)
        {
            tile->rect.y = (int16_t)(s.tile_pointer_rect.y + dy);
            tile->rect.height = (int16_t)(fixed_bottom - tile->rect.y);
        }
        else tile->rect.height = (int16_t)(s.tile_pointer_rect.height + dy);
        if (tile->rect.width < min_width)
        {
            tile->rect.width = min_width;
            if (s.tile_resize_corner == 1U || s.tile_resize_corner == 3U)
                tile->rect.x = (int16_t)(fixed_right - min_width);
        }
        if (tile->rect.height < min_height)
        {
            tile->rect.height = min_height;
            if (s.tile_resize_corner == 1U || s.tile_resize_corner == 2U)
                tile->rect.y = (int16_t)(fixed_bottom - min_height);
        }
    }
    if (tile->rect.x < s_layout.margin)
    {
        tile->rect.x = s_layout.margin;
        if (!s.tile_drag &&
            (s.tile_resize_corner == 1U || s.tile_resize_corner == 3U))
            tile->rect.width = (int16_t)(fixed_right - tile->rect.x);
    }
    if (tile->rect.y < s_layout.tile_top)
    {
        tile->rect.y = s_layout.tile_top;
        if (!s.tile_drag &&
            (s.tile_resize_corner == 1U || s.tile_resize_corner == 2U))
            tile->rect.height = (int16_t)(fixed_bottom - tile->rect.y);
    }
    if (tile->rect.x + tile->rect.width > FT_SCREEN_W - s_layout.margin)
    {
        if (s.tile_drag) tile->rect.x = FT_SCREEN_W - s_layout.margin - tile->rect.width;
        else tile->rect.width = FT_SCREEN_W - s_layout.margin - tile->rect.x;
    }
    if (tile->rect.y + tile->rect.height > desktop_bottom())
    {
        if (s.tile_drag) tile->rect.y = desktop_bottom() - tile->rect.height;
        else tile->rect.height = desktop_bottom() - tile->rect.y;
    }
}

bool ft_gpu_scene_event(const fui_event_t *event, void *user_data)
{
    ft_gpu_page_t page;
    (void)user_data;
    if (event == RT_NULL) return false;
    if (event->type == FUI_EVENT_FRAME)
    {
        bool dirty = false;
        ft_gpu_image_info_t gallery_image;
        uint32_t elapsed = event->timestamp_ms - s.last_stats_ms;
        if (elapsed >= 1000U)
        {
            fui_engine_stats_t stats;
            fui_engine_get_stats(&stats);
            s.fps = (uint32_t)(((uint64_t)(stats.frames_presented - s.last_frame_count) *
                                1000ULL) / elapsed);
            s.last_frame_count = stats.frames_presented;
            s.last_stats_ms = event->timestamp_ms;
            dirty = true;
        }
        if (event->timestamp_ms - s.last_service_ms >= 500U)
        {
            refresh_services();
            s.last_service_ms = event->timestamp_ms;
            if (ft_gpu_scene_current_page() != FT_GPU_PAGE_HOME) dirty = true;
        }
        if (s.gallery_viewer &&
            ft_gpu_image_get(FT_GPU_IMAGE_GALLERY, &gallery_image) &&
            gallery_image.state == FT_GPU_IMAGE_LOADING)
            dirty = true;
        if (s.toast_visible &&
            (int32_t)(event->timestamp_ms - s.toast_until_ms) >= 0)
        {
            s.toast_visible = false;
            dirty = true;
        }
        return dirty;
    }
    if (s.dialog_visible) return dialog_handle_event(event);
    if (s.select_kind != FT_SELECT_NONE) return select_handle_event(event);
    if (s.toast_visible && event->type == FUI_EVENT_TAP)
    {
        fui_rect_t bounds = toast_rect();
        if (fui_rect_contains(&bounds, event->x, event->y))
        {
            s.toast_visible = false;
            return true;
        }
    }
    if (handle_shade(event)) return true;
    page = ft_gpu_scene_current_page();
    if (s.keyboard_visible && event->y >= s_layout.keyboard_y &&
        event->y < FT_NAV_Y &&
        event->type != FUI_EVENT_TAP) return true;
    if (event->type == FUI_EVENT_TOUCH_DOWN)
    {
        s.touch_down_x = event->x;
        s.touch_down_y = event->y;
        s.touch_last_x = event->x;
        s.touch_last_y = event->y;
        s.scrolling = false;
        if (page == FT_GPU_PAGE_HOME && desktop_pointer_down(event->x, event->y))
            return true;
        return false;
    }
    if (event->type == FUI_EVENT_TOUCH_MOVE && event->y < FT_NAV_Y)
    {
        if (page == FT_GPU_PAGE_HOME && s.desktop_edit &&
            (s.tile_drag || s.tile_resize_corner != 0U))
        {
            desktop_pointer_move(event);
            return true;
        }
        int16_t total = (int16_t)(event->y - s.touch_down_y);
        if (total > s_layout.touch_slop || total < -s_layout.touch_slop)
            s.scrolling = true;
        if (s.scrolling && page != FT_GPU_PAGE_HOME)
        {
            s.scroll_y = (int16_t)(s.scroll_y + event->y - s.touch_last_y);
            s.touch_last_x = event->x;
            s.touch_last_y = event->y;
            if (s.scroll_y > 0) s.scroll_y = 0;
            if (s.scroll_y < s.scroll_limit) s.scroll_y = s.scroll_limit;
            return true;
        }
    }
    if (event->type == FUI_EVENT_TOUCH_UP && page == FT_GPU_PAGE_HOME &&
        s.desktop_edit && (s.tile_drag || s.tile_resize_corner != 0U))
    {
        settle_selected_tile(event->timestamp_ms);
        s.tile_drag = false;
        s.tile_resize_corner = 0U;
        return true;
    }
    if (event->type == FUI_EVENT_LONG_PRESS)
    {
        size_t i;
        if (page == FT_GPU_PAGE_HOME)
        {
            for (i = 0U; i < FT_APP_COUNT; i++)
                if (fui_rect_contains(&s_apps[i].rect, event->x, event->y))
                {
                    s.desktop_edit = true;
                    s.selected_tile = (int8_t)i;
                    s.tile_drag = true;
                    s.tile_resize_corner = 0U;
                    s.tile_pointer_x = event->x;
                    s.tile_pointer_y = event->y;
                    s.tile_pointer_rect = s_apps[i].rect;
                    start_tile_pulse(event->timestamp_ms);
                    return true;
                }
        }
        else if ((page == FT_GPU_PAGE_FILES || page == FT_GPU_PAGE_GALLERY) &&
                 !s.gallery_viewer &&
                 event->y < FT_NAV_Y)
        {
            int row = row_at(event->y);
            int index = visible_file_index((uint8_t)row, page == FT_GPU_PAGE_GALLERY);
            if (index >= 0)
            {
                s.file_selected = (int8_t)index;
                s.file_menu = true;
                s.root_format_menu = strcmp(s.file_path, "/") == 0;
                return true;
            }
        }
        return false;
    }
    if (event->type != FUI_EVENT_TAP || s.scrolling) return false;
    if (s.keyboard_visible && event->y < FT_NAV_Y)
        return keyboard_tap(event->x, event->y);
    if (event->y >= FT_NAV_Y)
    {
        if (s.keyboard_visible)
        {
            s.keyboard_visible = false;
            return true;
        }
        if (event->x < FT_SCREEN_W / 3) route_back();
        else if (event->x < FT_SCREEN_W * 2 / 3)
            (void)ft_gpu_scene_open(FT_GPU_PAGE_HOME);
        else (void)ft_gpu_scene_open(FT_GPU_PAGE_SEARCH);
        return true;
    }
    handle_page_tap(event);
    return true;
}

int ft_gpu_scene_init(uint16_t screen_width, uint16_t screen_height)
{
    ft_preferences_store_payload_t defaults;
    uint32_t default_rate;
    uint8_t default_bits;
    uint8_t default_channels;
    memset(&s, 0, sizeof(s));
    s.tile_scale = FT_ANIMATION_SCALE_BASE;
    if (screen_width < FT_MIN_SURFACE_WIDTH ||
        screen_height < FT_MIN_SURFACE_HEIGHT ||
        screen_width > FT_MAX_SURFACE_WIDTH ||
        screen_height > FT_MAX_SURFACE_HEIGHT)
        return -RT_EINVAL;
    scene_layout_init(screen_width, screen_height);
    scene_tiles_reset_geometry();
    (void)ft_audio_get_status(&s.audio);
    default_rate = s.audio.output_sample_rate_count > 0U ?
                   s.audio.output_sample_rates[0] : s.audio.output_sample_rate;
    default_bits = s.audio.output_sample_bits_count > 0U ?
                   s.audio.output_sample_bits_supported[0] :
                   s.audio.output_sample_bits;
    default_channels = s.audio.output_channel_count > 0U ?
                       s.audio.output_channels_supported[0] :
                       s.audio.output_channels;
    if (ft_audio_output_format_supported(s.audio.output_sample_rate,
                                         s.audio.output_sample_bits,
                                         s.audio.output_channels))
    {
        default_rate = s.audio.output_sample_rate;
        default_bits = s.audio.output_sample_bits;
        default_channels = s.audio.output_channels;
    }
    memset(&defaults, 0, sizeof(defaults));
    defaults.accent_rgb = s_accent_palette[0];
    defaults.tile_opa = UINT8_MAX;
    defaults.background = FT_BACKGROUND_DARK;
    defaults.use_24_hour = true;
    defaults.timezone_offset_minutes = FT_DEFAULT_TIMEZONE_MINUTES;
    defaults.language = 0U;
    defaults.audio_output_volume = s.audio.output_ready ? s.audio.output_volume :
        percent_as_value(FT_DEFAULT_AUDIO_VOLUME_PERCENT,
                         s.audio.output_volume_max);
    defaults.audio_input_gain = s.audio.input_ready ? s.audio.input_gain :
        percent_as_value(FT_DEFAULT_AUDIO_GAIN_PERCENT, s.audio.input_gain_max);
    defaults.audio_output_sample_rate = default_rate;
    defaults.audio_output_sample_bits = default_bits;
    defaults.audio_output_channels = default_channels;
    s.prefs = defaults;
    (void)ft_preferences_store_init(&defaults, &s.prefs);
    if (!ft_audio_output_format_supported(s.prefs.audio_output_sample_rate,
                                          s.prefs.audio_output_sample_bits,
                                          s.prefs.audio_output_channels))
    {
        s.prefs.audio_output_sample_rate = default_rate;
        s.prefs.audio_output_sample_bits = default_bits;
        s.prefs.audio_output_channels = default_channels;
        preferences_save();
    }
    if (s.audio.output_registered)
    {
        (void)ft_audio_set_output_format(s.prefs.audio_output_sample_rate,
                                         s.prefs.audio_output_sample_bits,
                                         s.prefs.audio_output_channels);
        (void)ft_audio_set_output_volume(s.prefs.audio_output_volume);
    }
    if (s.audio.input_registered)
        (void)ft_audio_set_input_gain(s.prefs.audio_input_gain);
    if (ft_gpu_image_init() != RT_EOK) return -RT_ERROR;
    s.depth = 1U;
    s.route[0] = FT_GPU_PAGE_HOME;
    s.selected_tile = -1;
    s.file_selected = -1;
    s.shade_y = -FT_SHADE_H;
    s.shade_target_y = -FT_SHADE_H;
    s.brightness_valid = lcd_backlight_get_percent(&s.brightness) == RT_EOK;
    if (!s.brightness_valid) s.brightness = 0U;
    s.media_volume = s.prefs.audio_output_volume;
    strcpy(s.file_path, "/");
    reload_files();
    ft_notifications_init();
    (void)ft_notifications_push("FEATHERTALK",
                                tr("GPU UI READY", "GPU 界面已就绪"),
                                tr("ALL PAGES USE ONE GPU LIST",
                                   "全部页面使用单条 GPU 命令链"));
    (void)ft_notifications_push(tr("SYSTEM", "系统"),
                                tr("INTERACTION RESTORED", "交互功能已恢复"),
                                tr("SWIPE DOWN FOR QUICK SETTINGS",
                                   "下滑打开快捷设置"));
    refresh_services();
    if (s.prefs.background == FT_BACKGROUND_WALLPAPER &&
        s.prefs.wallpaper_path[0] != '\0')
        (void)ft_gpu_image_request(FT_GPU_IMAGE_WALLPAPER,
                                   s.prefs.wallpaper_path,
                                   FT_SCREEN_W, FT_CONTENT_H);
    s.last_stats_ms = rt_tick_get_millisecond();
    s.last_service_ms = s.last_stats_ms;
    return RT_EOK;
}

int ft_gpu_scene_run_test(void)
{
    ft_gpu_page_t page;
    uint8_t pass = 0U, fail = 0U;
    fui_event_t event;
    size_t notifications_before;
    ft_app_t saved_apps[FT_APP_COUNT];
    ft_scene_layout_t saved_layout;
    ft_preferences_store_payload_t saved_preferences;
    int16_t tile_start_x;
#define FT_CHECK(condition) do { if (condition) pass++; else fail++; } while (0)

    memcpy(saved_apps, s_apps, sizeof(saved_apps));
    saved_layout = s_layout;
    saved_preferences = s.prefs;
    tile_start_x = s_apps[0].rect.x;
    memset(&event, 0, sizeof(event));
    fui_animation_cancel_all();
    FT_CHECK(fui_component_text_width("ABC", 2U) > 0 &&
             fui_component_text_width("ABC", 3U) >
             fui_component_text_width("ABC", 2U));
    FT_CHECK(fui_component_text_width("设置", 2U) > 0 &&
             fui_component_text_width("A设置", 2U) >
             fui_component_text_width("设置", 2U));
    FT_CHECK(fui_component_text_height("A设", 1U) <
             fui_component_text_height("A设", 2U) &&
             fui_component_text_height("A设", 2U) <
             fui_component_text_height("A设", 3U));
    {
        fui_slider_t slider = {
            .bounds = {10, 0, 120, 20},
            .minimum = 0,
            .maximum = 100,
            .value = 50
        };
        FT_CHECK(fui_component_slider_value_from_x(&slider, 20) == 0 &&
                 fui_component_slider_value_from_x(&slider, 70) == 50 &&
                 fui_component_slider_value_from_x(&slider, 120) == 100);
    }
    {
        fui_scrollbar_t scrollbar = {
            .bounds = {0, 10, 4, 200},
            .viewport_extent = 100,
            .content_extent = 400,
            .offset = 150,
            .minimum_thumb = 20
        };
        fui_rect_t thumb = fui_component_scrollbar_thumb_rect(&scrollbar);
        FT_CHECK(thumb.y == 85 && thumb.height == 50);
    }
    {
        fui_option_t options[3] = {
            {"ONE", FUI_COMPONENT_STATE_DEFAULT},
            {"TWO", FUI_COMPONENT_STATE_DEFAULT},
            {"THREE", FUI_COMPONENT_STATE_DISABLED}
        };
        fui_radio_group_t radio = {
            .bounds = {10, 20, 180, 90}, .options = options,
            .option_count = 3U, .selected_index = 1U,
            .state = FUI_COMPONENT_STATE_DEFAULT, .text_scale = 1U,
            .vertical = true
        };
        fui_segmented_control_t segments = {
            .bounds = {0, 0, 180, 40}, .options = options,
            .option_count = 3U, .selected_index = 0U,
            .state = FUI_COMPONENT_STATE_DEFAULT, .text_scale = 1U
        };
        fui_select_popup_t popup = {
            .bounds = {20, 30, 180, 160}, .title = "SELECT",
            .options = options, .option_count = 3U, .selected_index = 1U,
            .state = FUI_COMPONENT_STATE_DEFAULT, .text_scale = 1U,
            .row_height = 40
        };
        fui_rect_t second = fui_component_radio_option_rect(&radio, 1U);
        FT_CHECK(second.y == 50 && second.height == 30 &&
                 fui_component_radio_index_from_point(&radio, 20, 60) == 1 &&
                 fui_component_radio_index_from_point(&radio, 20, 95) == -1);
        second = fui_component_segment_rect(&segments, 1U);
        FT_CHECK(second.x == 60 && second.width == 60 &&
                 fui_component_segment_index_from_point(&segments, 80, 20) == 1 &&
                 fui_component_segment_index_from_point(&segments, 150, 20) == -1);
        second = fui_component_select_option_rect(&popup, 1U);
        FT_CHECK(second.y == 110 && second.height == 40 &&
                 fui_component_select_index_from_point(&popup, 30, 125) == 1);
        {
            fui_context_menu_item_t items[3] = {
                {.label = "OPEN"},
                {.label = "COPY"},
                {.label = "DELETE", .state = FUI_COMPONENT_STATE_DISABLED,
                 .variant = FUI_BUTTON_DANGER}
            };
            fui_context_menu_t menu = {
                .bounds = {10, 20, 180, 160}, .title = "FILE",
                .items = items, .item_count = 3U,
                .state = FUI_COMPONENT_STATE_DEFAULT, .text_scale = 1U,
                .header_height = 40, .row_height = 40
            };
            fui_dialog_t dialog = {
                .bounds = {20, 30, 300, 220}, .content_inset = 20,
                .button_height = 44, .button_gap = 12,
                .show_secondary = true
            };
            fui_rect_t item = fui_component_context_menu_item_rect(&menu, 1U);
            fui_rect_t cancel = fui_component_dialog_button_rect(&dialog, false);
            fui_rect_t primary = fui_component_dialog_button_rect(&dialog, true);
            FT_CHECK(item.y == 100 && item.height == 40 &&
                     fui_component_context_menu_index_from_point(&menu,
                                                                  20, 115) == 1 &&
                     fui_component_context_menu_index_from_point(&menu,
                                                                  20, 155) == -1);
            FT_CHECK(cancel.x == 40 && cancel.y == 186 && cancel.width == 124 &&
                     primary.x == 176 && primary.width == 124);
        }
        FT_CHECK(fui_icon_is_valid(FUI_ICON_APP_SETTINGS) &&
                 fui_icon_is_valid((fui_icon_id_t)(FUI_ICON_COUNT - 1U)) &&
                 !fui_icon_is_valid(FUI_ICON_NONE) &&
                 !fui_icon_is_valid(FUI_ICON_COUNT));
    }
    event.type = FUI_EVENT_FRAME;
    event.timestamp_ms = rt_tick_get_millisecond();
    toast_show("TEST", event.timestamp_ms);
    FT_CHECK(s.toast_visible);
    event.timestamp_ms += 2201U;
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && !s.toast_visible);
    event.timestamp_ms = rt_tick_get_millisecond();
    (void)ft_gpu_scene_open(FT_GPU_PAGE_HOME);
    event.type = FUI_EVENT_LONG_PRESS;
    event.x = (int16_t)(s_apps[0].rect.x + s_apps[0].rect.width / 2);
    event.y = (int16_t)(s_apps[0].rect.y + s_apps[0].rect.height / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.desktop_edit &&
             s.selected_tile == 0);
    FT_CHECK(fui_animation_is_active());
    (void)fui_animation_update(event.timestamp_ms + FT_TILE_PULSE_DURATION_MS / 2U);
    FT_CHECK(s.tile_scale < FT_ANIMATION_SCALE_BASE);
    event.type = FUI_EVENT_TOUCH_MOVE;
    event.delta_x = s_layout.tile_cell_width / 2;
    event.delta_y = s_layout.tile_height / 3;
    event.x = (int16_t)(event.x + event.delta_x);
    event.y = (int16_t)(event.y + event.delta_y);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
             s_apps[0].rect.x == tile_start_x + event.delta_x);
    event.type = FUI_EVENT_TOUCH_UP;
    event.timestamp_ms += 32U;
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.desktop_edit &&
             s_apps[0].rect.x >= s_layout.margin &&
             s_apps[0].rect.y >= s_layout.tile_top);
    (void)fui_animation_update(event.timestamp_ms + FT_TILE_SETTLE_MAX_MS + 1U);
    event.type = FUI_EVENT_TOUCH_DOWN;
    event.x = s_apps[0].rect.x + s_apps[0].rect.width - 4;
    event.y = s_apps[0].rect.y + s_apps[0].rect.height - 4;
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.tile_resize_corner == 4U);
    event.type = FUI_EVENT_TOUCH_MOVE;
    event.delta_x = s_layout.tile_cell_width;
    event.delta_y = s_layout.tile_height;
    desktop_pointer_move(&event);
    event.type = FUI_EVENT_TOUCH_UP;
    event.timestamp_ms += 32U;
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
             s_apps[0].rect.width >=
                 s_layout.tile_cell_width * 2 - s_layout.tile_gap &&
             s_apps[0].rect.height >= s_layout.tile_height);
    (void)fui_animation_update(event.timestamp_ms + FT_TILE_SETTLE_MAX_MS + 1U);
    event.type = FUI_EVENT_TAP;
    event.x = (int16_t)(s_apps[0].rect.x + s_apps[0].rect.width / 2);
    event.y = (int16_t)(s_apps[0].rect.y + s_apps[0].rect.height / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && !s.desktop_edit);
    FT_CHECK(s.tile_scale == FT_ANIMATION_SCALE_BASE);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
             ft_gpu_scene_current_page() == FT_GPU_PAGE_SETTINGS);

    event.x = FT_SCREEN_W / 2;
    event.y = (int16_t)(FT_NAV_Y + FT_NAV_H / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
             ft_gpu_scene_current_page() == FT_GPU_PAGE_HOME && s.depth == 1U);
    event.x = FT_SCREEN_W * 5 / 6;
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
             ft_gpu_scene_current_page() == FT_GPU_PAGE_SEARCH && s.depth == 2U);
    {
        fui_rect_t search = search_box_rect();
        event.x = (int16_t)(search.x + search.width / 2);
        event.y = (int16_t)(search.y + search.height / 2);
    }
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.keyboard_visible);
    event.x = (int16_t)(keyboard_x(10) + keyboard_width(42) / 2);
    event.y = (int16_t)(keyboard_y(58) +
        (keyboard_y(100) - keyboard_y(58)) / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && strcmp(s.search_text, "Q") == 0);
    event.x = (int16_t)(keyboard_x(248) + keyboard_width(62) / 2);
    event.y = (int16_t)(keyboard_y(212) +
        (keyboard_y(254) - keyboard_y(212)) / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.search_text[0] == '\0');
    event.x = (int16_t)(keyboard_x(392) + keyboard_width(68) / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && !s.keyboard_visible);
    event.x = FT_SCREEN_W / 6;
    event.y = (int16_t)(FT_NAV_Y + FT_NAV_H / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
             ft_gpu_scene_current_page() == FT_GPU_PAGE_HOME);

    (void)ft_gpu_scene_open(FT_GPU_PAGE_SETTINGS);
    s.scroll_limit = -170;
    event.type = FUI_EVENT_TOUCH_DOWN;
    event.x = FT_SCREEN_W / 2;
    event.y = (int16_t)(FT_LIST_TOP + FT_ROW_H * 3);
    (void)ft_gpu_scene_event(&event, RT_NULL);
    event.type = FUI_EVENT_TOUCH_MOVE;
    event.y = (int16_t)(event.y - 90);
    event.delta_y = -90;
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.scroll_y == -90);

    (void)ft_gpu_scene_open(FT_GPU_PAGE_HOME);
    memcpy(s_apps, saved_apps, sizeof(saved_apps));
    s.desktop_edit = false;
    s.selected_tile = -1;
    s.tile_drag = false;
    s.tile_resize_corner = 0U;
    fui_engine_invalidate();
    event.type = FUI_EVENT_TOUCH_DOWN;
    event.timestamp_ms += 1000U;
    event.x = FT_SCREEN_W / 2;
    event.y = s_layout.touch_slop / 2;
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.shade_visible &&
             s.shade_pointer_down && !s.shade_drag);
    event.type = FUI_EVENT_TOUCH_MOVE;
    event.y = (int16_t)(FT_SHADE_H * 3 / 4);
    event.delta_y = (int16_t)(event.y - s.touch_down_y);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.shade_drag);
    event.type = FUI_EVENT_TOUCH_UP;
    event.timestamp_ms += 32U;
    event.delta_y = (int16_t)(event.y - s.touch_down_y);
    (void)ft_gpu_scene_event(&event, RT_NULL);
    (void)fui_animation_update(event.timestamp_ms + FT_SHADE_SETTLE_MAX_MS + 1U);
    FT_CHECK(s.shade_visible && !s.shade_drag && s.shade_y == FT_STATUS_H);
    notifications_before = ft_notifications_count();
    event.type = FUI_EVENT_TOUCH_DOWN;
    event.x = (int16_t)(FT_SCREEN_W / 2 - s_layout.horizontal_swipe / 2);
    event.y = (int16_t)(s.shade_y + shade_offset(300));
    event.timestamp_ms += FT_SHADE_SETTLE_MAX_MS + 16U;
    (void)ft_gpu_scene_event(&event, RT_NULL);
    event.type = FUI_EVENT_TOUCH_UP;
    event.x = (int16_t)(event.x + s_layout.horizontal_swipe + 10);
    event.delta_x = (int16_t)(s_layout.horizontal_swipe + 10);
    event.delta_y = 0;
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
             ft_notifications_count() + 1U == notifications_before);
    (void)ft_notifications_push("TEST", "GESTURE PASS", "SWIPE DELETE VERIFIED");
    event.type = FUI_EVENT_TAP;
    event.x = s_layout.margin;
    event.y = (int16_t)(FT_NAV_Y - s_layout.margin);
    event.timestamp_ms += 32U;
    (void)ft_gpu_scene_event(&event, RT_NULL);
    (void)fui_animation_update(event.timestamp_ms + FT_SHADE_SETTLE_MAX_MS + 1U);
    FT_CHECK(!s.shade_visible);

    (void)ft_gpu_scene_open(FT_GPU_PAGE_MEDIA);
    s.media_track = 0U;
    s.media_state = 0U;
    event.type = FUI_EVENT_TAP;
    {
        fui_rect_t controls = media_controls_rect();
        event.x = (int16_t)(controls.x + controls.width * 5 / 6);
        event.y = (int16_t)(controls.y + controls.height / 2);
    }
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.media_track == 1U);
    {
        fui_rect_t controls = media_controls_rect();
        event.x = (int16_t)(controls.x + controls.width / 2);
    }
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.media_state == 1U);
    {
        fui_rect_t controls = media_controls_rect();
        event.x = (int16_t)(controls.x + controls.width / 6);
    }
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.media_track == 0U);

    (void)ft_preferences_store_test_suspend(true);
    (void)ft_gpu_scene_open(FT_GPU_PAGE_SETTINGS_TIME_LANGUAGE);
    event.x = 200;
    event.y = (int16_t)(row_y(2) + FT_ROW_H / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
             s.select_kind == FT_SELECT_LANGUAGE);
    {
        ft_select_model_t model;
        uint8_t target = saved_preferences.language == 0U ? 1U : 0U;
        fui_rect_t option;
        FT_CHECK(select_model_build(&model) && model.count == 2U);
        option = fui_component_select_option_rect(&model.popup, target);
        event.x = (int16_t)(option.x + option.width / 2);
        event.y = (int16_t)(option.y + option.height / 2);
        FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) &&
                 s.select_kind == FT_SELECT_NONE &&
                 s.prefs.language != saved_preferences.language);
    }
    s.prefs = saved_preferences;
    (void)ft_preferences_store_test_suspend(false);

    (void)ft_gpu_scene_open(FT_GPU_PAGE_SETTINGS_STORAGE);
    dialog_show_confirm(FT_DIALOG_ACTION_FORMAT_FLASH,
                        tr("INTERNAL FLASH", "内部 Flash"));
    FT_CHECK(s.dialog_visible &&
             s.dialog_view == FT_DIALOG_VIEW_CONFIRM &&
             ft_gpu_scene_current_page() == FT_GPU_PAGE_SETTINGS_STORAGE);
    {
        fui_rect_t primary = dialog_button_rect(true);
        event.type = FUI_EVENT_TAP;
        event.x = (int16_t)(primary.x + primary.width / 2);
        event.y = (int16_t)(primary.y + primary.height / 2);
    }
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && s.dialog_visible &&
             s.dialog_view == FT_DIALOG_VIEW_FINAL_CONFIRM);
    {
        fui_rect_t cancel = dialog_button_rect(false);
        event.x = (int16_t)(cancel.x + cancel.width / 2);
        event.y = (int16_t)(cancel.y + cancel.height / 2);
    }
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && !s.dialog_visible &&
             ft_gpu_scene_current_page() == FT_GPU_PAGE_SETTINGS_STORAGE);
    dialog_show_confirm(FT_DIALOG_ACTION_DELETE_FILE, "/flash/test.txt");
    event.x = FT_SCREEN_W / 6;
    event.y = (int16_t)(FT_NAV_Y + FT_NAV_H / 2);
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && !s.dialog_visible &&
             ft_gpu_scene_current_page() == FT_GPU_PAGE_SETTINGS_STORAGE);
    dialog_show_message(FT_DIALOG_ACTION_RECORDER_START, -RT_EIO, false);
    {
        fui_rect_t primary = dialog_button_rect(true);
        event.x = (int16_t)(primary.x + primary.width / 2);
        event.y = (int16_t)(primary.y + primary.height / 2);
    }
    FT_CHECK(ft_gpu_scene_event(&event, RT_NULL) && !s.dialog_visible);

    {
        fui_animation_stats_t animation_stats;
        fui_animation_get_stats(&animation_stats);
        FT_CHECK(animation_stats.started >= 3U &&
                 animation_stats.completed >= 2U);
        FT_CHECK(animation_stats.peak_active >= 2U);
    }

    FT_CHECK(s.audio.output_sample_rate_count <= FT_AUDIO_MAX_SAMPLE_RATES &&
             s.audio.output_sample_bits_count <= FT_AUDIO_MAX_SAMPLE_BITS &&
             s.audio.output_channel_count <= FT_AUDIO_MAX_CHANNELS);
    if (s.audio.output_sample_rate_count > 0U)
        FT_CHECK(ft_audio_output_format_supported(
            s.audio.output_sample_rates[0],
            s.audio.output_sample_bits_supported[0],
            s.audio.output_channels_supported[0]));

    {
        static const uint16_t sizes[][2] =
        {
            {240U, 320U}, {320U, 480U}, {480U, 800U}, {800U, 480U}
        };
        size_t configuration;
        for (configuration = 0U;
             configuration < sizeof(sizes) / sizeof(sizes[0]); configuration++)
        {
            size_t tile;
            bool tiles_inside = true;
            fui_rect_t viewer;
            fui_rect_t delete_button;
            fui_rect_t file_menu;
            fui_rect_t dialog;
            fui_rect_t dialog_primary;
            scene_layout_init(sizes[configuration][0], sizes[configuration][1]);
            scene_tiles_reset_geometry();
            for (tile = 0U; tile < FT_APP_COUNT; tile++)
                if (s_apps[tile].rect.x < s_layout.margin ||
                    s_apps[tile].rect.y < s_layout.status_height ||
                    s_apps[tile].rect.x + s_apps[tile].rect.width >
                        s_layout.screen_width - s_layout.margin ||
                    s_apps[tile].rect.y + s_apps[tile].rect.height >
                        s_layout.navigation_y)
                    tiles_inside = false;
            FT_CHECK(s_layout.navigation_y + s_layout.navigation_height ==
                     s_layout.screen_height && s_layout.row_width > 0 &&
                     s_layout.content_height > 0);
            FT_CHECK(tiles_inside);
            viewer = gallery_view_panel_rect();
            delete_button = gallery_delete_rect();
            file_menu = file_menu_rect(false);
            dialog = dialog_panel_rect();
            dialog_primary = dialog_button_rect(true);
            FT_CHECK(viewer.x >= 0 && viewer.y >= s_layout.status_height &&
                     viewer.x + viewer.width <= s_layout.screen_width &&
                     delete_button.y + delete_button.height <=
                         s_layout.navigation_y &&
                     file_menu.y >= s_layout.status_height &&
                     file_menu.y + file_menu.height <= s_layout.navigation_y);
            FT_CHECK(dialog.x >= 0 && dialog.y >= s_layout.status_height &&
                     dialog.x + dialog.width <= s_layout.screen_width &&
                     dialog.y + dialog.height <= s_layout.navigation_y &&
                     dialog_primary.x >= dialog.x &&
                     dialog_primary.x + dialog_primary.width <=
                         dialog.x + dialog.width);
        }
        s_layout = saved_layout;
        memcpy(s_apps, saved_apps, sizeof(saved_apps));
    }

    for (page = FT_GPU_PAGE_SEARCH; page < FT_GPU_PAGE_COUNT; page++)
    {
        FT_CHECK(ft_gpu_scene_open(page) == RT_EOK &&
                 ft_gpu_scene_current_page() == page);
    }
    FT_CHECK(s.depth <= FT_ROUTE_DEPTH);
    FT_CHECK(ft_notifications_count() > 0U);
    refresh_services();
    pass += 5U; /* audio, recorder, USB, flash and SD status calls returned safely. */
    (void)ft_gpu_scene_open(FT_GPU_PAGE_HOME);
    s.test_pass = pass;
    s.test_fail = fail;
    rt_kprintf("FeatherUI interaction test: pass=%u fail=%u pages=%u route=%u\n",
               pass, fail, FT_GPU_PAGE_COUNT, s.depth);
#undef FT_CHECK
    return fail == 0U ? RT_EOK : -RT_ERROR;
}
