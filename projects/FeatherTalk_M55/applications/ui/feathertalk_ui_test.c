#include <rtthread.h>
#include <string.h>
#include "feathertalk_ui.h"
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_notifications.h"

#ifdef FEATHERTALK_UI_TEST_MODE

#define FT_UI_TEST_START_DELAY_MS 1800U
#define FT_UI_TEST_STEP_MS         320U
#define FT_UI_TEST_ROUTE_LIMIT     8U
#define FT_UI_TEST_MEDIA_INDEX     2U

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
static uint32_t s_message_before;
static uint32_t s_files_before;
static uint8_t s_lifecycle_wait_steps;

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
    char detail[24];
    if (s_control_index < accent_count)
    {
        uint32_t expected = ft_pages_test_accent_rgb(s_control_index);
        lv_snprintf(detail, sizeof(detail), "accent[%lu]", (unsigned long)s_control_index);
        (void)test_click(ft_pages_test_get_accent_button(s_control_index), "settings.click", detail);
        test_record(preferences->accent_rgb == expected, "settings.accent", detail);
    }
    else if (s_control_index < accent_count + opacity_count)
    {
        size_t index = s_control_index - accent_count;
        uint8_t expected = ft_pages_test_opacity_value(index);
        lv_snprintf(detail, sizeof(detail), "opacity[%lu]", (unsigned long)index);
        (void)test_click(ft_pages_test_get_opacity_button(index), "settings.click", detail);
        test_record(preferences->tile_opa == expected, "settings.opacity", detail);
    }
    else if (s_control_index < accent_count + opacity_count + background_count)
    {
        size_t index = s_control_index - accent_count - opacity_count;
        lv_snprintf(detail, sizeof(detail), "background[%lu]", (unsigned long)index);
        (void)test_click(ft_pages_test_get_background_button(index), "settings.click", detail);
        test_record(preferences->background == (ft_background_mode_t)index,
                    "settings.background", detail);
    }
    else
    {
        finish_page_controls();
        return;
    }
    s_control_index++;
}

static void run_media_test(void)
{
    const char *label;
    switch (s_control_index)
    {
    case 0U:
        (void)test_click(ft_pages_test_get_media_prev_button(), "media.prev", "track 0 -> 2");
        test_record(ft_pages_test_media_track() == 2, "media.track", "previous wraps");
        break;
    case 1U:
        (void)test_click(ft_pages_test_get_media_button(), "media.play", RT_NULL);
        label = ft_pages_test_get_media_label();
        test_record(ft_pages_test_media_is_playing() && label != RT_NULL && strstr(label, "Pause") != RT_NULL,
                    "media.state", "playing/Pause");
        break;
    case 2U:
        (void)test_click(ft_pages_test_get_media_button(), "media.pause", RT_NULL);
        label = ft_pages_test_get_media_label();
        test_record(!ft_pages_test_media_is_playing() && label != RT_NULL && strstr(label, "Play") != RT_NULL,
                    "media.state", "paused/Play");
        break;
    case 3U:
        (void)test_click(ft_pages_test_get_media_next_button(), "media.next", "track 2 -> 0");
        test_record(ft_pages_test_media_track() == 0, "media.track", "next wraps");
        break;
    case 4U:
        lv_slider_set_value(ft_pages_test_get_media_volume(), 35, LV_ANIM_OFF);
        (void)test_event(ft_pages_test_get_media_volume(), LV_EVENT_VALUE_CHANGED,
                         "media.volume", "35");
        test_record(ft_pages_test_media_volume() == 35, "media.volume.state", "35");
        break;
    default:
        finish_page_controls();
        return;
    }
    s_control_index++;
}

static void run_message_test(void)
{
    if (s_control_index == 0U)
    {
        s_message_before = ft_pages_test_message_count();
        (void)test_click(ft_pages_test_get_messages_button(), "messages.notify", "create alert");
        test_record(ft_pages_test_message_count() == s_message_before + 1U,
                    "messages.count", "incremented");
        test_record(ft_ui_test_get_alert_button() != RT_NULL, "messages.alert", "visible");
        s_control_index++;
        return;
    }
    if (s_control_index == 1U)
    {
        (void)test_alert_close("messages.alert.close");
        s_control_index++;
        return;
    }
    finish_page_controls();
}

static void run_files_test(void)
{
    if (s_control_index == 0U)
    {
        s_files_before = ft_pages_test_files_refresh_count();
        (void)test_click(ft_pages_test_get_files_refresh_button(), "files.refresh", RT_NULL);
        test_record(ft_pages_test_files_refresh_count() == s_files_before + 1U,
                    "files.refresh.count", "incremented");
        s_control_index++;
        return;
    }
    finish_page_controls();
}

static void run_page_control_test(const ft_app_descriptor_t *app)
{
    if (app->page_id == FT_PAGE_SETTINGS) run_settings_test();
    else if (app->page_id == FT_PAGE_MEDIA) run_media_test();
    else if (app->page_id == FT_PAGE_MESSAGES) run_message_test();
    else if (app->page_id == FT_PAGE_FILES) run_files_test();
    else
    {
        test_record(current_app_is(app), "page.content", app->name);
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
        char detail[64];
        lv_snprintf(detail, sizeof(detail), "%ldx%ld %u columns scale %ld%%",
                    (long)layout->screen_width, (long)layout->screen_height,
                    layout->tile_columns, (long)layout->scale_percent);
        test_record(home_start_is_ready(), "shell.start", "home/start");
        test_record(app_count == 6U, "registry.count", "6 applications");
        test_record(ft_layout_profiles_self_test(), "layout.profiles",
                    "240x320 through 720x1280 + landscape");
        test_record(layout->tile_column_width > 0 &&
                    layout->status_bar_height + layout->nav_bar_height < layout->screen_height,
                    "layout.current", detail);
        ft_ui_test_notification_reset();
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
        s_test_phase = FT_TEST_NOTIFY_DRAG_CLOSE;
        break;
    case FT_TEST_NOTIFY_DRAG_CLOSE:
    {
        int32_t open_y = ft_layout_get()->status_bar_height;
        int32_t closed_y = -ft_layout_get()->notification_height;
        int32_t pointer_y = open_y + ft_layout_px(80);
        s_action_count++;
        ft_ui_test_notification_drag_begin(pointer_y);
        ft_ui_test_notification_drag_move(pointer_y - ft_layout_px(100));
        test_record(ft_ui_test_notification_y() < open_y &&
                    ft_ui_test_notification_y() > closed_y,
                    "notification.follow", "upward intermediate Y");
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
        test_record(!ft_ui_test_quick_available(FEATHERTALK_QUICK_WIFI) &&
                    !ft_ui_test_quick_available(FEATHERTALK_QUICK_BLUETOOTH) &&
                    !ft_ui_test_quick_available(FEATHERTALK_QUICK_ROTATION),
                    "quick.capabilities", "M33 drivers unavailable");
        test_record(!ft_ui_test_quick_connected(FEATHERTALK_QUICK_WIFI) &&
                    !ft_ui_test_quick_connected(FEATHERTALK_QUICK_BLUETOOTH) &&
                    ft_ui_test_quick_signal() == FEATHERTALK_SYSTEM_VALUE_UNKNOWN,
                    "quick.connections", "no fabricated link or signal state");
        s_test_phase = FT_TEST_QUICK_UNAVAILABLE_CLICK;
        break;
    case FT_TEST_QUICK_UNAVAILABLE_CLICK:
        (void)test_click(ft_ui_test_get_quick_button(FEATHERTALK_QUICK_WIFI),
                         "quick.wifi", "disabled/unavailable");
        (void)test_click(ft_ui_test_get_quick_button(FEATHERTALK_QUICK_BLUETOOTH),
                         "quick.bluetooth", "disabled/unavailable");
        (void)test_click(ft_ui_test_get_quick_button(FEATHERTALK_QUICK_ROTATION),
                         "quick.rotation", "disabled/unavailable");
        test_record(!ft_ui_test_quick_enabled(FEATHERTALK_QUICK_WIFI) &&
                    !ft_ui_test_quick_enabled(FEATHERTALK_QUICK_BLUETOOTH) &&
                    !ft_ui_test_quick_enabled(FEATHERTALK_QUICK_ROTATION),
                    "quick.unavailable.state", "all unchanged");
        s_test_phase = FT_TEST_QUICK_BRIGHTNESS_CLICK;
        break;
    case FT_TEST_QUICK_BRIGHTNESS_CLICK:
        (void)test_click(ft_ui_test_get_quick_button(FEATHERTALK_QUICK_BRIGHTNESS),
                         "quick.brightness", "100 -> 30");
        test_record(ft_ui_test_brightness() == 30U, "quick.brightness.state", "30%");
        s_test_phase = FT_TEST_QUICK_BRIGHTNESS_SLIDER;
        break;
    case FT_TEST_QUICK_BRIGHTNESS_SLIDER:
        lv_slider_set_value(ft_ui_test_get_brightness_slider(), 65, LV_ANIM_OFF);
        (void)test_event(ft_ui_test_get_brightness_slider(), LV_EVENT_VALUE_CHANGED,
                         "quick.brightness.slider", "65");
        test_record(ft_ui_test_brightness() == 65U, "quick.brightness.state", "65%");
        s_test_phase = FT_TEST_QUEUE_ADD;
        break;
    case FT_TEST_QUEUE_ADD:
        s_action_count += 2U;
        feathertalk_ui_notify("System", "First notification", "Swipe test item");
        feathertalk_ui_notify("Messages", "Second notification", "Clear-all test item");
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
        s_action_count++;
        ft_pages_show_all_apps();
        test_record(true, "tileview.swipe", "all apps");
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
        (void)test_click(ft_pages_test_get_start_button(s_app_index), "start.click", app->name);
        test_record(current_app_is(app), "router.push", app->name);
        s_control_index = 0U;
        s_test_phase = FT_TEST_PAGE_CONTROLS;
        break;
    case FT_TEST_PAGE_CONTROLS:
        if (app == RT_NULL) { test_record(false, "page.controls", "missing app"); finish_page_controls(); }
        else run_page_control_test(app);
        break;
    case FT_TEST_START_BACK:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_BACK), "nav.back", "from page");
        test_record(home_start_is_ready(), "router.pop", app != RT_NULL ? app->name : "unknown");
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
                         "lifecycle.search.open", "after System release");
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
                         "lifecycle.search.result", "Media after System");
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
        (void)test_click(ft_pages_test_get_apps_button(s_app_index), "apps.click", app->name);
        test_record(current_app_is(app), "router.push", app->name);
        s_test_phase = FT_TEST_LIST_HOME;
        break;
    case FT_TEST_LIST_HOME:
        (void)test_click(ft_ui_test_get_nav_button(FT_NAV_HOME), "nav.home", app->name);
        s_test_phase = FT_TEST_LIST_HOME_VERIFY;
        break;
    case FT_TEST_LIST_HOME_VERIFY:
        test_record(home_start_is_ready(), "router.home", app->name);
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
                    preferences->background == FT_BACKGROUND_BLACK,
                    "preferences.restore", "defaults");
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
    s_test_phase = FT_TEST_PENDING;
    s_app_index = 0U;
    s_control_index = 0U;
    s_pass_count = 0U;
    s_fail_count = 0U;
    s_action_count = 0U;
    s_lifecycle_wait_steps = 0U;
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
