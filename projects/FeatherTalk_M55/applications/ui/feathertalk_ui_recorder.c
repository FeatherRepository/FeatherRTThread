#include <rtthread.h>
#include <stdint.h>

#include "feathertalk_recorder.h"
#include "feathertalk_ui.h"
#include "feathertalk_ui_icons.h"
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_layout.h"
#include "feathertalk_ui_recorder.h"

static lv_obj_t *s_page;
static lv_obj_t *s_title;
static lv_obj_t *s_description;
static lv_obj_t *s_device_heading;
static lv_obj_t *s_device_buttons[FT_RECORDER_DEVICE_COUNT];
static lv_obj_t *s_device_titles[FT_RECORDER_DEVICE_COUNT];
static lv_obj_t *s_device_details[FT_RECORDER_DEVICE_COUNT];
static lv_obj_t *s_timer_label;
static lv_obj_t *s_level_bar;
static lv_obj_t *s_record_button;
static lv_obj_t *s_record_icon;
static lv_obj_t *s_record_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_path_label;
static lv_obj_t *s_files_button;
static lv_obj_t *s_files_label;
static lv_timer_t *s_monitor_timer;

static bool recorder_obj_valid(lv_obj_t *obj)
{
    return obj != RT_NULL && lv_obj_is_valid(obj);
}

static void recorder_style_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
}

static const char *recorder_device_title(size_t index)
{
    if (index == 0U)
        return ft_preferences_text("双 PDM 麦克风阵列", "Dual PDM microphone array");
    return ft_preferences_text("AMIC2 模拟麦克风", "AMIC2 analog microphone");
}

static void recorder_format_time(uint32_t milliseconds, char *text,
                                 size_t text_size)
{
    uint32_t total_seconds = milliseconds / 1000U;
    rt_snprintf(text, text_size, "%02lu:%02lu.%lu",
                (unsigned long)(total_seconds / 60U),
                (unsigned long)(total_seconds % 60U),
                (unsigned long)((milliseconds / 100U) % 10U));
}

static void recorder_refresh(void)
{
    ft_recorder_device_info_t devices[FT_RECORDER_DEVICE_COUNT];
    ft_recorder_status_t status;
    char time_text[20];
    char detail[FT_RECORDER_PATH_MAX + 32U];
    const char *state_text;
    bool active;
    size_t count;
    size_t index;

    if (!recorder_obj_valid(s_page)) return;
    (void)ft_recorder_get_devices(devices, FT_RECORDER_DEVICE_COUNT, &count);
    (void)ft_recorder_get_status(&status);
    active = status.state == FT_RECORDER_STARTING ||
             status.state == FT_RECORDER_RECORDING ||
             status.state == FT_RECORDER_STOPPING;

    for (index = 0U; index < FT_RECORDER_DEVICE_COUNT; index++)
    {
        bool available = index < count && devices[index].registered &&
                         devices[index].ready;
        if (!recorder_obj_valid(s_device_buttons[index])) continue;
        if (!available || active)
            lv_obj_add_state(s_device_buttons[index], LV_STATE_DISABLED);
        else
            lv_obj_remove_state(s_device_buttons[index], LV_STATE_DISABLED);
        lv_obj_set_style_border_width(s_device_buttons[index],
                                      status.selected_device == index ? 3 : 1,
                                      LV_PART_MAIN);
        if (available)
        {
            lv_snprintf(detail, sizeof(detail),
                        "%s · %lu Hz · %u ch · %u bit",
                        devices[index].device_name,
                        (unsigned long)devices[index].sample_rate,
                        devices[index].channels, devices[index].sample_bits);
        }
        else
        {
            lv_snprintf(detail, sizeof(detail), "%s · %s",
                        devices[index].device_name,
                        ft_preferences_text("驱动不可用", "Driver unavailable"));
        }
        if (recorder_obj_valid(s_device_details[index]))
            lv_label_set_text(s_device_details[index], detail);
    }

    recorder_format_time(status.duration_ms, time_text, sizeof(time_text));
    if (recorder_obj_valid(s_timer_label))
        lv_label_set_text(s_timer_label, time_text);
    if (recorder_obj_valid(s_level_bar))
        lv_bar_set_value(s_level_bar, (int32_t)status.peak_per_mille,
                         LV_ANIM_ON);

    switch (status.state)
    {
    case FT_RECORDER_STARTING:
        state_text = ft_preferences_text("正在启动麦克风…", "Starting microphone...");
        break;
    case FT_RECORDER_RECORDING:
        state_text = ft_preferences_text("正在录音", "Recording");
        break;
    case FT_RECORDER_STOPPING:
        state_text = ft_preferences_text("正在保存 WAV 文件…", "Saving WAV file...");
        break;
    case FT_RECORDER_SAVED:
        state_text = ft_preferences_text("录音已保存", "Recording saved");
        break;
    case FT_RECORDER_ERROR:
        state_text = ft_preferences_text("录音失败", "Recording failed");
        break;
    default:
        state_text = ft_recorder_can_start() ?
            ft_preferences_text("准备录音", "Ready to record") :
            ft_preferences_text("没有可写存储介质", "No writable storage");
        break;
    }
    if (recorder_obj_valid(s_status_label))
    {
        if (status.state == FT_RECORDER_ERROR)
        {
            lv_snprintf(detail, sizeof(detail), "%s (%d)",
                        state_text, status.last_error);
            lv_label_set_text(s_status_label, detail);
        }
        else
        {
            lv_label_set_text(s_status_label, state_text);
        }
    }

    if (recorder_obj_valid(s_path_label))
    {
        if (status.file_path[0] != '\0')
        {
            lv_snprintf(detail, sizeof(detail), "%s\n%lu KiB",
                        status.file_path,
                        (unsigned long)(status.data_bytes / 1024U));
            lv_label_set_text(s_path_label, detail);
        }
        else
        {
            lv_label_set_text(s_path_label,
                ft_preferences_text("保存位置：自动选择，优先使用 SD 卡",
                                    "Save location: automatic, SD card preferred"));
        }
    }

    if (recorder_obj_valid(s_record_icon))
        ft_icon_set(s_record_icon,
                    active ? FT_ICON_RECORD_STOP : FT_ICON_RECORD_ACTION,
                    ft_layout_icon_size(32U));
    if (recorder_obj_valid(s_record_label))
        lv_label_set_text(s_record_label,
            active ? ft_preferences_text("结束并保存", "Stop & save") :
                     ft_preferences_text("开始录音", "Start recording"));
    if (recorder_obj_valid(s_record_button))
    {
        if (status.state == FT_RECORDER_STOPPING ||
            (!active && !ft_recorder_can_start()))
            lv_obj_add_state(s_record_button, LV_STATE_DISABLED);
        else
            lv_obj_remove_state(s_record_button, LV_STATE_DISABLED);
    }
    if (recorder_obj_valid(s_files_button))
    {
        if (active) lv_obj_add_state(s_files_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(s_files_button, LV_STATE_DISABLED);
    }
}

static void recorder_device_clicked(lv_event_t *event)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (ft_recorder_select_device(index) != RT_EOK)
    {
        feathertalk_ui_alert(ft_preferences_text("录音设备", "Recording device"),
                            ft_preferences_text("该输入设备当前不可用。",
                                                "This input device is unavailable."));
    }
    recorder_refresh();
}

static void recorder_action_clicked(lv_event_t *event)
{
    ft_recorder_status_t status;
    int result;
    LV_UNUSED(event);
    (void)ft_recorder_get_status(&status);
    if (status.state == FT_RECORDER_STARTING ||
        status.state == FT_RECORDER_RECORDING)
        result = ft_recorder_stop();
    else
        result = ft_recorder_start();
    if (result != RT_EOK)
    {
        feathertalk_ui_alert(ft_preferences_text("录音机", "Recorder"),
                            ft_preferences_text(
                                "无法开始录音。请检查输入设备、SD 卡或内部 Flash。",
                                "Cannot start recording. Check the input device, SD card or internal Flash."));
    }
    recorder_refresh();
}

static void recorder_files_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    (void)ft_router_push(FT_PAGE_FILES);
}

static void recorder_monitor_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    recorder_refresh();
}

static lv_obj_t *recorder_create_device(lv_obj_t *parent, size_t index,
                                        ft_icon_id_t icon)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *column;
    lv_obj_set_width(button, lv_pct(100));
    lv_obj_set_height(button, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(button, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, ft_ui_accent_color(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, layout->section_gap, LV_PART_MAIN);
    lv_obj_set_style_pad_column(button, layout->section_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(button, recorder_device_clicked, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);
    (void)ft_icon_create(button, icon, ft_layout_icon_size(32U), true);
    column = lv_obj_create(button);
    recorder_style_container(column);
    lv_obj_set_width(column, 0);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, ft_layout_px(3), LV_PART_MAIN);
    s_device_titles[index] = lv_label_create(column);
    lv_obj_set_width(s_device_titles[index], lv_pct(100));
    lv_label_set_long_mode(s_device_titles[index], LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_device_titles[index], ft_layout_font(15), LV_PART_MAIN);
    s_device_details[index] = lv_label_create(column);
    lv_obj_set_width(s_device_details[index], lv_pct(100));
    lv_label_set_long_mode(s_device_details[index], LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_device_details[index], lv_color_hex(0xB8B8B8),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_device_details[index], ft_layout_font(12), LV_PART_MAIN);
    return button;
}

lv_obj_t *ft_recorder_page_create(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *header;
    lv_obj_t *record_column;

    s_page = lv_obj_create(parent);
    ft_ui_style_page(s_page);
    lv_obj_set_style_pad_all(s_page, layout->page_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_page, layout->section_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_page, LV_DIR_VER);

    header = lv_obj_create(s_page);
    recorder_style_container(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, layout->section_gap, LV_PART_MAIN);
    (void)ft_icon_create(header, FT_ICON_RECORDER_APP,
                         ft_layout_icon_size(40U), true);
    s_title = lv_label_create(header);
    lv_obj_set_style_text_font(s_title, ft_layout_font(22), LV_PART_MAIN);
    ft_ui_register_accent(s_title, FT_ACCENT_TEXT);
    s_description = lv_label_create(s_page);
    lv_obj_set_width(s_description, lv_pct(100));
    lv_label_set_long_mode(s_description, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_description, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_description, ft_layout_font(13), LV_PART_MAIN);

    s_device_heading = lv_label_create(s_page);
    lv_obj_set_width(s_device_heading, lv_pct(100));
    lv_obj_set_style_text_font(s_device_heading, ft_layout_font(16), LV_PART_MAIN);
    s_device_buttons[0] = recorder_create_device(
        s_page, 0U, FT_ICON_RECORDER_PDM_SOURCE);
    s_device_buttons[1] = recorder_create_device(
        s_page, 1U, FT_ICON_RECORDER_ANALOG_SOURCE);

    record_column = lv_obj_create(s_page);
    recorder_style_container(record_column);
    lv_obj_set_width(record_column, lv_pct(100));
    lv_obj_set_height(record_column, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(record_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(record_column, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(record_column, ft_layout_px(8), LV_PART_MAIN);
    s_timer_label = lv_label_create(record_column);
    lv_obj_set_style_text_font(s_timer_label, ft_layout_font(30), LV_PART_MAIN);
    s_level_bar = lv_bar_create(record_column);
    lv_obj_set_size(s_level_bar, lv_pct(90), ft_layout_px(8));
    lv_bar_set_range(s_level_bar, 0, 1000);
    ft_ui_register_accent(s_level_bar, FT_ACCENT_BACKGROUND);
    s_record_button = lv_button_create(record_column);
    lv_obj_set_size(s_record_button, ft_layout_px(96), ft_layout_px(96));
    lv_obj_set_style_radius(s_record_button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_record_button, lv_color_hex(0xD92D20), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_record_button, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_record_button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_record_button, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(s_record_button, recorder_action_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    s_record_icon = ft_icon_create(s_record_button, FT_ICON_RECORD_ACTION,
                                   ft_layout_icon_size(32U), false);
    s_record_label = lv_label_create(record_column);
    lv_obj_set_style_text_font(s_record_label, ft_layout_font(15), LV_PART_MAIN);
    s_status_label = lv_label_create(record_column);
    lv_obj_set_style_text_font(s_status_label, ft_layout_font(14), LV_PART_MAIN);
    ft_ui_register_accent(s_status_label, FT_ACCENT_TEXT);
    s_path_label = lv_label_create(record_column);
    lv_obj_set_width(s_path_label, lv_pct(100));
    lv_label_set_long_mode(s_path_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_path_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_path_label, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_path_label, ft_layout_font(12), LV_PART_MAIN);

    s_files_button = lv_button_create(s_page);
    lv_obj_set_size(s_files_button, lv_pct(100), layout->control_height);
    lv_obj_set_style_radius(s_files_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_files_button, 0, LV_PART_MAIN);
    ft_ui_register_accent(s_files_button, FT_ACCENT_BACKGROUND);
    lv_obj_add_event_cb(s_files_button, recorder_files_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    s_files_label = lv_label_create(s_files_button);
    lv_obj_center(s_files_label);
    lv_obj_set_style_text_font(s_files_label, ft_layout_font(14), LV_PART_MAIN);

    ft_recorder_page_apply_language();
    recorder_refresh();
    return s_page;
}

void ft_recorder_page_enter(void)
{
    if (s_monitor_timer == RT_NULL)
        s_monitor_timer = lv_timer_create(recorder_monitor_cb, 160U, RT_NULL);
    recorder_refresh();
}

void ft_recorder_page_leave(void)
{
    ft_recorder_status_t status;
    (void)ft_recorder_get_status(&status);
    if (status.state == FT_RECORDER_STARTING ||
        status.state == FT_RECORDER_RECORDING)
        (void)ft_recorder_stop();
    if (s_monitor_timer != RT_NULL)
    {
        lv_timer_delete(s_monitor_timer);
        s_monitor_timer = RT_NULL;
    }
    s_page = RT_NULL;
    s_title = RT_NULL;
    s_description = RT_NULL;
    s_device_heading = RT_NULL;
    s_device_buttons[0] = RT_NULL;
    s_device_buttons[1] = RT_NULL;
    s_device_titles[0] = RT_NULL;
    s_device_titles[1] = RT_NULL;
    s_device_details[0] = RT_NULL;
    s_device_details[1] = RT_NULL;
    s_timer_label = RT_NULL;
    s_level_bar = RT_NULL;
    s_record_button = RT_NULL;
    s_record_icon = RT_NULL;
    s_record_label = RT_NULL;
    s_status_label = RT_NULL;
    s_path_label = RT_NULL;
    s_files_button = RT_NULL;
    s_files_label = RT_NULL;
}

void ft_recorder_page_apply_language(void)
{
    size_t index;
    if (!recorder_obj_valid(s_page)) return;
    lv_label_set_text(s_title, ft_preferences_text("录音机", "Recorder"));
    lv_label_set_text(s_description,
        ft_preferences_text(
            "选择输入设备，开始录音；结束后保存为标准 PCM WAV 文件。",
            "Choose an input device and record. Stopping saves a standard PCM WAV file."));
    lv_label_set_text(s_device_heading,
                      ft_preferences_text("输入设备", "Input device"));
    for (index = 0U; index < FT_RECORDER_DEVICE_COUNT; index++)
        if (recorder_obj_valid(s_device_titles[index]))
            lv_label_set_text(s_device_titles[index],
                              recorder_device_title(index));
    if (recorder_obj_valid(s_files_label))
        lv_label_set_text(s_files_label,
                          ft_preferences_text("在文件中查看录音",
                                              "View recordings in Files"));
    recorder_refresh();
}

#ifdef FEATHERTALK_UI_TEST_MODE
bool ft_recorder_page_test_ready(void)
{
    ft_recorder_device_info_t devices[FT_RECORDER_DEVICE_COUNT];
    size_t count;
    (void)ft_recorder_get_devices(devices, FT_RECORDER_DEVICE_COUNT, &count);
    return recorder_obj_valid(s_page) && recorder_obj_valid(s_record_button) &&
           recorder_obj_valid(s_timer_label) && recorder_obj_valid(s_level_bar) &&
           recorder_obj_valid(s_device_buttons[0]) &&
           recorder_obj_valid(s_device_buttons[1]) && count == 2U &&
           devices[0].registered && devices[0].ready &&
           !devices[1].registered;
}

bool ft_recorder_page_test_slots_clear(void)
{
    return s_page == RT_NULL && s_title == RT_NULL &&
           s_record_button == RT_NULL && s_monitor_timer == RT_NULL &&
           s_device_buttons[0] == RT_NULL && s_device_buttons[1] == RT_NULL;
}

lv_obj_t *ft_recorder_page_test_get_device(size_t index)
{
    return index < FT_RECORDER_DEVICE_COUNT ? s_device_buttons[index] : RT_NULL;
}

lv_obj_t *ft_recorder_page_test_get_record_button(void)
{
    return s_record_button;
}

size_t ft_recorder_page_test_selected_device(void)
{
    ft_recorder_status_t status;
    (void)ft_recorder_get_status(&status);
    return status.selected_device;
}
#endif
