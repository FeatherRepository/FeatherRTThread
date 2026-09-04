#include <rtthread.h>
#include <string.h>
#include "feathertalk_wifi.h"
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_wifi.h"
#include "feathertalk_ui_keyboard.h"

typedef struct {
    lv_obj_t *root, *body, *toggle, *state, *details, *scan, *disconnect, *list;
    lv_obj_t *form, *form_body, *ssid, *password, *keyboard, *form_error;
    lv_obj_t *rows[FT_WIFI_MAX_NETWORKS];
    lv_timer_t *timer;
    ft_wifi_status_t snapshot;
    uint32_t scan_revision;
} wifi_view_t;
static wifi_view_t *s_view;

static lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_width(obj, lv_pct(100));
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, ft_layout_font(16), 0);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, wifi_view_t *view)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(obj, ft_layout_get()->control_height, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, ft_layout_px(10), 0);
    ft_ui_register_accent(obj, FT_ACCENT_BACKGROUND);
    label(obj, text);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, view);
    return obj;
}
static void set_disabled(lv_obj_t *obj, bool disabled)
{
    if (disabled) lv_obj_add_state(obj, LV_STATE_DISABLED);
    else lv_obj_remove_state(obj, LV_STATE_DISABLED);
}
static void set_text(lv_obj_t *obj, const char *text)
{
    if (strcmp(lv_label_get_text(obj), text)) lv_label_set_text(obj, text);
}
static const char *state_text(ft_wifi_state_t state)
{
    switch (state) {
    case FT_WIFI_WAITING: return ft_preferences_text("正在初始化无线模组…", "Initializing radio...");
    case FT_WIFI_OFF: return ft_preferences_text("Wi-Fi 已关闭", "Wi-Fi off");
    case FT_WIFI_IDLE: return ft_preferences_text("未连接", "Not connected");
    case FT_WIFI_SCANNING: return ft_preferences_text("正在扫描附近网络…", "Scanning nearby networks...");
    case FT_WIFI_CONNECTING: return ft_preferences_text("正在连接…", "Connecting...");
    case FT_WIFI_ADDRESS: return ft_preferences_text("已关联，正在获取 IP…", "Associated; obtaining IP...");
    case FT_WIFI_CONNECTED: return ft_preferences_text("已连接", "Connected");
    default: return ft_preferences_text("操作失败，请重试", "Operation failed; retry");
    }
}
static void hide_form(wifi_view_t *view)
{
    lv_textarea_set_text(view->password, "");
    lv_obj_add_flag(view->form, LV_OBJ_FLAG_HIDDEN);
}
bool ft_wifi_page_back(void)
{
    if (s_view && !lv_obj_has_flag(s_view->form, LV_OBJ_FLAG_HIDDEN)) {
        hide_form(s_view);
        return true;
    }
    return false;
}
static void form_cancel(lv_event_t *event) { hide_form(lv_event_get_user_data(event)); }
static void form_reflow(wifi_view_t *view)
{
    if (!view->form_body || !view->keyboard) return;
    int32_t height = lv_obj_get_content_height(view->form);
    int32_t keyboard_height = ft_layout_get()->keyboard_height;
    lv_obj_set_height(view->form_body, LV_MAX(1, height -
        (lv_obj_has_flag(view->keyboard, LV_OBJ_FLAG_HIDDEN) ? 0 : keyboard_height)));
}
static void form_size_changed(lv_event_t *event) { form_reflow(lv_event_get_user_data(event)); }
static void hide_keyboard(lv_event_t *event)
{
    wifi_view_t *view = lv_event_get_user_data(event);
    lv_obj_add_flag(view->keyboard, LV_OBJ_FLAG_HIDDEN);
    form_reflow(view);
}
static void show_keyboard(lv_event_t *event)
{
    wifi_view_t *view = lv_event_get_user_data(event);
    lv_obj_remove_flag(view->keyboard, LV_OBJ_FLAG_HIDDEN);
    form_reflow(view);
}
static void connect_clicked(lv_event_t *event)
{
    wifi_view_t *view = lv_event_get_user_data(event);
    int result = feathertalk_wifi_connect(lv_label_get_text(view->ssid), lv_textarea_get_text(view->password));
    if (result == RT_EOK) hide_form(view);
    else lv_label_set_text(view->form_error, ft_preferences_text("服务忙或输入无效，请稍后重试", "Service busy or invalid input; retry"));
}
static void network_clicked(lv_event_t *event)
{
    wifi_view_t *view = lv_event_get_user_data(event);
    size_t index = (size_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(event));
    if (index >= view->snapshot.count || view->snapshot.busy) return;
    lv_label_set_text(view->ssid, view->snapshot.networks[index].ssid);
    lv_textarea_set_text(view->password, "");
    lv_label_set_text(view->form_error, view->snapshot.networks[index].security == 0 ?
        ft_preferences_text("开放网络，无需密码", "Open network; no password required") :
        ft_preferences_text("请输入网络密码", "Enter network password"));
    lv_obj_remove_flag(view->form, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(view->form);
    lv_obj_update_layout(view->form);
    form_reflow(view);
    lv_obj_scroll_to_y(view->form_body, 0, LV_ANIM_OFF);
}
static void toggle_changed(lv_event_t *event)
{
    wifi_view_t *view = lv_event_get_user_data(event);
    int rc = feathertalk_wifi_enable(lv_obj_has_state(view->toggle, LV_STATE_CHECKED));
    if (rc != RT_EOK) set_text(view->state, ft_preferences_text("服务忙，请稍后重试", "Service busy; retry"));
}
static void scan_clicked(lv_event_t *event)
{
    wifi_view_t *view = lv_event_get_user_data(event);
    if (feathertalk_wifi_scan() != RT_EOK)
        set_text(view->state, ft_preferences_text("服务忙，请稍后重试", "Service busy; retry"));
}
static void disconnect_clicked(lv_event_t *event)
{
    wifi_view_t *view = lv_event_get_user_data(event);
    if (feathertalk_wifi_disconnect() != RT_EOK)
        set_text(view->state, ft_preferences_text("服务忙，请稍后重试", "Service busy; retry"));
}
static void refresh(lv_timer_t *timer)
{
    wifi_view_t *view = lv_timer_get_user_data(timer);
    char text[256];
    if (!lv_obj_is_visible(view->root)) return;
    feathertalk_wifi_status(&view->snapshot);
    const ft_wifi_status_t *status = &view->snapshot;
    set_disabled(view->toggle, !status->available || status->busy);
    if (status->enabled) lv_obj_add_state(view->toggle, LV_STATE_CHECKED);
    else lv_obj_remove_state(view->toggle, LV_STATE_CHECKED);
    set_disabled(view->scan, !status->enabled || !status->available || status->busy);
    set_disabled(view->disconnect, !status->associated || status->busy);
    if (status->error) rt_snprintf(text, sizeof(text), "%s (%d)", state_text(status->state), status->error);
    else rt_snprintf(text, sizeof(text), "%s", state_text(status->state));
    set_text(view->state, text);
    if (status->associated)
        rt_snprintf(text, sizeof(text), "%s\nIP: %s\n%s: %s\nRSSI: %d dBm\nMAC: %s",
                    status->ssid, status->ip[0] ? status->ip : "—",
                    ft_preferences_text("网关", "Gateway"), status->gateway, status->rssi, status->mac);
    else rt_snprintf(text, sizeof(text), "CYW55513 / SDIO0\nMAC: %s", status->mac[0] ? status->mac : "—");
    set_text(view->details, text);
    /* Rebuild only when a completed scan changes the result set. Never replace
     * a touched row on a one-second status update. */
    if (view->scan_revision != status->scan_revision) {
        view->scan_revision = status->scan_revision;
        lv_obj_clean(view->list);
        memset(view->rows, 0, sizeof(view->rows));
        for (unsigned i = 0; i < status->count; i++) {
            const ft_wifi_network_t *net = &status->networks[i];
            rt_snprintf(text, sizeof(text), "%s\n%d dBm · %s %d · %s", net->ssid, net->rssi,
                        ft_preferences_text("信道", "CH"), net->channel,
                        net->security == 0 ? ft_preferences_text("开放", "Open") : ft_preferences_text("加密", "Secured"));
            view->rows[i] = button(view->list, text, network_clicked, view);
            lv_obj_set_user_data(view->rows[i], (void *)(uintptr_t)i);
        }
        if (!status->count) label(view->list, ft_preferences_text("未发现网络", "No networks found"));
    }
    for (unsigned i = 0; i < FT_WIFI_MAX_NETWORKS; i++)
        if (view->rows[i]) set_disabled(view->rows[i], !status->enabled || status->busy);
}
static void deleted(lv_event_t *event)
{
    wifi_view_t *view = lv_event_get_user_data(event);
    if (view->timer) lv_timer_delete(view->timer);
    if (s_view == view) s_view = RT_NULL;
    rt_free(view);
}
static void column(lv_obj_t *obj)
{
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(obj, ft_layout_get()->page_padding, 0);
    lv_obj_set_style_pad_row(obj, ft_layout_get()->section_gap, 0);
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
}
lv_obj_t *ft_wifi_page_create(lv_obj_t *parent)
{
    wifi_view_t *view = rt_calloc(1, sizeof(*view));
    if (!view) return RT_NULL;
    s_view = view;
    view->scan_revision = UINT32_MAX;
    view->root = lv_obj_create(parent);
    ft_ui_style_page(view->root);
    lv_obj_set_size(view->root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(view->root, 0, 0);
    lv_obj_remove_flag(view->root, LV_OBJ_FLAG_SCROLLABLE);
    view->body = lv_obj_create(view->root);
    ft_ui_style_page(view->body);
    lv_obj_set_size(view->body, lv_pct(100), lv_pct(100));
    column(view->body);
    lv_obj_t *title = label(view->body, "Wi-Fi");
    lv_obj_set_style_text_font(title, ft_layout_font(22), 0);
    ft_ui_register_accent(title, FT_ACCENT_TEXT);
    view->toggle = lv_switch_create(view->body);
    lv_obj_add_event_cb(view->toggle, toggle_changed, LV_EVENT_VALUE_CHANGED, view);
    view->state = label(view->body, "");
    view->details = label(view->body, "");
    view->scan = button(view->body, ft_preferences_text("扫描网络", "Scan networks"), scan_clicked, view);
    view->disconnect = button(view->body, ft_preferences_text("断开连接", "Disconnect"), disconnect_clicked, view);
    view->list = lv_obj_create(view->body);
    lv_obj_set_width(view->list, lv_pct(100));
    lv_obj_set_height(view->list, LV_SIZE_CONTENT);
    ft_ui_style_page(view->list);
    column(view->list);
    lv_obj_set_style_pad_all(view->list, 0, 0);

    /* A page-local overlay; keyboard occupies the bottom of the viewport,
     * never below the scrollable scan results or over system navigation. */
    view->form = lv_obj_create(view->root);
    ft_ui_style_panel(view->form);
    lv_obj_set_size(view->form, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(view->form, 0, 0);
    lv_obj_set_style_border_width(view->form, 0, 0);
    lv_obj_remove_flag(view->form, LV_OBJ_FLAG_SCROLLABLE);
    view->form_body = lv_obj_create(view->form);
    ft_ui_style_page(view->form_body);
    lv_obj_set_width(view->form_body, lv_pct(100));
    lv_obj_align(view->form_body, LV_ALIGN_TOP_MID, 0, 0);
    column(view->form_body);
    view->ssid = label(view->form_body, "");
    view->form_error = label(view->form_body, "");
    view->password = lv_textarea_create(view->form_body);
    lv_obj_set_width(view->password, lv_pct(100));
    lv_textarea_set_one_line(view->password, true);
    lv_textarea_set_password_mode(view->password, true);
    lv_textarea_set_max_length(view->password, FT_WIFI_KEY_BYTES - 1);
    lv_textarea_set_placeholder_text(view->password, ft_preferences_text("密码", "Password"));
    lv_obj_add_event_cb(view->password, show_keyboard, LV_EVENT_CLICKED, view);
    lv_obj_t *actions = lv_obj_create(view->form_body);
    ft_ui_style_panel(actions);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(button(actions, ft_preferences_text("连接", "Connect"), connect_clicked, view), lv_pct(31));
    lv_obj_set_width(button(actions, ft_preferences_text("取消", "Cancel"), form_cancel, view), lv_pct(31));
    lv_obj_set_width(button(actions, ft_preferences_text("收起键盘", "Hide keyboard"), hide_keyboard, view), lv_pct(31));
    view->keyboard = ft_ui_keyboard_create(view->form, view->password);
    lv_obj_align(view->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(view->keyboard, hide_keyboard, LV_EVENT_READY, view);
    lv_obj_add_event_cb(view->keyboard, hide_keyboard, LV_EVENT_CANCEL, view);
    lv_obj_add_flag(view->form, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(view->form, form_size_changed, LV_EVENT_SIZE_CHANGED, view);
    lv_obj_add_event_cb(view->root, deleted, LV_EVENT_DELETE, view);
    view->timer = lv_timer_create(refresh, 500, view);
    refresh(view->timer);
    return view->root;
}

/* Real scan results only. This test never joins or changes radio power. */
static void wifi_ui_test_async(void *argument)
{
    unsigned failures = 0, checked = 0;
    (void)argument;
    for (unsigned pass = 0; pass < 5; pass++) {
        ft_router_home();
        if (ft_router_push(FT_PAGE_SETTINGS) != RT_EOK ||
            ft_router_push(FT_PAGE_SETTINGS_WIFI) != RT_EOK || !s_view) {
            failures++; break;
        }
        refresh(s_view->timer);
        lv_obj_update_layout(s_view->root);
        if (lv_obj_get_width(s_view->body) > lv_obj_get_width(s_view->root)) failures++;
        checked++;
        if (s_view->snapshot.count && !s_view->snapshot.busy) {
            lv_obj_send_event(s_view->rows[0], LV_EVENT_CLICKED, NULL);
            if (lv_obj_has_flag(s_view->form, LV_OBJ_FLAG_HIDDEN)) failures++;
            if (!lv_textarea_get_password_mode(s_view->password)) failures++;
            lv_textarea_set_text(s_view->password, "not-sent");
            lv_obj_send_event(s_view->keyboard, LV_EVENT_CANCEL, NULL);
            if (!lv_obj_has_flag(s_view->keyboard, LV_OBJ_FLAG_HIDDEN)) failures++;
            lv_obj_update_layout(s_view->form);
            if (lv_obj_get_height(s_view->form_body) != lv_obj_get_content_height(s_view->form)) failures++;
            lv_obj_send_event(s_view->password, LV_EVENT_CLICKED, NULL);
            if (lv_obj_has_flag(s_view->keyboard, LV_OBJ_FLAG_HIDDEN)) failures++;
            lv_obj_update_layout(s_view->form);
            lv_area_t keyboard_area, root_area;
            lv_obj_get_coords(s_view->keyboard, &keyboard_area);
            lv_obj_get_coords(s_view->root, &root_area);
            if (keyboard_area.y2 > root_area.y2) failures++;
            if (lv_obj_get_height(s_view->keyboard) != ft_layout_get()->keyboard_height ||
                keyboard_area.y2 != root_area.y2 || keyboard_area.x1 < root_area.x1 ||
                keyboard_area.x2 > root_area.x2) failures++;
            lv_keyboard_set_mode(s_view->keyboard, LV_KEYBOARD_MODE_SPECIAL);
            lv_obj_update_layout(s_view->form);
            if (lv_obj_get_height(s_view->keyboard) != ft_layout_get()->keyboard_height) failures++;
            lv_keyboard_set_mode(s_view->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
            lv_obj_scroll_to_y(s_view->form_body, ft_layout_px(100), LV_ANIM_OFF);
            lv_obj_update_layout(s_view->form);
            lv_area_t after_scroll;
            lv_obj_get_coords(s_view->keyboard, &after_scroll);
            if (after_scroll.y1 != keyboard_area.y1 || after_scroll.y2 != keyboard_area.y2) failures++;
            if (!ft_wifi_page_back() || lv_textarea_get_text(s_view->password)[0]) failures++;
            checked += 10;
        }
    }
    rt_kprintf("[WIFI-UI-TEST] checks=%u failures=%u networks=%u (no connection attempted)\n",
               checked, failures, s_view ? s_view->snapshot.count : 0);
}
static void feather_wifi_ui_test(void)
{
    lv_lock();
    lv_result_t result = lv_async_call(wifi_ui_test_async, NULL);
    lv_unlock();
    rt_kprintf("[WIFI-UI-TEST] queued=%d\n", result == LV_RESULT_OK);
}
MSH_CMD_EXPORT(feather_wifi_ui_test, Test WiFi page lifecycle/password keyboard without joining);
