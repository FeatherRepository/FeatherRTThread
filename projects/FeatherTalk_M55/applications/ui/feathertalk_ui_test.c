#include <fcntl.h>
#include <rtthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "feathertalk_audio.h"
#include "feathertalk_player.h"
#include "feathertalk_storage.h"
#include "feathertalk_ui.h"
#include "feathertalk_ui_gallery.h"
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_notifications.h"
#include "feathertalk_ui_preferences_store.h"
#include "feathertalk_ui_recorder.h"

#ifdef FEATHERTALK_UI_TEST_MODE

#define FT_UI_TEST_START_DELAY_MS 1800U
#define FT_UI_TEST_STEP_MS         320U
#define FT_UI_TEST_ROUTE_LIMIT     8U
#define FT_UI_TEST_MEDIA_INDEX     1U
#define FT_UI_TEST_GALLERY_FIXTURE "/flash/Pictures/.feathertalk-ui-click-test.bmp"

typedef enum
{
    FT_TEST_PENDING = 0,
    FT_TEST_NOTIFY_SHOW,
    FT_TEST_NOTIFY_SHOW_VERIFY,
    FT_TEST_NOTIFY_DRAG_CLOSE,
    FT_TEST_NOTIFY_DRAG_CLOSE_VERIFY,
    FT_TEST_NOTIFY_DRAG_OPEN,
    FT_TEST_NOTIFY_DRAG_OPEN_VERIFY,
    FT_TEST_NOTIFY_BACK_CLOSE,
    FT_TEST_NOTIFY_BACK_CLOSE_VERIFY,
    FT_TEST_NOTIFY_FLING_OPEN,
    FT_TEST_NOTIFY_FLING_OPEN_VERIFY,
    FT_TEST_NOTIFY_FLING_CLOSE,
    FT_TEST_NOTIFY_FLING_CLOSE_VERIFY,
    FT_TEST_QUICK_STATUS_VERIFY,
    FT_TEST_QUICK_UNAVAILABLE_CLICK,
    FT_TEST_QUICK_BRIGHTNESS_CLICK,
    FT_TEST_QUICK_BRIGHTNESS_SLIDER,
    FT_TEST_QUICK_BRIGHTNESS_MINIMUM,
    FT_TEST_QUICK_BRIGHTNESS_RESTORE,
    FT_TEST_QUEUE_ADD,
    FT_TEST_QUEUE_OPEN,
    FT_TEST_QUEUE_OPEN_VERIFY,
    FT_TEST_QUEUE_SWIPE_DELETE,
    FT_TEST_QUEUE_CLEAR,
    FT_TEST_QUEUE_OVERFLOW,
    FT_TEST_QUEUE_MASK_CLOSE,
    FT_TEST_QUEUE_MASK_CLOSE_VERIFY,
    FT_TEST_QUEUE_HOME_OPEN,
    FT_TEST_QUEUE_HOME_CLOSE,
    FT_TEST_QUEUE_HOME_CLOSE_VERIFY,
    FT_TEST_ALERT_CREATE,
    FT_TEST_ALERT_VERIFY,
    FT_TEST_ALERT_CLOSE,
    FT_TEST_SEARCH_SHADE_OPEN,
    FT_TEST_SEARCH_OPEN,
    FT_TEST_SEARCH_VERIFY,
    FT_TEST_SEARCH_KEYBOARD_VERIFY,
    FT_TEST_SEARCH_KEYBOARD_HIDE,
    FT_TEST_SEARCH_KEYBOARD_HIDE_VERIFY,
    FT_TEST_SEARCH_KEYBOARD_REOPEN_VERIFY,
    FT_TEST_SEARCH_KEYBOARD_CANCEL_VERIFY,
    FT_TEST_SEARCH_FILTER,
    FT_TEST_SEARCH_FILTER_VERIFY,
    FT_TEST_SEARCH_RESULT,
    FT_TEST_SEARCH_RESULT_VERIFY,
    FT_TEST_SEARCH_HOME,
    FT_TEST_SEARCH_HOME_VERIFY,
    FT_TEST_ROOT_APPS_SHOW,
    FT_TEST_ROOT_APPS_VERIFY,
    FT_TEST_ROOT_BACK,
    FT_TEST_ROOT_BACK_VERIFY,
    FT_TEST_TILE_MODEL,
    FT_TEST_TILE_LONG_PRESS,
    FT_TEST_TILE_LONG_PRESS_VERIFY,
    FT_TEST_TILE_MOVE,
    FT_TEST_TILE_RESIZE,
    FT_TEST_TILE_PROPERTIES,
    FT_TEST_TILE_LIVE,
    FT_TEST_TILE_RESTORE,
    FT_TEST_START_OPEN,
    FT_TEST_PAGE_CONTROLS,
    FT_TEST_START_BACK,
    FT_TEST_LIFECYCLE_RELEASE_VERIFY,
    FT_TEST_LIFECYCLE_SEARCH_OPEN,
    FT_TEST_LIFECYCLE_SEARCH_WAIT,
    FT_TEST_LIFECYCLE_SEARCH_RESULT,
    FT_TEST_LIFECYCLE_SEARCH_RESULT_VERIFY,
    FT_TEST_LIFECYCLE_SEARCH_HOME,
    FT_TEST_LIFECYCLE_SEARCH_HOME_VERIFY,
    FT_TEST_LIST_SHOW,
    FT_TEST_LIST_SHOW_VERIFY,
    FT_TEST_LIST_OPEN,
    FT_TEST_LIST_HOME,
    FT_TEST_LIST_HOME_VERIFY,
    FT_TEST_ROUTE_FILL,
    FT_TEST_ROUTE_OVERFLOW,
    FT_TEST_ROUTE_HOME,
    FT_TEST_ROUTE_HOME_VERIFY,
    FT_TEST_LEAK_CHECK,
    FT_TEST_FINISH,
    FT_TEST_COMPLETE
} ft_ui_test_phase_t;

static lv_timer_t *s_test_timer;
static ft_ui_test_phase_t s_test_phase = FT_TEST_PENDING;
static size_t s_app_index;
static size_t s_control_index;
static uint32_t s_pass_count;
static uint32_t s_fail_count;
static uint32_t s_action_count;
static uint32_t s_start_ms;
static bool s_step_period_active;
static uint32_t s_files_before;
static uint32_t s_shade_render_before;
static uint8_t s_settings_brightness_before;
static uint8_t s_settings_audio_output_before;
static uint8_t s_settings_audio_input_before;
static uint8_t s_lifecycle_wait_steps;
static uint8_t s_gallery_wait_steps;
static ft_ui_preferences_t s_preferences_before;

static void test_record(bool passed, const char *action, const char *detail)
{
    uint32_t sequence;
    if (passed) s_pass_count++; else s_fail_count++;
    sequence = s_pass_count + s_fail_count;
    rt_kprintf("[UI-TEST] %s #%03lu %-25s%s%s\n", passed ? "PASS" : "FAIL",
               (unsigned long)sequence, action, detail != RT_NULL ? " " : "",
               detail != RT_NULL ? detail : "");
}

static bool test_event(lv_obj_t *control, lv_event_code_t code,
                       const char *action, const char *detail)
{
    lv_result_t result;
    s_action_count++;
    if (control == RT_NULL || !lv_obj_is_valid(control))
    {
        test_record(false, action, "object unavailable");
        return false;
    }
    if (!ft_layout_control_fits(control))
    {
        test_record(false, action, "zero-size or wider than parent");
        return false;
    }
    result = lv_obj_send_event(control, code, RT_NULL);
    test_record(result == LV_RESULT_OK, action, detail);
    return result == LV_RESULT_OK;
}

static bool test_click(lv_obj_t *control, const char *action, const char *detail)
{
    return test_event(control, LV_EVENT_CLICKED, action, detail);
}

static bool test_short_click(lv_obj_t *control, const char *action,
                             const char *detail)
{
    return test_event(control, LV_EVENT_SHORT_CLICKED, action, detail);
}

static bool test_alert_close(const char *action)
{
    lv_obj_t *button = ft_ui_test_get_alert_button();
    bool closed;
    s_action_count++;
    if (button == RT_NULL || !lv_obj_is_valid(button))
    {
        test_record(false, action, "object unavailable");
        return false;
    }
    /* LVGL returns LV_RESULT_INVALID when the close callback deletes the
       message-box tree during event dispatch. The observable contract is that
       the modal and its close button no longer exist. */
    (void)lv_obj_send_event(button, LV_EVENT_CLICKED, RT_NULL);
    closed = ft_ui_test_get_alert_button() == RT_NULL;
    test_record(closed, action, closed ? "closed" : "still visible");
    return closed;
}

static bool current_app_is(const ft_app_descriptor_t *app)
{
    return app != RT_NULL && ft_router_current_page() == app->page_id && ft_router_depth() == 2U;
}

static bool home_start_is_ready(void)
{
    return ft_router_current_page() == FT_PAGE_HOME && ft_router_depth() == 1U &&
           ft_pages_test_start_is_active();
}

static void finish_page_controls(void)
{
    s_test_phase = FT_TEST_START_BACK;
}

static void run_settings_test(void)
{
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    size_t accent_count = ft_pages_test_accent_count();
    size_t opacity_count = ft_pages_test_opacity_count();
    size_t background_count = ft_pages_test_background_count();
    const size_t preference_base = 23U;
    char detail[24];
    if (!ft_preferences_wallpaper_available() && background_count > 0U)
        background_count--;
    if (s_control_index == 0U)
    {
        test_record(ft_pages_test_settings_count() == 10U &&
                    ft_pages_test_settings_visible_count() == 10U,
                    "settings.categories", "8 controls + system information + about");
        test_record(ft_pages_test_settings_page_id(0U) == FT_PAGE_SETTINGS_DISPLAY &&
                    ft_pages_test_settings_page_id(1U) == FT_PAGE_SETTINGS_AUDIO &&
                    ft_pages_test_settings_page_id(2U) == FT_PAGE_SETTINGS_WIFI &&
                    ft_pages_test_settings_page_id(3U) == FT_PAGE_SETTINGS_BLUETOOTH &&
                    ft_pages_test_settings_page_id(4U) == FT_PAGE_SETTINGS_STORAGE &&
                    ft_pages_test_settings_page_id(5U) == FT_PAGE_SETTINGS_USB &&
                    ft_pages_test_settings_page_id(6U) == FT_PAGE_SETTINGS_TIME_LANGUAGE &&
                    ft_pages_test_settings_page_id(7U) == FT_PAGE_SETTINGS_PERSONALIZATION &&
                    ft_pages_test_settings_page_id(8U) == FT_PAGE_SYSTEM &&
                    ft_pages_test_settings_page_id(9U) == FT_PAGE_ABOUT,
                    "settings.scope", "board controls plus device information");
    }
    else if (s_control_index == 1U)
    {
        (void)test_click(ft_pages_test_get_settings_search_box(),
                         "settings.search.focus", RT_NULL);
        test_record(ft_pages_test_settings_keyboard_visible() &&
                    ft_pages_test_settings_keyboard_overlay_ok(),
                    "settings.keyboard", "fixed overlay");
    }
    else if (s_control_index == 2U)
    {
        lv_textarea_set_text(ft_pages_test_get_settings_search_box(), "wifi");
        (void)test_event(ft_pages_test_get_settings_search_box(), LV_EVENT_VALUE_CHANGED,
                         "settings.search.filter", "wifi");
        test_record(ft_pages_test_settings_visible_count() == 1U,
                    "settings.search.result", "one Wi-Fi category");
    }
    else if (s_control_index == 3U)
    {
        (void)test_click(ft_pages_test_get_settings_keyboard_hide(),
                         "settings.keyboard.hide", RT_NULL);
        test_record(!ft_pages_test_settings_keyboard_visible(),
                    "settings.keyboard", "hidden");
        lv_textarea_set_text(ft_pages_test_get_settings_search_box(), "");
        (void)lv_obj_send_event(ft_pages_test_get_settings_search_box(),
                                LV_EVENT_VALUE_CHANGED, RT_NULL);
        test_record(ft_pages_test_settings_visible_count() == 10U,
                    "settings.search.clear", "all categories restored");
    }
    else if (s_control_index == 4U)
    {
        s_settings_brightness_before = ft_ui_test_brightness();
        (void)test_click(ft_pages_test_get_settings_result(0U),
                         "settings.open", "Display & brightness");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_DISPLAY &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_get_settings_brightness() != RT_NULL,
                    "settings.route", "display detail");
    }
    else if (s_control_index == 5U)
    {
        uint8_t actual;
        lv_slider_set_value(ft_pages_test_get_settings_brightness(), 30, LV_ANIM_OFF);
        (void)test_event(ft_pages_test_get_settings_brightness(), LV_EVENT_VALUE_CHANGED,
                         "settings.brightness", "30%");
        actual = ft_ui_test_brightness();
        test_record(actual == 30U,
                    "settings.brightness.state", "real PWM readback");
        lv_slider_set_value(ft_pages_test_get_settings_brightness(),
                            s_settings_brightness_before, LV_ANIM_OFF);
        (void)lv_obj_send_event(ft_pages_test_get_settings_brightness(),
                                LV_EVENT_VALUE_CHANGED, RT_NULL);
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS &&
                    ft_router_depth() == 2U,
                    "settings.back", "detail -> settings");
    }
    else if (s_control_index == 6U)
    {
        s_settings_audio_output_before = preferences->audio_output_volume;
        s_settings_audio_input_before = preferences->audio_input_gain;
        (void)test_click(ft_pages_test_get_settings_result(1U),
                         "settings.open", "Audio");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_AUDIO &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_audio_state_valid(),
                    "settings.audio", "sound0 and mic0 registered and ready");
    }
    else if (s_control_index == 7U)
    {
        lv_obj_t *output;
        lv_obj_t *input;
        ft_audio_status_t audio_status;

        (void)test_click(ft_pages_test_get_audio_output_device(),
                         "settings.audio.output.open", "sound0 properties");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_AUDIO_OUTPUT &&
                    ft_router_depth() == 4U &&
                    ft_pages_test_audio_output_properties_valid(),
                    "settings.audio.output.route", "device -> output properties");
        output = ft_pages_test_get_audio_output_slider();
        lv_slider_set_value(output, 36, LV_ANIM_OFF);
        (void)test_event(output, LV_EVENT_VALUE_CHANGED,
                         "settings.audio.output", "36%");
        (void)test_event(output, LV_EVENT_RELEASED,
                         "settings.audio.output.apply", "sound0");
        (void)test_click(ft_pages_test_get_audio_rate_button(2U),
                         "settings.audio.rate", "48 kHz");
        (void)test_click(ft_pages_test_get_audio_bits_button(1U),
                         "settings.audio.depth", "24 bit");
        (void)test_click(ft_pages_test_get_audio_channel_button(0U),
                         "settings.audio.channels", "mono");
        (void)ft_audio_get_status(&audio_status);
        test_record(preferences->audio_output_sample_rate == 48000U &&
                    preferences->audio_output_sample_bits == 24U &&
                    preferences->audio_output_channels == 1U &&
                    audio_status.output_sample_rate == 48000U &&
                    audio_status.output_sample_bits == 24U &&
                    audio_status.output_channels == 1U,
                    "settings.audio.format", "UI selection and sound0 readback agree");
        lv_slider_set_value(output, s_settings_audio_output_before, LV_ANIM_OFF);
        (void)lv_obj_send_event(output, LV_EVENT_RELEASED, RT_NULL);
        (void)ft_preferences_set_audio_output_format(
            s_preferences_before.audio_output_sample_rate,
            s_preferences_before.audio_output_sample_bits,
            s_preferences_before.audio_output_channels);
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_AUDIO &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_audio_state_valid(),
                    "settings.audio.output.back", "properties -> device list");

        (void)test_click(ft_pages_test_get_audio_input_device(),
                         "settings.audio.input.open", "mic0 properties");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_AUDIO_INPUT &&
                    ft_router_depth() == 4U &&
                    ft_pages_test_audio_input_properties_valid(),
                    "settings.audio.input.route", "device -> input properties");
        input = ft_pages_test_get_audio_input_slider();
        lv_slider_set_value(input, 30, LV_ANIM_OFF);
        (void)test_event(input, LV_EVENT_VALUE_CHANGED,
                         "settings.audio.input", "15.0 dB");
        (void)test_event(input, LV_EVENT_RELEASED,
                         "settings.audio.input.apply", "mic0");
        test_record(preferences->audio_output_volume ==
                        s_settings_audio_output_before &&
                    preferences->audio_input_gain == 30U &&
                    ft_pages_test_audio_input_properties_valid(),
                    "settings.audio.state", "levels applied through RT-Thread Audio");
        lv_slider_set_value(input, s_settings_audio_input_before, LV_ANIM_OFF);
        (void)lv_obj_send_event(input, LV_EVENT_RELEASED, RT_NULL);
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_AUDIO &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_audio_state_valid(),
                    "settings.audio.input.back", "properties -> device list");
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS &&
                    ft_router_depth() == 2U,
                    "settings.back", "audio -> settings");
    }
    else if (s_control_index >= 8U && s_control_index <= 9U)
    {
        size_t category_index = s_control_index - 6U;
        ft_page_id_t expected = ft_pages_test_settings_page_id(category_index);
        lv_snprintf(detail, sizeof(detail), "category[%lu]",
                    (unsigned long)category_index);
        (void)test_click(ft_pages_test_get_settings_result(category_index),
                         "settings.open", detail);
        test_record(ft_router_current_page() == expected && ft_router_depth() == 3U,
                    "settings.route", detail);
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS &&
                    ft_router_depth() == 2U,
                    "settings.back", detail);
    }
    else if (s_control_index == 10U)
    {
        lv_obj_t *flash_button;
        lv_obj_t *sd_button;
        (void)test_click(ft_pages_test_get_settings_result(4U),
                         "settings.open", "Storage");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_STORAGE &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_storage_state_valid(),
                    "settings.storage", "two-device capacity and action view");
        test_record(ft_pages_test_storage_device_count() == 2U &&
                    ft_pages_test_get_storage_capacity_track() != RT_NULL,
                    "settings.storage.devices", "Flash and SD with visual capacity bar");
        flash_button = ft_pages_test_get_storage_device_button(0U);
        sd_button = ft_pages_test_get_storage_device_button(1U);
        (void)test_click(flash_button, "settings.storage.select", "Internal Flash");
        test_record(ft_pages_test_storage_selected_device() == 0U &&
                    ft_pages_test_storage_visual_valid() &&
                    ft_pages_test_storage_state_valid(),
                    "settings.storage.flash", "independent Flash view and actions");
        if (ft_pages_test_get_storage_format_button() != RT_NULL &&
            !lv_obj_has_state(ft_pages_test_get_storage_format_button(),
                              LV_STATE_DISABLED))
        {
            lv_obj_t *cancel;
            (void)test_click(ft_pages_test_get_storage_format_button(),
                             "settings.storage.flash.confirm", "stage 1");
            test_record(ft_pages_test_storage_confirm_stage() == 1U &&
                        ft_pages_test_storage_action_target() == 0U,
                        "settings.storage.flash.target", "Flash target captured");
            cancel = ft_pages_test_get_storage_confirm_cancel();
            s_action_count++;
            if (cancel != RT_NULL && lv_obj_is_valid(cancel))
                (void)lv_obj_send_event(cancel, LV_EVENT_CLICKED, RT_NULL);
            test_record(ft_pages_test_storage_confirm_stage() == 0U,
                        "settings.storage.flash.cancel",
                        "Flash destructive action cancelled");
        }
        (void)test_click(sd_button, "settings.storage.select", "SD card");
        test_record(ft_pages_test_storage_selected_device() == 1U &&
                    ft_pages_test_storage_visual_valid() &&
                    ft_pages_test_storage_state_valid(),
                    "settings.storage.sd", "independent SD view and actions");
    }
    else if (s_control_index == 11U)
    {
        lv_obj_t *format = ft_pages_test_get_storage_format_button();
        if (format != RT_NULL && lv_obj_is_valid(format) &&
            !lv_obj_has_state(format, LV_STATE_DISABLED))
        {
            lv_obj_t *control;
            (void)test_click(format, "settings.storage.confirm", "stage 1");
            test_record(ft_pages_test_storage_confirm_stage() == 1U &&
                        ft_pages_test_storage_action_target() == 1U &&
                        ft_pages_test_get_storage_confirm_cancel() != RT_NULL &&
                        ft_pages_test_get_storage_confirm_continue() != RT_NULL,
                        "settings.storage.confirm", "first warning shown");
            control = ft_pages_test_get_storage_confirm_continue();
            s_action_count++;
            if (control != RT_NULL && lv_obj_is_valid(control))
                (void)lv_obj_send_event(control, LV_EVENT_CLICKED, RT_NULL);
            test_record(ft_pages_test_storage_confirm_stage() == 2U &&
                        ft_pages_test_get_storage_confirm_cancel() != RT_NULL,
                        "settings.storage.confirm", "final warning shown; no erase");
            control = ft_pages_test_get_storage_confirm_cancel();
            s_action_count++;
            if (control != RT_NULL && lv_obj_is_valid(control))
                (void)lv_obj_send_event(control, LV_EVENT_CLICKED, RT_NULL);
            test_record(ft_pages_test_storage_confirm_stage() == 0U,
                        "settings.storage.cancel", "destructive action cancelled");
        }
        else
        {
            test_record(ft_pages_test_storage_state_valid(),
                        "settings.storage.gate", "format safely unavailable");
        }
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS &&
                    ft_router_depth() == 2U,
                    "settings.back", "storage -> settings");
    }
    else if (s_control_index == 12U)
    {
        ft_usb_status_t usb_status;
        lv_obj_t *usb_switch;

        (void)test_click(ft_pages_test_get_settings_result(5U),
                         "settings.open", "USB");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_USB &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_usb_state_valid(),
                    "settings.usb", "master switch, device-only role and function list");
        ft_usb_get_status(&usb_status);
        usb_switch = ft_pages_test_get_usb_enable_switch();
        if (!usb_status.active && usb_status.audio_supported)
        {
            (void)test_click(ft_pages_test_get_usb_function_button(
                                FT_USB_FUNCTION_AUDIO),
                             "settings.usb.function", "select UAC2 while off");
            lv_obj_add_state(usb_switch, LV_STATE_CHECKED);
            (void)test_event(usb_switch, LV_EVENT_VALUE_CHANGED,
                             "settings.usb.enable", "start selected UAC2");
            ft_usb_get_status(&usb_status);
            test_record(usb_status.active &&
                        usb_status.function == FT_USB_FUNCTION_AUDIO,
                        "settings.usb.enable.state", "UAC2 stack active");
            lv_obj_remove_state(usb_switch, LV_STATE_CHECKED);
            (void)test_event(usb_switch, LV_EVENT_VALUE_CHANGED,
                             "settings.usb.disable", "stop USB stack");
            ft_usb_get_status(&usb_status);
            test_record(!usb_status.active &&
                        usb_status.function == FT_USB_FUNCTION_NONE,
                        "settings.usb.disable.state", "USB stack stopped");
        }
        else
        {
            test_record(ft_pages_test_usb_state_valid(),
                        "settings.usb.switch.gate", "existing USB state preserved");
        }
        (void)test_click(ft_pages_test_get_usb_function_properties_button(
                            FT_USB_FUNCTION_STORAGE),
                         "settings.usb.storage.open", "storage properties");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_USB_STORAGE &&
                    ft_router_depth() == 4U &&
                    ft_pages_test_usb_storage_properties_valid(),
                    "settings.usb.storage.route", "function -> storage properties");
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_USB &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_usb_state_valid(),
                    "settings.usb.storage.back", "properties -> function list");
        (void)test_click(ft_pages_test_get_usb_function_properties_button(
                            FT_USB_FUNCTION_AUDIO),
                         "settings.usb.audio.open", "UAC2 properties");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_USB_AUDIO &&
                    ft_router_depth() == 4U &&
                    ft_pages_test_usb_audio_properties_valid(),
                    "settings.usb.audio.route", "function -> UAC2 properties");
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_USB &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_usb_state_valid(),
                    "settings.usb.audio.back", "properties -> function list");
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS &&
                    ft_router_depth() == 2U,
                    "settings.back", "USB -> settings");
    }
    else if (s_control_index == 13U)
    {
        (void)test_click(ft_pages_test_get_settings_result(6U),
                         "settings.open", "Time & language");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_TIME_LANGUAGE &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_get_time_format_button(0U) != RT_NULL &&
                    ft_pages_test_get_timezone_dropdown() != RT_NULL &&
                    ft_pages_test_get_language_button(0U) != RT_NULL,
                    "settings.time_language", "format, time zone and language controls");
    }
    else if (s_control_index == 14U)
    {
        (void)test_click(ft_pages_test_get_time_format_button(1U),
                         "settings.time_format", "12-hour");
        test_record(!preferences->use_24_hour,
                    "settings.time_format.state", "12-hour selected");
        (void)test_click(ft_pages_test_get_time_format_button(0U),
                         "settings.time_format", "24-hour");
        test_record(preferences->use_24_hour,
                    "settings.time_format.restore", "24-hour restored");
    }
    else if (s_control_index == 15U)
    {
        lv_obj_t *dropdown = ft_pages_test_get_timezone_dropdown();
        uint32_t timezone_index = 2U;
        lv_dropdown_set_selected(dropdown, timezone_index);
        (void)test_event(dropdown, LV_EVENT_VALUE_CHANGED,
                         "settings.timezone", "UTC+00:00");
        test_record(preferences->timezone_offset_minutes ==
                    ft_pages_test_timezone_offset(timezone_index),
                    "settings.timezone.state", "fixed UTC offset selected");
        lv_dropdown_set_selected(dropdown, 5U);
        (void)lv_obj_send_event(dropdown, LV_EVENT_VALUE_CHANGED, RT_NULL);
        test_record(preferences->timezone_offset_minutes == 480,
                    "settings.timezone.restore", "UTC+08:00 restored");
    }
    else if (s_control_index == 16U)
    {
        (void)test_click(ft_pages_test_get_language_button(FT_LANGUAGE_EN_US),
                         "settings.language", "English");
        test_record(preferences->language == FT_LANGUAGE_EN_US,
                    "settings.language.state", "English selected");
    }
    else if (s_control_index == 17U)
    {
        test_record(preferences->language == FT_LANGUAGE_EN_US &&
                    ft_router_current_page() == FT_PAGE_SETTINGS_TIME_LANGUAGE &&
                    ft_pages_test_language_surface(FT_LANGUAGE_EN_US) &&
                    ft_ui_test_language_surface(FT_LANGUAGE_EN_US),
                    "settings.language.surface", "English applied to pages, tiles and shell");
        (void)test_click(ft_pages_test_get_language_button(FT_LANGUAGE_ZH_CN),
                         "settings.language", "Simplified Chinese");
        test_record(preferences->language == FT_LANGUAGE_ZH_CN,
                    "settings.language.state", "Simplified Chinese selected");
    }
    else if (s_control_index == 18U)
    {
        test_record(preferences->language == FT_LANGUAGE_ZH_CN &&
                    ft_router_current_page() == FT_PAGE_SETTINGS_TIME_LANGUAGE &&
                    ft_pages_test_language_surface(FT_LANGUAGE_ZH_CN) &&
                    ft_ui_test_language_surface(FT_LANGUAGE_ZH_CN),
                    "settings.language.surface", "Chinese applied to pages, tiles and shell");
    }
    else if (s_control_index == 19U)
    {
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS &&
                    ft_router_depth() == 2U,
                    "settings.back", "time & language -> settings");
    }
    else if (s_control_index == 20U)
    {
        (void)test_click(ft_pages_test_get_settings_result(8U),
                         "settings.open", "System information");
        test_record(ft_router_current_page() == FT_PAGE_SYSTEM &&
                    ft_router_depth() == 3U &&
                    ft_pages_test_system_info_complete(),
                    "settings.system", "summary cards and expandable inventory");
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS &&
                    ft_router_depth() == 2U,
                    "settings.back", "system information -> settings");
    }
    else if (s_control_index == 21U)
    {
        (void)test_click(ft_pages_test_get_settings_result(9U),
                         "settings.open", "About FeatherTalk");
        test_record(ft_router_current_page() == FT_PAGE_ABOUT &&
                    ft_router_depth() == 3U,
                    "settings.about", "product and version information");
        (void)ft_router_back();
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS &&
                    ft_router_depth() == 2U,
                    "settings.back", "about -> settings");
    }
    else if (s_control_index == 22U)
    {
        (void)test_click(ft_pages_test_get_settings_result(7U),
                         "settings.open", "Personalization");
        test_record(ft_router_current_page() == FT_PAGE_SETTINGS_PERSONALIZATION &&
                    ft_router_depth() == 3U,
                    "settings.route", "personalization detail");
    }
    else if (s_control_index < preference_base + accent_count)
    {
        size_t index = s_control_index - preference_base;
        uint32_t expected = ft_pages_test_accent_rgb(index);
        lv_snprintf(detail, sizeof(detail), "accent[%lu]", (unsigned long)index);
        (void)test_click(ft_pages_test_get_accent_button(index), "settings.click", detail);
        test_record(preferences->accent_rgb == expected, "settings.accent", detail);
    }
    else if (s_control_index < preference_base + accent_count + opacity_count)
    {
        size_t index = s_control_index - preference_base - accent_count;
        uint8_t expected = ft_pages_test_opacity_value(index);
        lv_snprintf(detail, sizeof(detail), "opacity[%lu]", (unsigned long)index);
        (void)test_click(ft_pages_test_get_opacity_button(index), "settings.click", detail);
        test_record(preferences->tile_opa == expected, "settings.opacity", detail);
    }
    else if (s_control_index < preference_base + accent_count + opacity_count + background_count)
    {
        size_t index = s_control_index - preference_base - accent_count - opacity_count;
        lv_snprintf(detail, sizeof(detail), "background[%lu]", (unsigned long)index);
        (void)test_click(ft_pages_test_get_background_button(index), "settings.click", detail);
        test_record(preferences->background == (ft_background_mode_t)index,
                    "settings.background", detail);
    }
    else
    {
        if (ft_router_current_page() == FT_PAGE_SETTINGS_PERSONALIZATION)
            (void)ft_router_back();
        finish_page_controls();
        return;
    }
    s_control_index++;
}

static void run_media_test(void)
{
    const char *label;
    size_t track_count = ft_pages_test_media_track_count();
    switch (s_control_index)
    {
    case 0U:
        test_record(ft_pages_test_media_cover_ready(), "media.cover-flow",
                    "five covers, front center and inward Y-perspective sides");
        test_record(track_count >= 1U, "media.library", "selected-folder WAV/MP3 collection or visual fallback");
        test_record(ft_pages_test_get_media_directory_label() != RT_NULL,
                    "media.folder", "selected folder is visible and selectable");
        (void)test_click(ft_pages_test_get_media_prev_button(), "media.prev", "wrap to last track");
        test_record(ft_pages_test_media_track() == (int32_t)track_count - 1,
                    "media.track", "previous wraps");
        break;
    case 1U:
        if (ft_player_get_track_count() != 0U)
        {
            (void)test_click(ft_pages_test_get_media_button(), "media.play", RT_NULL);
            label = ft_pages_test_get_media_label();
            test_record(ft_pages_test_media_is_playing() && label != RT_NULL &&
                        (strstr(label, "Pause") != RT_NULL || strstr(label, "暂停") != RT_NULL),
                        "media.state", "real local WAV/MP3 starting/Pause");
        }
        else
            test_record(!ft_pages_test_media_is_playing(), "media.empty",
                        "demo covers never pretend to produce audio");
        break;
    case 2U:
        if (ft_player_get_track_count() != 0U)
        {
            (void)test_click(ft_pages_test_get_media_button(), "media.pause", RT_NULL);
            label = ft_pages_test_get_media_label();
            test_record(!ft_pages_test_media_is_playing() && label != RT_NULL &&
                        (strstr(label, "Play") != RT_NULL || strstr(label, "播放") != RT_NULL),
                        "media.state", "paused/Play");
        }
        else
            test_record(true, "media.pause.skip", "no local track");
        break;
    case 3U:
        (void)test_click(ft_pages_test_get_media_next_button(), "media.next", "last track -> 0");
        test_record(ft_pages_test_media_track() == 0, "media.track", "next wraps");
        break;
    case 4U:
        lv_slider_set_value(ft_pages_test_get_media_volume(), 35, LV_ANIM_OFF);
        (void)test_event(ft_pages_test_get_media_volume(), LV_EVENT_VALUE_CHANGED,
                         "media.volume", "35");
        test_record(ft_pages_test_media_volume() == 35, "media.volume.state", "35");
        break;
    case 5U:
    {
        ft_player_status_t before;
        ft_player_status_t after;
        (void)ft_player_get_status(&before);
        (void)test_click(ft_pages_test_get_media_loop_button(),
                         "media.folder-loop", "toggle folder wrap");
        (void)ft_player_get_status(&after);
        test_record(before.folder_loop != after.folder_loop,
                    "media.folder-loop.state", "backend follows UI toggle");
        (void)ft_player_set_folder_loop(before.folder_loop);
        break;
    }
    default:
        finish_page_controls();
        return;
    }
    s_control_index++;
}

static void run_recorder_test(void)
{
    if (s_control_index == 0U)
    {
        test_record(ft_recorder_page_test_ready(), "recorder.page",
                    "mic0 ready and AMIC2 explicitly unavailable");
        test_record(ft_recorder_page_test_selected_device() == 0U,
                    "recorder.device", "mic0 selected by default");
        test_record(ft_recorder_page_test_get_device(0U) != RT_NULL &&
                    ft_recorder_page_test_get_device(1U) != RT_NULL,
                    "recorder.devices", "two independent device selectors");
    }
    else if (s_control_index == 1U)
    {
        test_record(ft_recorder_page_test_get_record_button() != RT_NULL &&
                    lv_obj_is_valid(ft_recorder_page_test_get_record_button()),
                    "recorder.action", "start / stop-and-save control ready");
    }
    else
    {
        finish_page_controls();
        return;
    }
    s_control_index++;
}

static bool s_gallery_fixture_prepared;
static bool s_gallery_fixture_created;

static void test_write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static bool test_write_all(int file, const uint8_t *data, size_t size)
{
    size_t offset = 0U;
    while (offset < size)
    {
        int written = write(file, data + offset, size - offset);
        if (written <= 0) return false;
        offset += (size_t)written;
    }
    return true;
}

static bool test_prepare_gallery_fixture(void)
{
    enum { WIDTH = 32, HEIGHT = 24, ROW_BYTES = WIDTH * 3 };
    uint8_t header[54] = {0};
    uint8_t row[ROW_BYTES];
    struct stat status;
    int file;
    int x;
    int y;

    if (stat(FT_UI_TEST_GALLERY_FIXTURE, &status) == 0)
        return S_ISREG(status.st_mode);
    file = open(FT_UI_TEST_GALLERY_FIXTURE,
                O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (file < 0) return false;
    header[0] = 'B';
    header[1] = 'M';
    test_write_le32(header + 2U, sizeof(header) + ROW_BYTES * HEIGHT);
    test_write_le32(header + 10U, sizeof(header));
    test_write_le32(header + 14U, 40U);
    test_write_le32(header + 18U, WIDTH);
    test_write_le32(header + 22U, HEIGHT);
    header[26] = 1U;
    header[28] = 24U;
    test_write_le32(header + 34U, ROW_BYTES * HEIGHT);
    if (!test_write_all(file, header, sizeof(header))) goto fail;
    for (y = 0; y < HEIGHT; y++)
    {
        for (x = 0; x < WIDTH; x++)
        {
            row[x * 3] = (uint8_t)(32 + x * 5);
            row[x * 3 + 1] = (uint8_t)(48 + y * 7);
            row[x * 3 + 2] = (uint8_t)(192 - x * 3);
        }
        if (!test_write_all(file, row, sizeof(row))) goto fail;
    }
    close(file);
    s_gallery_fixture_created = true;
    return true;

fail:
    close(file);
    (void)unlink(FT_UI_TEST_GALLERY_FIXTURE);
    return false;
}

static void test_cleanup_gallery_fixture(void)
{
    if (s_gallery_fixture_created)
        (void)unlink(FT_UI_TEST_GALLERY_FIXTURE);
    s_gallery_fixture_created = false;
}

static void run_gallery_test(void)
{
    lv_obj_t *image;
    if (s_control_index == 0U)
    {
        if (!s_gallery_fixture_prepared)
        {
            s_gallery_fixture_prepared = true;
            if (ft_gallery_test_entry_count() == 0U)
            {
                test_record(test_prepare_gallery_fixture(),
                            "gallery.fixture",
                            "temporary real BMP created for click/decode coverage");
                (void)test_click(ft_gallery_test_get_refresh_button(),
                                 "gallery.fixture.refresh", "discover fixture");
                s_gallery_wait_steps = 0U;
                return;
            }
        }
        if (!ft_gallery_test_thumbnails_ready() && s_gallery_wait_steps < 80U)
        {
            s_gallery_wait_steps++;
            return;
        }
        test_record(ft_gallery_test_browser_visible() &&
                    ft_gallery_test_selected_source() == FT_GALLERY_SOURCE_FLASH &&
                    ft_gallery_test_path_safe() &&
                    ft_gallery_test_uses_dedicated_collection(),
                    "gallery.collection", "dedicated Flash/SD Pictures collection");
        test_record(ft_gallery_test_get_source_button(FT_GALLERY_SOURCE_FLASH) != RT_NULL &&
                    ft_gallery_test_get_source_button(FT_GALLERY_SOURCE_SD) != RT_NULL,
                    "gallery.sources", "independent Flash and SD collections");
        test_record(ft_gallery_test_thumbnails_ready(),
                    "gallery.thumbnails",
                    "RGB565 previews and formatted dimensions/size");
        (void)test_click(ft_gallery_test_get_refresh_button(),
                         "gallery.refresh", "refresh current source");
        s_gallery_wait_steps = 0U;
        s_control_index++;
        return;
    }
    if (s_control_index == 1U)
    {
        if (!ft_gallery_test_thumbnails_ready() && s_gallery_wait_steps < 80U)
        {
            s_gallery_wait_steps++;
            return;
        }
        test_record(ft_gallery_test_thumbnails_ready(),
                    "gallery.refresh.thumbnails", "previews rebuilt progressively");
        image = ft_gallery_test_get_first_image();
        if (image != RT_NULL)
        {
            test_record(ft_gallery_test_entry_hit_target(0U),
                        "gallery.hit_target",
                        "whole 72px+ row is clickable; children do not intercept");
            (void)test_click(image, "gallery.open", "first supported image");
            test_record(ft_gallery_test_viewer_visible() &&
                        ft_gallery_test_path_safe() &&
                        ft_gallery_test_preview_loading(),
                        "gallery.viewer.immediate",
                        "viewer shell and loading feedback shown before decode");
            s_gallery_wait_steps = 0U;
        }
        else
        {
            test_record(true, "gallery.empty", "no image on Flash; browser remains usable");
        }
        s_control_index++;
        return;
    }
    if (s_control_index == 2U)
    {
        if (ft_gallery_test_viewer_visible() &&
            ft_gallery_test_preview_loading() && s_gallery_wait_steps < 80U)
        {
            s_gallery_wait_steps++;
            return;
        }
        if (ft_gallery_test_viewer_visible())
        {
            test_record(ft_gallery_test_path_safe() &&
                        !ft_gallery_test_preview_loading() &&
                        ft_gallery_test_current_image_verified() &&
                        ft_gallery_test_current_image_cached(),
                        "gallery.viewer", "non-black RGB565 cached preview");
            test_record(ft_gallery_can_open_file(ft_gallery_test_current_file()),
                        "gallery.files.handoff",
                        "Files can route this image through the Gallery viewer");
            if (ft_gallery_test_current_image_verified())
            {
                (void)test_click(ft_gallery_test_get_wallpaper_button(),
                                 "gallery.wallpaper", "set current photo");
                test_record(ft_preferences_get()->background == FT_BACKGROUND_CUSTOM &&
                            strcmp(ft_preferences_get()->wallpaper_path,
                                   ft_gallery_test_current_file()) == 0 &&
                            ft_ui_test_wallpaper_cached(),
                            "gallery.wallpaper.state",
                            "custom background uses non-black RGB565 cache");
                (void)test_alert_close("gallery.wallpaper.alert.close");
            }
            (void)test_click(ft_gallery_test_get_delete_button(),
                             "gallery.delete.request",
                             "viewer deletion requires confirmation");
            test_record(ft_gallery_test_delete_confirmation_visible(),
                        "gallery.delete.confirm", "current image is explicit");
            if (ft_gallery_test_get_delete_cancel() != RT_NULL &&
                lv_obj_is_valid(ft_gallery_test_get_delete_cancel()))
            {
                s_action_count++;
                (void)lv_obj_send_event(ft_gallery_test_get_delete_cancel(),
                                        LV_EVENT_CLICKED, RT_NULL);
            }
            test_record(!ft_gallery_test_delete_confirmation_visible() &&
                        ft_gallery_test_viewer_visible(),
                        "gallery.delete.cancel", "closed; image retained");
        }
        else
        {
            test_record(true, "gallery.viewer.skip", "no Flash image to decode");
        }
        s_control_index++;
        return;
    }
    if (s_control_index == 3U)
    {
        if (ft_gallery_test_viewer_visible())
            (void)test_click(ft_gallery_test_get_close_button(),
                             "gallery.close", "return to browser");
        (void)test_click(ft_gallery_test_get_source_button(FT_GALLERY_SOURCE_SD),
                         "gallery.source", "SD card");
        test_record(ft_gallery_test_selected_source() == FT_GALLERY_SOURCE_SD &&
                    ft_gallery_test_browser_visible() &&
                    ft_gallery_test_path_safe() &&
                    ft_gallery_test_uses_dedicated_collection(),
                    "gallery.sd", "dedicated SD Pictures collection or unavailable state");
        (void)test_click(ft_gallery_test_get_source_button(FT_GALLERY_SOURCE_FLASH),
                         "gallery.source", "Internal Flash");
        test_cleanup_gallery_fixture();
        test_record(!s_gallery_fixture_created,
                    "gallery.fixture.cleanup", "temporary image removed");
        s_control_index++;
        return;
    }
    finish_page_controls();
}

static void run_files_test(void)
{
    if (s_control_index == 0U)
    {
        test_record(ft_pages_test_files_browser_ready(), "files.browser",
                    "path/status/up/refresh/list/mount monitor");
        test_record(ft_pages_test_files_at_root(), "files.root",
                    "/ with flash and sdcard device directories");
        test_record(ft_pages_test_files_entry_count() >= 2U,
                    "files.devices", "flash + sdcard entries");
        test_record(ft_storage_test_delete_contract(),
                    "files.delete.contract",
                    "file + recursive directory removed; volume roots rejected");
        test_record(ft_storage_test_clipboard_contract(),
                    "files.clipboard.contract",
                    "copy, move, collision rename, and self-nesting rejection");
        test_record(ft_storage_test_name_contract(),
                    "files.name.contract",
                    "create folder, rename, collision/path guards, and root protection");
        s_files_before = ft_pages_test_files_refresh_count();
        (void)test_click(ft_pages_test_get_files_refresh_button(), "files.refresh", RT_NULL);
        test_record(ft_pages_test_files_refresh_count() == s_files_before + 1U,
                    "files.refresh.count", "incremented");
        s_control_index++;
        return;
    }
    if (s_control_index == 1U)
    {
        lv_obj_t *device_entry = ft_pages_test_get_files_first_entry();
        s_action_count++;
        if (device_entry != RT_NULL && lv_obj_is_valid(device_entry))
            (void)lv_obj_send_event(device_entry, LV_EVENT_LONG_PRESSED, RT_NULL);
        test_record(ft_router_current_page() == FT_PAGE_FILES &&
                    ft_pages_test_files_at_root() &&
                    ft_pages_test_storage_selected_device() == 0U &&
                    ft_pages_test_storage_action_target() == 0U &&
                    ft_pages_test_storage_confirm_stage() == 1U,
                    "files.device.format", "volume root opens in-place locked format confirmation");
        test_record(ft_pages_test_storage_confirm_fonts(),
                    "files.device.format.fonts",
                    "format action labels use the Simplified Chinese font");
        if (ft_pages_test_get_storage_confirm_continue() != RT_NULL &&
            lv_obj_is_valid(ft_pages_test_get_storage_confirm_continue()))
        {
            s_action_count++;
            (void)lv_obj_send_event(ft_pages_test_get_storage_confirm_continue(),
                                    LV_EVENT_CLICKED, RT_NULL);
        }
        test_record(ft_router_current_page() == FT_PAGE_FILES &&
                    ft_pages_test_files_at_root() &&
                    ft_pages_test_storage_action_target() == 0U &&
                    ft_pages_test_storage_confirm_stage() == 2U,
                    "files.device.format.continue",
                    "final confirmation remains over Files; no erase requested");
        if (ft_pages_test_get_storage_confirm_cancel() != RT_NULL &&
            lv_obj_is_valid(ft_pages_test_get_storage_confirm_cancel()))
        {
            s_action_count++;
            (void)lv_obj_send_event(ft_pages_test_get_storage_confirm_cancel(),
                                    LV_EVENT_CLICKED, RT_NULL);
        }
        if (device_entry != RT_NULL && lv_obj_is_valid(device_entry))
            (void)lv_obj_send_event(device_entry, LV_EVENT_CLICKED, RT_NULL);
        test_record(ft_router_current_page() == FT_PAGE_FILES &&
                    ft_pages_test_files_at_root(),
                    "files.device.format.cancel", "no format; Files never changed page");
        s_control_index++;
        return;
    }
    if (s_control_index == 2U)
    {
        lv_obj_t *device_entry = ft_pages_test_get_files_first_entry();
        s_action_count++;
        if (device_entry != RT_NULL && lv_obj_is_valid(device_entry))
            (void)lv_obj_send_event(device_entry, LV_EVENT_CLICKED, RT_NULL);
        test_record(!ft_pages_test_files_at_root() &&
                    ft_pages_test_files_mounted(),
                    "files.device.open", "opened Internal Flash storage");
        s_control_index++;
        return;
    }
    if (s_control_index == 3U)
    {
        lv_obj_t *directory = ft_pages_test_get_files_first_directory_entry();
        lv_obj_t *entry = ft_pages_test_get_files_first_content_entry();
        if (directory != RT_NULL) entry = directory;
        test_record(ft_pages_test_files_rows_have_no_permanent_actions(),
                    "files.actions.hidden", "no persistent row action buttons");
        if (entry != RT_NULL && lv_obj_check_type(entry, &lv_button_class))
        {
            s_action_count++;
            (void)lv_obj_send_event(entry, LV_EVENT_LONG_PRESSED, RT_NULL);
            test_record(ft_pages_test_files_action_visible(),
                        "files.longpress.menu",
                        "open/cut/copy/rename/new-folder/paste/delete actions appear on demand");
            test_record(ft_pages_test_files_action_fonts(),
                        "files.longpress.fonts",
                        "all context labels use the Simplified Chinese font");
            test_record(ft_pages_test_files_action_layout(),
                        "files.longpress.layout",
                        "four quick actions above supported vertical rows");
            test_record(ft_pages_test_get_files_action_paste() != RT_NULL &&
                        lv_obj_has_state(ft_pages_test_get_files_action_paste(),
                                         LV_STATE_DISABLED),
                        "files.paste.disabled", "empty clipboard");
            if (directory != RT_NULL)
            {
                test_record(ft_pages_test_files_context_is_directory() &&
                            ft_pages_test_get_files_action_new_folder() != RT_NULL &&
                            !lv_obj_has_flag(ft_pages_test_get_files_action_new_folder(),
                                             LV_OBJ_FLAG_HIDDEN),
                            "files.directory.actions",
                            "directory supports recursive clipboard/delete and creating children");
            }
            s_action_count++;
            (void)lv_obj_send_event(ft_pages_test_get_files_action_rename(),
                                    LV_EVENT_CLICKED, RT_NULL);
            test_record(ft_pages_test_files_name_editor_visible(true),
                        "files.rename.editor", "keyboard editor opens with current name");
            if (ft_pages_test_get_files_name_cancel() != RT_NULL &&
                lv_obj_is_valid(ft_pages_test_get_files_name_cancel()))
            {
                s_action_count++;
                (void)lv_obj_send_event(ft_pages_test_get_files_name_cancel(),
                                        LV_EVENT_CLICKED, RT_NULL);
            }
            (void)lv_obj_send_event(entry, LV_EVENT_LONG_PRESSED, RT_NULL);
            s_action_count++;
            (void)lv_obj_send_event(ft_pages_test_get_files_action_delete(),
                                    LV_EVENT_CLICKED, RT_NULL);
            test_record(true, "files.delete.request",
                        "long-press action requires confirmation");
            test_record(ft_pages_test_files_delete_confirmation_visible(),
                        "files.delete.confirm", "explicit target and confirmation");
            s_action_count++;
            if (ft_pages_test_get_files_delete_cancel() != RT_NULL &&
                lv_obj_is_valid(ft_pages_test_get_files_delete_cancel()))
                (void)lv_obj_send_event(ft_pages_test_get_files_delete_cancel(),
                                        LV_EVENT_CLICKED, RT_NULL);
            test_record(!ft_pages_test_files_delete_confirmation_visible(),
                        "files.delete.cancel", "closed; nothing deleted");
        }
        else
        {
            test_record(true, "files.delete.empty",
                        "device has no entries; long-press action is not shown");
        }
        s_control_index++;
        return;
    }
    if (s_control_index == 4U)
    {
        lv_obj_t *list = ft_pages_test_get_files_list();
        if (list != RT_NULL && lv_obj_is_valid(list))
        {
            s_action_count++;
            (void)lv_obj_send_event(list, LV_EVENT_LONG_PRESSED, RT_NULL);
            test_record(ft_pages_test_files_action_visible() &&
                        ft_pages_test_get_files_action_new_folder() != RT_NULL &&
                        !lv_obj_has_flag(ft_pages_test_get_files_action_new_folder(),
                                         LV_OBJ_FLAG_HIDDEN),
                        "files.folder.create", "blank area offers New folder and Paste");
            test_record(ft_pages_test_files_action_layout(),
                        "files.folder.layout",
                        "blank-area actions are a single vertical menu");
            s_action_count++;
            (void)lv_obj_send_event(ft_pages_test_get_files_action_new_folder(),
                                    LV_EVENT_CLICKED, RT_NULL);
            test_record(ft_pages_test_files_name_editor_visible(false),
                        "files.folder.editor", "new-folder keyboard editor opens");
            if (ft_pages_test_get_files_name_cancel() != RT_NULL &&
                lv_obj_is_valid(ft_pages_test_get_files_name_cancel()))
            {
                s_action_count++;
                (void)lv_obj_send_event(ft_pages_test_get_files_name_cancel(),
                                        LV_EVENT_CLICKED, RT_NULL);
            }
        }
        s_control_index++;
        return;
    }
    if (s_control_index == 5U)
    {
        (void)test_click(ft_pages_test_get_files_up_button(),
                         "files.up", "return to storage devices");
        test_record(ft_pages_test_files_at_root(), "files.root.restore",
                    "ready for route Back");
        s_control_index++;
        return;
    }
    finish_page_controls();
}

static void run_page_control_test(const ft_app_descriptor_t *app)
{
    if (app->page_id == FT_PAGE_SYSTEM)
    {
        test_record(ft_pages_test_system_info_complete(), "system.inventory",
                    "summary cards/specifications/collapsed details");
        finish_page_controls();
    }
    else if (app->page_id == FT_PAGE_SETTINGS) run_settings_test();
    else if (app->page_id == FT_PAGE_MEDIA) run_media_test();
    else if (app->page_id == FT_PAGE_RECORDER) run_recorder_test();
    else if (app->page_id == FT_PAGE_GALLERY) run_gallery_test();
    else if (app->page_id == FT_PAGE_FILES) run_files_test();
    else
    {
        test_record(current_app_is(app), "page.content", app->tile.name);
        finish_page_controls();
    }
}

static void ui_test_timer_cb(lv_timer_t *timer)
{
    const ft_app_descriptor_t *apps;
    const ft_app_descriptor_t *app;
    size_t app_count;
    if (!s_step_period_active)
    {
        lv_timer_set_period(timer, FT_UI_TEST_STEP_MS);
        s_step_period_active = true;
    }
    apps = ft_apps_get(&app_count);
    app = s_app_index < app_count ? &apps[s_app_index] : RT_NULL;
    switch (s_test_phase)
    {
    case FT_TEST_PENDING:
    {
        const ft_ui_layout_t *layout = ft_layout_get();
        ft_preferences_store_status_t store_status;
        char detail[64];
        lv_snprintf(detail, sizeof(detail), "%ldx%ld %u columns scale %ld%%",
                    (long)layout->screen_width, (long)layout->screen_height,
                    layout->tile_columns, (long)layout->scale_percent);
        test_record(home_start_is_ready(), "shell.start", "home/start");
        test_record(app_count == 5U, "registry.count", "5 standalone applications");
        test_record(ft_preferences_store_get_status(&store_status) == RT_EOK &&
                    store_status.initialized && store_status.worker_started &&
                    store_status.test_suspended,
                    "preferences.store", "A/B Flash store active; tests suspended");
        test_record(ft_pages_test_icon_assignments_unique(), "icons.entity.unique",
                    "apps/settings/cards use distinct semantic icons");
        test_record(ft_layout_profiles_self_test(), "layout.profiles",
                    "240x320 through 720x1280 + landscape");
        test_record(ft_vector_font_metrics_self_test(), "font.metrics.all",
                    "7586 glyphs x 8..48 px share exact pixel bounds");
        test_record(layout->tile_column_width > 0 &&
                    layout->status_bar_height + layout->nav_bar_height < layout->screen_height,
                    "layout.current", detail);
        test_record(ft_ui_test_status_monitor_visible(), "status.monitor",
                    "present FPS / refresh Hz / RT heap");
        test_record(ft_ui_test_shell_seams_closed(), "shell.seams",
                    "status/content/navigation are pixel-contiguous");
        ft_ui_test_notification_reset();
        s_shade_render_before = ft_ui_test_notification_render_count();
        ft_metrics_route_baseline();
        s_test_phase = FT_TEST_NOTIFY_SHOW;
        break;
    }
    case FT_TEST_NOTIFY_SHOW:
        (void)test_click(ft_ui_test_get_status_bar(), "status.click", "show notifications");
        s_test_phase = FT_TEST_NOTIFY_SHOW_VERIFY;
        break;
    case FT_TEST_NOTIFY_SHOW_VERIFY:
        test_record(ft_ui_test_notification_is_visible(), "notification.state", "visible");
        test_record(ft_ui_test_notification_y() == ft_layout_get()->status_bar_height,
                    "notification.position", "fully open");
        test_record(ft_ui_test_notification_mask_visible(), "notification.mask", "visible");
        test_record(ft_ui_test_notification_render_count() == s_shade_render_before,
                    "notification.render.cached", "empty queue was not rebuilt");
        s_test_phase = FT_TEST_NOTIFY_DRAG_CLOSE;
        break;
    case FT_TEST_NOTIFY_DRAG_CLOSE:
    {
        int32_t open_y = ft_layout_get()->status_bar_height;
        int32_t closed_y = -ft_layout_get()->notification_height;
        int32_t pointer_y = open_y + ft_layout_px(80);
        int32_t target_y = pointer_y - ft_layout_px(100);
        uint32_t applied_before = ft_ui_test_notification_drag_applied();
        uint32_t skipped_before = ft_ui_test_notification_drag_skipped();
        uint32_t mask_before = ft_ui_test_notification_mask_applied();
        s_action_count++;
        ft_ui_test_notification_drag_begin(pointer_y);
        ft_ui_test_notification_drag_move(target_y);
        test_record(ft_ui_test_notification_y() < open_y &&
                    ft_ui_test_notification_y() > closed_y,
                    "notification.follow", "upward intermediate Y");
        ft_ui_test_notification_drag_move(target_y);
        test_record(ft_ui_test_notification_drag_applied() == applied_before + 1U &&
                    ft_ui_test_notification_drag_skipped() == skipped_before + 1U,
                    "notification.drag.dedup", "same pointer Y caused no redraw");
        test_record(ft_ui_test_notification_mask_applied() <= mask_before + 1U,
                    "notification.mask.quantized", "mask updated at most once");
        ft_ui_test_notification_drag_end();
        s_test_phase = FT_TEST_NOTIFY_DRAG_CLOSE_VERIFY;
        break;
    }
    case FT_TEST_NOTIFY_DRAG_CLOSE_VERIFY:
        test_record(!ft_ui_test_notification_is_visible(), "notification.state", "hidden");
        test_record(ft_ui_test_notification_y() == -ft_layout_get()->notification_height,
                    "notification.position", "snapped closed");
        s_test_phase = FT_TEST_NOTIFY_DRAG_OPEN;
        break;
    case FT_TEST_NOTIFY_DRAG_OPEN:
    {
        int32_t closed_y = -ft_layout_get()->notification_height;
        int32_t open_y = ft_layout_get()->status_bar_height;
        int32_t pointer_y = 0;
        s_action_count++;
        ft_ui_test_notification_drag_begin(pointer_y);
        ft_ui_test_notification_drag_move(pointer_y + ft_layout_px(100));
        test_record(ft_ui_test_notification_y() > closed_y &&
                    ft_ui_test_notification_y() < open_y,
                    "notification.follow", "downward intermediate Y");
        ft_ui_test_notification_drag_end();
        s_test_phase = FT_TEST_NOTIFY_DRAG_OPEN_VERIFY;
        break;
    }
    case FT_TEST_NOTIFY_DRAG_OPEN_VERIFY:
        test_record(ft_ui_test_notification_is_visible(), "notification.state", "drag opened");
        test_record(ft_ui_test_notification_y() == ft_layout_get()->status_bar_height,
                    "notification.position", "snapped open");
        s_test_phase = FT_TEST_NOTIFY_BACK_CLOSE;
        break;
    case FT_TEST_NOTIFY_BACK_CLOSE:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_BACK), "nav.back", "close notifications");
        s_test_phase = FT_TEST_NOTIFY_BACK_CLOSE_VERIFY;
        break;
    case FT_TEST_NOTIFY_BACK_CLOSE_VERIFY:
        test_record(!ft_ui_test_notification_is_visible(), "notification.state", "Back closed");
        test_record(!ft_ui_test_notification_mask_visible(), "notification.mask", "Back hidden");
        test_record(home_start_is_ready(), "notification.route", "route unchanged");
        s_test_phase = FT_TEST_NOTIFY_FLING_OPEN;
        break;
    case FT_TEST_NOTIFY_FLING_OPEN:
        s_action_count++;
        ft_ui_test_notification_fling(0, ft_layout_px(50), 20U, 0U);
        test_record(true, "notification.fling", "fast downward, short distance");
        s_test_phase = FT_TEST_NOTIFY_FLING_OPEN_VERIFY;
        break;
    case FT_TEST_NOTIFY_FLING_OPEN_VERIFY:
        test_record(ft_ui_test_notification_is_visible(), "notification.velocity", "opened by speed");
        test_record(ft_ui_test_notification_y() == ft_layout_get()->status_bar_height,
                    "notification.position", "fling open");
        s_test_phase = FT_TEST_NOTIFY_FLING_CLOSE;
        break;
    case FT_TEST_NOTIFY_FLING_CLOSE:
        s_action_count++;
        ft_ui_test_notification_fling(ft_layout_px(200), ft_layout_px(150), 20U, 0U);
        test_record(true, "notification.fling", "fast upward, short distance");
        s_test_phase = FT_TEST_NOTIFY_FLING_CLOSE_VERIFY;
        break;
    case FT_TEST_NOTIFY_FLING_CLOSE_VERIFY:
        test_record(!ft_ui_test_notification_is_visible(), "notification.velocity", "closed by speed");
        test_record(!ft_ui_test_notification_mask_visible(), "notification.mask", "fling hidden");
        s_test_phase = FT_TEST_QUICK_STATUS_VERIFY;
        break;
    case FT_TEST_QUICK_STATUS_VERIFY:
        test_record(ft_ui_test_status_radio_icons_present(),
                    "status.radios", "Wi-Fi and Bluetooth icons present");
        test_record(ft_ui_test_wifi_signal_icon(false, 80U) == FT_ICON_WIFI_OFF &&
                    ft_ui_test_wifi_signal_icon(true, 20U) == FT_ICON_WIFI_WEAK &&
                    ft_ui_test_wifi_signal_icon(true, 50U) == FT_ICON_WIFI_MEDIUM &&
                    ft_ui_test_wifi_signal_icon(true, 90U) == FT_ICON_WIFI,
                    "status.wifi.signal", "off / weak / medium / strong assets");
        test_record(ft_ui_test_quick_available(FEATHERTALK_QUICK_BRIGHTNESS),
                    "quick.brightness", "real PWM available");
        test_record(ft_ui_test_brightness() == 60U,
                    "quick.brightness.initial", "80% duty maps to UI 60%");
        /* Services may now be available. Do not assume the old all-disabled
         * bring-up fixture or switch off a real connection during UI tests. */
        for (unsigned control = 0; control < FEATHERTALK_QUICK_COUNT; control++) {
            if (control == FEATHERTALK_QUICK_BRIGHTNESS) continue;
            test_record(ft_ui_test_quick_available(control) ||
                        !ft_ui_test_quick_connected(control),
                        "quick.capabilities", "unavailable radios cannot report a connection");
        }
        s_test_phase = FT_TEST_QUICK_UNAVAILABLE_CLICK;
        break;
    case FT_TEST_QUICK_UNAVAILABLE_CLICK:
        for (unsigned control = 0; control < FEATHERTALK_QUICK_COUNT; control++) {
            if (control == FEATHERTALK_QUICK_BRIGHTNESS ||
                ft_ui_test_quick_available(control)) continue;
            (void)test_click(ft_ui_test_get_quick_button(control),
                             "quick.unavailable", "disabled service");
            test_record(!ft_ui_test_quick_enabled(control),
                        "quick.unavailable.state", "unchanged");
        }
        s_test_phase = FT_TEST_QUICK_BRIGHTNESS_CLICK;
        break;
    case FT_TEST_QUICK_BRIGHTNESS_CLICK:
        (void)test_click(ft_ui_test_get_quick_button(FEATHERTALK_QUICK_BRIGHTNESS),
                         "quick.brightness", "current -> 30");
        test_record(ft_ui_test_brightness() == 30U, "quick.brightness.state", "30%");
        s_test_phase = FT_TEST_QUICK_BRIGHTNESS_SLIDER;
        break;
    case FT_TEST_QUICK_BRIGHTNESS_SLIDER:
        lv_slider_set_value(ft_ui_test_get_brightness_slider(), 65, LV_ANIM_OFF);
        (void)test_event(ft_ui_test_get_brightness_slider(), LV_EVENT_VALUE_CHANGED,
                         "quick.brightness.slider", "65");
        test_record(ft_ui_test_brightness() == 65U, "quick.brightness.state", "65%");
        s_test_phase = FT_TEST_QUICK_BRIGHTNESS_MINIMUM;
        break;
    case FT_TEST_QUICK_BRIGHTNESS_MINIMUM:
        lv_slider_set_value(ft_ui_test_get_brightness_slider(), 0, LV_ANIM_OFF);
        (void)test_event(ft_ui_test_get_brightness_slider(), LV_EVENT_VALUE_CHANGED,
                         "quick.brightness.slider", "0");
        test_record(ft_ui_test_brightness() == 0U, "quick.brightness.state",
                    "UI minimum / hardware 50% duty");
        s_test_phase = FT_TEST_QUICK_BRIGHTNESS_RESTORE;
        break;
    case FT_TEST_QUICK_BRIGHTNESS_RESTORE:
        lv_slider_set_value(ft_ui_test_get_brightness_slider(), 65, LV_ANIM_OFF);
        (void)test_event(ft_ui_test_get_brightness_slider(), LV_EVENT_VALUE_CHANGED,
                         "quick.brightness.slider", "restore 65");
        test_record(ft_ui_test_brightness() == 65U, "quick.brightness.state", "restored 65%");
        s_test_phase = FT_TEST_QUEUE_ADD;
        break;
    case FT_TEST_QUEUE_ADD:
        s_action_count += 2U;
        /* Exercise the real Simplified-Chinese notification rendering path.
         * Keep product and radio keywords untranslated by design. */
        feathertalk_ui_notify("FeatherTalk", "系统通知",
                              "Wi-Fi 与蓝牙服务状态测试。");
        feathertalk_ui_notify("消息", "第二条通知",
                              "左右滑动删除，或点击清除。");
        test_record(ft_ui_test_notification_count() == 2U &&
                    ft_ui_test_notification_unread() == 2U,
                    "notification.queue", "2 total / 2 unread");
        s_test_phase = FT_TEST_QUEUE_OPEN;
        break;
    case FT_TEST_QUEUE_OPEN:
        (void)test_click(ft_ui_test_get_status_bar(), "status.click", "open queued notifications");
        s_test_phase = FT_TEST_QUEUE_OPEN_VERIFY;
        break;
    case FT_TEST_QUEUE_OPEN_VERIFY:
        test_record(ft_ui_test_notification_is_visible() &&
                    ft_ui_test_notification_count() == 2U &&
                    ft_ui_test_notification_unread() == 0U,
                    "notification.read", "open marks all read");
        s_test_phase = FT_TEST_QUEUE_SWIPE_DELETE;
        break;
    case FT_TEST_QUEUE_SWIPE_DELETE:
        s_action_count++;
        test_record(ft_ui_test_notification_remove(0U) &&
                    ft_ui_test_notification_count() == 1U,
                    "notification.swipe", "single item deleted");
        s_test_phase = FT_TEST_QUEUE_CLEAR;
        break;
    case FT_TEST_QUEUE_CLEAR:
        (void)test_click(ft_ui_test_get_notification_clear(),
                         "notification.clear", "clear all");
        test_record(ft_ui_test_notification_count() == 0U,
                    "notification.queue", "empty");
        s_test_phase = FT_TEST_QUEUE_OVERFLOW;
        break;
    case FT_TEST_QUEUE_OVERFLOW:
    {
        size_t i;
        s_action_count += FT_NOTIFICATION_CAPACITY + 2U;
        for (i = 0U; i < FT_NOTIFICATION_CAPACITY + 2U; i++)
            feathertalk_ui_notify("Overflow", "Bounded queue", "Newest items are retained");
        test_record(ft_ui_test_notification_count() == FT_NOTIFICATION_CAPACITY &&
                    ft_ui_test_notification_unread() == FT_NOTIFICATION_CAPACITY,
                    "notification.overflow", "bounded at 8 newest items");
        ft_ui_test_notification_reset();
        s_test_phase = FT_TEST_QUEUE_MASK_CLOSE;
        break;
    }
    case FT_TEST_QUEUE_MASK_CLOSE:
        (void)test_click(ft_ui_test_get_notification_mask(),
                         "notification.mask.click", "close shade");
        s_test_phase = FT_TEST_QUEUE_MASK_CLOSE_VERIFY;
        break;
    case FT_TEST_QUEUE_MASK_CLOSE_VERIFY:
        test_record(!ft_ui_test_notification_is_visible() &&
                    !ft_ui_test_notification_mask_visible(),
                    "notification.mask", "closed and hidden");
        s_test_phase = FT_TEST_QUEUE_HOME_OPEN;
        break;
    case FT_TEST_QUEUE_HOME_OPEN:
        (void)test_click(ft_ui_test_get_status_bar(), "status.click", "open before Home");
        s_test_phase = FT_TEST_QUEUE_HOME_CLOSE;
        break;
    case FT_TEST_QUEUE_HOME_CLOSE:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_HOME),
                         "nav.home", "close notifications");
        s_test_phase = FT_TEST_QUEUE_HOME_CLOSE_VERIFY;
        break;
    case FT_TEST_QUEUE_HOME_CLOSE_VERIFY:
        test_record(!ft_ui_test_notification_is_visible() &&
                    !ft_ui_test_notification_mask_visible() && home_start_is_ready(),
                    "notification.home", "closed / Home unchanged");
        s_test_phase = FT_TEST_ALERT_CREATE;
        break;
    case FT_TEST_ALERT_CREATE:
        s_action_count++;
        feathertalk_ui_alert("Auto test", "Alert lifecycle test");
        test_record(true, "alert.create", "modal");
        s_test_phase = FT_TEST_ALERT_VERIFY;
        break;
    case FT_TEST_ALERT_VERIFY:
        test_record(ft_ui_test_get_alert_button() != RT_NULL, "alert.state", "visible");
        s_test_phase = FT_TEST_ALERT_CLOSE;
        break;
    case FT_TEST_ALERT_CLOSE:
        (void)test_alert_close("alert.close");
        s_test_phase = FT_TEST_SEARCH_SHADE_OPEN;
        break;
    case FT_TEST_SEARCH_SHADE_OPEN:
        (void)test_click(ft_ui_test_get_status_bar(), "status.click", "open before Search");
        s_test_phase = FT_TEST_SEARCH_OPEN;
        break;
    case FT_TEST_SEARCH_OPEN:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_SEARCH), "nav.search", RT_NULL);
        s_test_phase = FT_TEST_SEARCH_VERIFY;
        break;
    case FT_TEST_SEARCH_VERIFY:
        test_record(ft_router_current_page() == FT_PAGE_SEARCH && ft_router_depth() == 2U,
                    "search.route", "depth 2");
        test_record(!ft_ui_test_notification_is_visible() &&
                    !ft_ui_test_notification_mask_visible(),
                    "search.shade", "Search closed notifications");
        test_record(ft_pages_test_search_visible_count() == app_count,
                    "search.results", "all applications");
        (void)test_event(ft_pages_test_get_search_box(), LV_EVENT_CLICKED,
                         "search.keyboard.open", "tap search box");
        s_test_phase = FT_TEST_SEARCH_KEYBOARD_VERIFY;
        break;
    case FT_TEST_SEARCH_KEYBOARD_VERIFY:
        test_record(ft_pages_test_search_keyboard_visible(),
                    "search.keyboard.visible", "fixed overlay shown");
        test_record(ft_pages_test_search_keyboard_overlay_ok(),
                    "search.keyboard.geometry", "bottom overlay / no page scroll");
        test_record(ft_pages_test_get_search_keyboard_hide() != RT_NULL &&
                    lv_obj_is_valid(ft_pages_test_get_search_keyboard_hide()),
                    "search.keyboard.control", "collapse button available");
        s_test_phase = FT_TEST_SEARCH_KEYBOARD_HIDE;
        break;
    case FT_TEST_SEARCH_KEYBOARD_HIDE:
        (void)test_click(ft_pages_test_get_search_keyboard_hide(),
                         "search.keyboard.hide", "collapse button");
        s_test_phase = FT_TEST_SEARCH_KEYBOARD_HIDE_VERIFY;
        break;
    case FT_TEST_SEARCH_KEYBOARD_HIDE_VERIFY:
        test_record(!ft_pages_test_search_keyboard_visible(),
                    "search.keyboard.hidden", "collapsed");
        (void)test_event(ft_pages_test_get_search_box(), LV_EVENT_CLICKED,
                         "search.keyboard.reopen", "same focused field");
        s_test_phase = FT_TEST_SEARCH_KEYBOARD_REOPEN_VERIFY;
        break;
    case FT_TEST_SEARCH_KEYBOARD_REOPEN_VERIFY:
        test_record(ft_pages_test_search_keyboard_visible(),
                    "search.keyboard.reopened", "tap reopens");
        (void)test_event(ft_pages_test_get_search_keyboard(), LV_EVENT_CANCEL,
                         "search.keyboard.cancel", "built-in dismiss key");
        s_test_phase = FT_TEST_SEARCH_KEYBOARD_CANCEL_VERIFY;
        break;
    case FT_TEST_SEARCH_KEYBOARD_CANCEL_VERIFY:
        test_record(!ft_pages_test_search_keyboard_visible(),
                    "search.keyboard.cancelled", "built-in key collapsed");
        s_test_phase = FT_TEST_SEARCH_FILTER;
        break;
    case FT_TEST_SEARCH_FILTER:
        lv_textarea_set_text(ft_pages_test_get_search_box(), "med");
        (void)test_event(ft_pages_test_get_search_box(), LV_EVENT_VALUE_CHANGED,
                         "search.input", "med");
        s_test_phase = FT_TEST_SEARCH_FILTER_VERIFY;
        break;
    case FT_TEST_SEARCH_FILTER_VERIFY:
        test_record(ft_pages_test_search_visible_count() == 1U, "search.filter", "1 Media result");
        s_test_phase = FT_TEST_SEARCH_RESULT;
        break;
    case FT_TEST_SEARCH_RESULT:
        (void)test_click(ft_pages_test_get_search_result(FT_UI_TEST_MEDIA_INDEX),
                         "search.result", "Media");
        s_test_phase = FT_TEST_SEARCH_RESULT_VERIFY;
        break;
    case FT_TEST_SEARCH_RESULT_VERIFY:
        test_record(ft_router_current_page() == FT_PAGE_MEDIA && ft_router_depth() == 3U,
                    "search.push", "Media depth 3");
        s_test_phase = FT_TEST_SEARCH_HOME;
        break;
    case FT_TEST_SEARCH_HOME:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_HOME), "nav.home", "from search result");
        s_test_phase = FT_TEST_SEARCH_HOME_VERIFY;
        break;
    case FT_TEST_SEARCH_HOME_VERIFY:
        test_record(home_start_is_ready(), "search.home", "released search and result");
        s_test_phase = FT_TEST_ROOT_APPS_SHOW;
        break;
    case FT_TEST_ROOT_APPS_SHOW:
        test_record(ft_pages_test_home_swipe_ready(), "tileview.gesture",
                    "horizontal touch scrolling enabled");
        s_action_count++;
        ft_pages_show_all_apps();
        test_record(true, "tileview.api", "show all apps");
        s_test_phase = FT_TEST_ROOT_APPS_VERIFY;
        break;
    case FT_TEST_ROOT_APPS_VERIFY:
        test_record(ft_pages_test_apps_is_active(), "tileview.state", "all apps");
        s_test_phase = FT_TEST_ROOT_BACK;
        break;
    case FT_TEST_ROOT_BACK:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_BACK), "nav.back", "root");
        s_test_phase = FT_TEST_ROOT_BACK_VERIFY;
        break;
    case FT_TEST_ROOT_BACK_VERIFY:
        test_record(home_start_is_ready(), "tileview.state", "start");
        s_test_phase = FT_TEST_TILE_MODEL;
        break;
    case FT_TEST_TILE_MODEL:
        test_record(apps[0].tile.column_span == 2U && apps[0].tile.row_span == 1U &&
                    apps[0].tile.opacity == 255U &&
                    apps[0].app.app_icon == FT_ICON_SETTINGS,
                    "tile.model.common", "Settings 2x1 / opacity / icon");
        test_record(apps[FT_UI_TEST_MEDIA_INDEX].app.loop_enabled &&
                    apps[FT_UI_TEST_MEDIA_INDEX].app.live_content != RT_NULL &&
                    apps[FT_UI_TEST_MEDIA_INDEX].app.loop_period_ms > 0U,
                    "tile.model.private", "Media owns looping content");
        test_record(ft_pages_test_tile_layout_valid(), "tile.layout", "initial grid valid");
        s_test_phase = FT_TEST_TILE_LONG_PRESS;
        break;
    case FT_TEST_TILE_LONG_PRESS:
    {
        lv_obj_t *tile = ft_pages_test_get_start_button(0U);
        (void)test_event(tile, LV_EVENT_PRESSED,
                         "tile.press", "wait for long-press threshold");
        test_record(!ft_pages_test_tile_editing() && ft_router_depth() == 1U,
                    "tile.press.guard", "press alone opens nothing");
        (void)test_event(tile, LV_EVENT_LONG_PRESSED,
                         "tile.long_press", "Settings edit mode");
        /* Real pointers can report PRESS_LOST before the post-long-press CLICKED.
         * Neither event is allowed to cancel edit mode or launch the app. */
        (void)test_event(tile, LV_EVENT_PRESS_LOST,
                         "tile.long_press.lost", "retain edit selection");
        (void)test_event(tile, LV_EVENT_CLICKED,
                         "tile.long_press.click", "ignore post-long click");
        s_test_phase = FT_TEST_TILE_LONG_PRESS_VERIFY;
        break;
    }
    case FT_TEST_TILE_LONG_PRESS_VERIFY:
        test_record(ft_pages_test_tile_editing() &&
                    ft_pages_test_tile_selected() == 0U &&
                    ft_router_depth() == 1U,
                    "tile.edit", "selected Settings / app not opened");
        test_record(ft_pages_test_tile_handle_count() == 4U,
                    "tile.handles", "four inset 90-degree resize Chevrons");
        test_record(ft_pages_test_tile_handle_geometry(),
                    "tile.handles.geometry",
                    "body scales inside Tile; 51px corner targets stay fixed");
        s_test_phase = FT_TEST_TILE_MOVE;
        break;
    case FT_TEST_TILE_MOVE:
        s_action_count++;
        test_record(ft_pages_test_tile_move(0U, 2U),
                    "tile.move",
                    "long-press body drag; pending still; occupied pit reflows");
        test_record(ft_pages_test_tile_layout_valid(), "tile.reflow", "only confirmed conflicts move");
        test_record(ft_pages_test_tile_layout_settled(),
                    "tile.move.settled", "all Tiles final; selected animation continues");
        test_record(ft_pages_test_tile_move_nearest(1U),
                    "tile.move.nearest", "middle / edge / occupied pits selectable");
        test_record(ft_pages_test_tile_move_scrolled(3U),
                    "tile.move.scrolled", "foreground Tile remains under pointer after scroll");
        test_record(ft_pages_test_tile_move_edge_autoscroll(3U),
                    "tile.move.edge_scroll",
                    "bottom edge scrolls desktop and exposes a legal snap pit");
        s_test_phase = FT_TEST_TILE_RESIZE;
        break;
    case FT_TEST_TILE_RESIZE:
        s_action_count++;
        test_record(ft_pages_test_tile_resize(FT_UI_TEST_MEDIA_INDEX, 2U, 2U) &&
                    ft_pages_test_tile_columns(FT_UI_TEST_MEDIA_INDEX) == 2U &&
                    ft_pages_test_tile_rows(FT_UI_TEST_MEDIA_INDEX) == 2U,
                    "tile.resize", "live body follows; release animates to 2x2");
        test_record(ft_pages_test_tile_resize_collision(),
                    "tile.resize.collision",
                    "covered Tiles animate to nearest free pits; others stay");
        test_record(ft_pages_test_tile_layout_settled(),
                    "tile.resize.settled", "all Tiles final; selected animation continues");
        test_record(ft_pages_test_tile_layout_valid(), "tile.resize.reflow",
                    "all siblings remain in grid");
        test_record(ft_pages_test_tile_resize_boundary(),
                    "tile.resize.boundary", "clamped in current row / desktop bounds");
        test_record(ft_pages_test_tile_resize_edge_autoscroll(3U),
                    "tile.resize.edge_scroll",
                    "bottom resize scrolls through the logical desktop");
        test_record(ft_pages_test_tile_resize_anchors(FT_UI_TEST_MEDIA_INDEX),
                    "tile.resize.anchors", "TL/TR/BL/BR keep opposite edges fixed");
        s_test_phase = FT_TEST_TILE_PROPERTIES;
        break;
    case FT_TEST_TILE_PROPERTIES:
        s_action_count++;
        test_record(ft_pages_test_tile_set_common(0U, "Settings+", 192U,
                                                   FT_ICON_TILE_PATTERN) &&
                    strcmp(ft_pages_test_tile_name(0U), "Settings+") == 0 &&
                    ft_pages_test_tile_opacity(0U) == 192U &&
                    ft_pages_test_tile_pattern(0U) == FT_ICON_TILE_PATTERN,
                    "tile.properties", "name / opacity / pattern mutable");
        s_test_phase = FT_TEST_TILE_LIVE;
        break;
    case FT_TEST_TILE_LIVE:
    {
        char before[48];
        const char *text = ft_pages_test_tile_live_text(FT_UI_TEST_MEDIA_INDEX);
        before[0] = '\0';
        if (text != RT_NULL)
        {
            rt_strncpy(before, text, sizeof(before) - 1U);
            before[sizeof(before) - 1U] = '\0';
        }
        s_action_count++;
        test_record(ft_pages_test_tile_live_enabled(FT_UI_TEST_MEDIA_INDEX) &&
                    ft_pages_test_tile_live_advance(FT_UI_TEST_MEDIA_INDEX),
                    "tile.live", "application advances its own frame");
        text = ft_pages_test_tile_live_text(FT_UI_TEST_MEDIA_INDEX);
        test_record(text != RT_NULL && strcmp(before, text) != 0,
                    "tile.live.content", "Media frame changed");
        s_test_phase = FT_TEST_TILE_RESTORE;
        break;
    }
    case FT_TEST_TILE_RESTORE:
        s_action_count++;
        (void)ft_pages_test_tile_set_common(0U,
                                            ft_preferences_text("设置", "Settings"),
                                            255U, FT_ICON_COUNT);
        (void)ft_pages_test_tile_resize(FT_UI_TEST_MEDIA_INDEX, 1U, 1U);
        (void)ft_pages_test_tile_restore_layout();
        test_record(!ft_pages_test_tile_editing() &&
                    ft_pages_test_tile_order(0U) == 0U &&
                    ft_pages_test_tile_columns(FT_UI_TEST_MEDIA_INDEX) == 1U &&
                    ft_pages_test_tile_rows(FT_UI_TEST_MEDIA_INDEX) == 1U &&
                    ft_pages_test_tile_layout_valid(),
                    "tile.restore", "defaults restored / edit closed");
        s_app_index = 0U;
        s_test_phase = FT_TEST_START_OPEN;
        break;
    case FT_TEST_START_OPEN:
        if (app == RT_NULL)
        {
            s_app_index = 0U;
            s_test_phase = FT_TEST_LIFECYCLE_RELEASE_VERIFY;
            break;
        }
        (void)test_short_click(ft_pages_test_get_start_button(s_app_index),
                               "start.short_click", app->tile.name);
        test_record(current_app_is(app), "router.push", app->tile.name);
        s_control_index = 0U;
        s_test_phase = FT_TEST_PAGE_CONTROLS;
        break;
    case FT_TEST_PAGE_CONTROLS:
        if (app == RT_NULL) { test_record(false, "page.controls", "missing app"); finish_page_controls(); }
        else run_page_control_test(app);
        break;
    case FT_TEST_START_BACK:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_BACK), "nav.back", "from page");
        test_record(home_start_is_ready(), "router.pop",
                    app != RT_NULL ? app->tile.name : "unknown");
        s_app_index++;
        s_test_phase = FT_TEST_START_OPEN;
        break;
    case FT_TEST_LIFECYCLE_RELEASE_VERIFY:
        test_record(ft_pages_test_transient_slots_clear(), "lifecycle.release",
                    "all transient object slots cleared");
        s_test_phase = FT_TEST_LIFECYCLE_SEARCH_OPEN;
        break;
    case FT_TEST_LIFECYCLE_SEARCH_OPEN:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_SEARCH),
                         "lifecycle.search.open", "after application release");
        s_lifecycle_wait_steps = 0U;
        s_test_phase = FT_TEST_LIFECYCLE_SEARCH_WAIT;
        break;
    case FT_TEST_LIFECYCLE_SEARCH_WAIT:
        s_lifecycle_wait_steps++;
        if (s_lifecycle_wait_steps >= 5U)
        {
            test_record(ft_router_current_page() == FT_PAGE_SEARCH && ft_router_depth() == 2U,
                        "lifecycle.search.alive", "survived status timer refresh");
            s_test_phase = FT_TEST_LIFECYCLE_SEARCH_RESULT;
        }
        break;
    case FT_TEST_LIFECYCLE_SEARCH_RESULT:
        (void)test_click(ft_pages_test_get_search_result(FT_UI_TEST_MEDIA_INDEX),
                         "lifecycle.search.result", "Media after prior routes");
        s_test_phase = FT_TEST_LIFECYCLE_SEARCH_RESULT_VERIFY;
        break;
    case FT_TEST_LIFECYCLE_SEARCH_RESULT_VERIFY:
        test_record(ft_router_current_page() == FT_PAGE_MEDIA && ft_router_depth() == 3U,
                    "lifecycle.search.push", "Media depth 3");
        s_test_phase = FT_TEST_LIFECYCLE_SEARCH_HOME;
        break;
    case FT_TEST_LIFECYCLE_SEARCH_HOME:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_HOME),
                         "lifecycle.search.home", "release Search and Media");
        s_test_phase = FT_TEST_LIFECYCLE_SEARCH_HOME_VERIFY;
        break;
    case FT_TEST_LIFECYCLE_SEARCH_HOME_VERIFY:
        test_record(home_start_is_ready(), "lifecycle.home", "returned to Start");
        test_record(ft_pages_test_transient_slots_clear(), "lifecycle.release",
                    "Search and Media slots cleared");
        s_app_index = 0U;
        s_test_phase = FT_TEST_LIST_SHOW;
        break;
    case FT_TEST_LIST_SHOW:
        if (app == RT_NULL) { s_test_phase = FT_TEST_ROUTE_FILL; break; }
        s_action_count++;
        ft_pages_show_all_apps();
        test_record(true, "tileview.swipe", "all apps");
        s_test_phase = FT_TEST_LIST_SHOW_VERIFY;
        break;
    case FT_TEST_LIST_SHOW_VERIFY:
        test_record(ft_pages_test_apps_is_active(), "tileview.state", "all apps");
        s_test_phase = FT_TEST_LIST_OPEN;
        break;
    case FT_TEST_LIST_OPEN:
        (void)test_click(ft_pages_test_get_apps_button(s_app_index), "apps.click", app->tile.name);
        test_record(current_app_is(app), "router.push", app->tile.name);
        s_test_phase = FT_TEST_LIST_HOME;
        break;
    case FT_TEST_LIST_HOME:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_HOME), "nav.home", app->tile.name);
        s_test_phase = FT_TEST_LIST_HOME_VERIFY;
        break;
    case FT_TEST_LIST_HOME_VERIFY:
        test_record(home_start_is_ready(), "router.home", app->tile.name);
        s_app_index++;
        s_test_phase = FT_TEST_LIST_SHOW;
        break;
    case FT_TEST_ROUTE_FILL:
        if (ft_router_depth() < FT_UI_TEST_ROUTE_LIMIT)
        {
            int result = ft_router_push(FT_PAGE_ABOUT);
            s_action_count++;
            test_record(result == RT_EOK && ft_router_depth() <= FT_UI_TEST_ROUTE_LIMIT,
                        "router.boundary.push", "About");
        }
        else s_test_phase = FT_TEST_ROUTE_OVERFLOW;
        break;
    case FT_TEST_ROUTE_OVERFLOW:
    {
        int result = ft_router_push(FT_PAGE_ABOUT);
        s_action_count++;
        test_record(result == -RT_EFULL && ft_router_depth() == FT_UI_TEST_ROUTE_LIMIT,
                    "router.overflow", "rejected at depth 8");
        s_test_phase = FT_TEST_ROUTE_HOME;
        break;
    }
    case FT_TEST_ROUTE_HOME:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_HOME), "nav.home", "from depth 8");
        s_test_phase = FT_TEST_ROUTE_HOME_VERIFY;
        break;
    case FT_TEST_ROUTE_HOME_VERIFY:
        test_record(home_start_is_ready(), "router.release", "depth 8 -> 1");
        s_test_phase = FT_TEST_LEAK_CHECK;
        break;
    case FT_TEST_LEAK_CHECK:
    {
        ft_ui_metrics_t metrics;
        ft_metrics_route_check();
        ft_metrics_get(&metrics);
        test_record(metrics.last_route_object_delta == 0, "route.objects", "no object leak");
        test_record(metrics.refresh_count >= metrics.render_count &&
                    metrics.flush_count >= metrics.render_count &&
                    metrics.flushed_pixels > 0U,
                    "metrics.present", "render/flush/pixel counters active");
        rt_kprintf("[UI-TEST] route heap delta=%ld bytes (allocator high-water is reported, not asserted)\n",
                   (long)metrics.last_route_heap_delta);
        s_test_phase = FT_TEST_FINISH;
        break;
    }
    case FT_TEST_FINISH:
    {
        const ft_ui_preferences_t *preferences;
        uint32_t duration_ms;
        ft_preferences_reset();
        ft_ui_test_notification_reset();
        lv_slider_set_value(ft_ui_test_get_brightness_slider(), 100, LV_ANIM_OFF);
        (void)lv_obj_send_event(ft_ui_test_get_brightness_slider(),
                                LV_EVENT_VALUE_CHANGED, RT_NULL);
        preferences = ft_preferences_get();
        test_record(preferences->accent_rgb == 0x0078D7UL && preferences->tile_opa == 255U &&
                    preferences->background == FT_BACKGROUND_BLACK &&
                    preferences->audio_output_sample_rate == 16000U &&
                    preferences->audio_output_sample_bits == 16U &&
                    preferences->audio_output_channels == 2U,
                    "preferences.defaults", "reset path exercised under test suspension");
        ft_preferences_test_end();
        preferences = ft_preferences_get();
        test_record(preferences->accent_rgb == s_preferences_before.accent_rgb &&
                    preferences->tile_opa == s_preferences_before.tile_opa &&
                    preferences->background == s_preferences_before.background &&
                    preferences->use_24_hour == s_preferences_before.use_24_hour &&
                    preferences->timezone_offset_minutes ==
                        s_preferences_before.timezone_offset_minutes &&
                    preferences->language == s_preferences_before.language &&
                    preferences->audio_output_volume ==
                        s_preferences_before.audio_output_volume &&
                    preferences->audio_input_gain ==
                        s_preferences_before.audio_input_gain &&
                    preferences->audio_output_sample_rate ==
                        s_preferences_before.audio_output_sample_rate &&
                    preferences->audio_output_sample_bits ==
                        s_preferences_before.audio_output_sample_bits &&
                    preferences->audio_output_channels ==
                        s_preferences_before.audio_output_channels &&
                    strcmp(preferences->wallpaper_path,
                           s_preferences_before.wallpaper_path) == 0,
                    "preferences.restore", "per-device configuration restored");
        test_record(ft_ui_test_notification_count() == 0U &&
                    ft_ui_test_brightness() == 100U,
                    "shade.restore", "queue empty / brightness 100%");
        duration_ms = rt_tick_get_millisecond() - s_start_ms;
        s_test_phase = FT_TEST_COMPLETE;
        rt_kprintf("[UI-TEST] COMPLETE pass=%lu fail=%lu actions=%lu duration=%lums\n",
                   (unsigned long)s_pass_count, (unsigned long)s_fail_count,
                   (unsigned long)s_action_count, (unsigned long)duration_ms);
        s_test_timer = RT_NULL;
        lv_timer_delete(timer);
        break;
    }
    case FT_TEST_COMPLETE:
    default:
        break;
    }
}

void ft_ui_test_start(void)
{
    if (s_test_timer != RT_NULL) return;
    s_preferences_before = *ft_preferences_get();
    ft_preferences_test_begin();
    s_test_phase = FT_TEST_PENDING;
    s_app_index = 0U;
    s_control_index = 0U;
    s_pass_count = 0U;
    s_fail_count = 0U;
    s_action_count = 0U;
    s_lifecycle_wait_steps = 0U;
    s_gallery_wait_steps = 0U;
    s_step_period_active = false;
    s_start_ms = rt_tick_get_millisecond();
    rt_kprintf("[UI-TEST] ENABLED delay=%lums step=%lums shell/search/apps/preferences/business/route/performance\n",
               (unsigned long)FT_UI_TEST_START_DELAY_MS, (unsigned long)FT_UI_TEST_STEP_MS);
    s_test_timer = lv_timer_create(ui_test_timer_cb, FT_UI_TEST_START_DELAY_MS, RT_NULL);
}

void ft_ui_test_print_status(void)
{
    const char *state = "running";
    if (s_test_phase == FT_TEST_PENDING) state = "pending";
    else if (s_test_phase == FT_TEST_COMPLETE) state = s_fail_count == 0U ? "passed" : "failed";
    rt_kprintf("FeatherTalk UI auto-test: enabled=1 state=%s phase=%d pass=%lu fail=%lu actions=%lu\n",
               state, (int)s_test_phase, (unsigned long)s_pass_count,
               (unsigned long)s_fail_count, (unsigned long)s_action_count);
}

#endif
