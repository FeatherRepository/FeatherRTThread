#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>
#include <feathertalk/ipc_protocol.h>
#ifdef RT_USING_FINSH
#include <finsh.h>
#endif
#include "ipc/feathertalk_ipc.h"
#include "feathertalk_ui.h"
#include "feathertalk_ui_gallery.h"
#include "feathertalk_ui_internal.h"
#include "feathertalk_ui_notifications.h"
#include "feathertalk_ui_platform.h"
#include "feathertalk_ui_preferences_store.h"
#include "lv_image_cache.h"
#include "lv_os.h"

#define FT_ACCENT_SLOTS      96U
#define FT_BACKGROUND_SLOTS  16U
#define FT_DEFAULT_ACCENT    0x0078D7UL
#define FT_NOTIFICATION_MASK_OPA 110U
#define FT_NOTIFICATION_MASK_STEPS 12U

typedef struct
{
    lv_obj_t *button;
    lv_obj_t *icon;
    lv_obj_t *name_label;
    lv_obj_t *state_label;
    bool available;
    bool enabled;
    bool connected;
    uint8_t value;
    uint8_t signal_percent;
    bool rendered;
    bool rendered_available;
    bool rendered_enabled;
    bool rendered_connected;
    uint8_t rendered_value;
    uint8_t rendered_signal_percent;
} ft_quick_view_t;

typedef struct
{
    lv_obj_t *card;
    uint32_t notification_id;
    int32_t press_x;
    int32_t press_y;
    int32_t last_x;
    int32_t velocity_x;
    uint32_t last_ms;
    bool tracking;
    bool horizontal;
    bool vertical;
} ft_notification_swipe_t;

typedef struct
{
    lv_obj_t *obj;
    ft_accent_target_t target;
} ft_accent_slot_t;

static bool s_ui_initialized;
static rt_thread_t s_ui_thread;
static lv_obj_t *s_status_bar;
static lv_obj_t *s_content_viewport;
static lv_obj_t *s_nav_bar;
static lv_obj_t *s_status_uptime;
static lv_obj_t *s_status_metrics;
static lv_obj_t *s_status_wifi;
static lv_obj_t *s_status_bluetooth;
static ft_icon_id_t s_status_wifi_icon_id = FT_ICON_WIFI_OFF;
static lv_obj_t *s_nav_buttons[FT_NAV_COUNT];
static lv_color_t s_accent;
static lv_color_t s_page_background;
static ft_accent_slot_t s_accent_slots[FT_ACCENT_SLOTS];
static lv_obj_t *s_background_slots[FT_BACKGROUND_SLOTS];
static lv_obj_t *s_wallpaper_image;
static bool s_wallpaper_active;
static bool s_media_frozen;
static char s_wallpaper_native_path[256];
static ft_gallery_rendered_image_t s_wallpaper_cache;
static size_t s_accent_overflow_count;
static lv_obj_t *s_notification_mask;
static lv_obj_t *s_notification_panel;
static lv_obj_t *s_notification_badge;
static lv_obj_t *s_notification_summary;
static lv_obj_t *s_notification_clear;
static lv_obj_t *s_notification_panel_title;
static lv_obj_t *s_notification_clear_label;
static lv_obj_t *s_notification_brightness_name;
static lv_obj_t *s_notification_list_title;
static lv_obj_t *s_notification_list;
static lv_obj_t *s_brightness_slider;
static lv_obj_t *s_brightness_value;
static ft_quick_view_t s_quick_views[FEATHERTALK_QUICK_COUNT];
static bool s_notification_visible;
static bool s_notification_dragging;
static bool s_notification_animating;
static bool s_notification_drag_moved;
static bool s_notification_suppress_click;
static uint8_t s_notification_mask_level;
static int32_t s_notification_press_y;
static int32_t s_notification_start_y;
static int32_t s_notification_last_pointer_y;
static int32_t s_notification_velocity_y;
static uint32_t s_notification_press_ms;
static uint32_t s_notification_last_sample_ms;
static uint32_t s_notification_render_revision;
static uint32_t s_notification_drag_samples;
static uint32_t s_notification_drag_applied;
static uint32_t s_notification_drag_skipped;
static uint32_t s_notification_mask_applied;
static uint32_t s_notification_mask_skipped;
static uint32_t s_notification_render_count;
static uint32_t s_notification_render_skipped;
static ft_notification_swipe_t s_notification_swipe;
static lv_obj_t *s_alert;
static lv_obj_t *s_alert_button;
#ifdef FEATHERTALK_UI_TEST_MODE
static volatile bool s_notification_preview_requested;
#endif

static void notification_settle(bool visible);
static void notification_render(void);
static void quick_views_refresh(void);

static bool label_set_text_changed(lv_obj_t *label, const char *text)
{
    const char *current;
    if (label == RT_NULL || !lv_obj_is_valid(label) ||
        !lv_obj_check_type(label, &lv_label_class)) return false;
    if (text == RT_NULL) text = "";
    current = lv_label_get_text(label);
    if (current != RT_NULL && strcmp(current, text) == 0) return false;
    lv_label_set_text(label, text);
    return true;
}

static void apply_accent(ft_accent_slot_t *slot)
{
    if ((slot->obj == RT_NULL) || !lv_obj_is_valid(slot->obj))
    {
        slot->obj = RT_NULL;
        return;
    }

    if (slot->target == FT_ACCENT_BACKGROUND)
    {
        lv_obj_set_style_bg_color(slot->obj, s_accent, LV_PART_MAIN);
    }
    else if (slot->target == FT_ACCENT_BORDER)
    {
        lv_obj_set_style_border_color(slot->obj, s_accent, LV_PART_MAIN);
    }
    else if (slot->target == FT_ACCENT_IMAGE)
    {
        lv_obj_set_style_image_recolor(slot->obj, s_accent, LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(slot->obj, LV_OPA_COVER, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_text_color(slot->obj, s_accent, LV_PART_MAIN);
    }
}

static void accent_object_deleted(lv_event_t *event)
{
    ft_accent_slot_t *slot = (ft_accent_slot_t *)lv_event_get_user_data(event);

    if (slot != RT_NULL)
    {
        slot->obj = RT_NULL;
    }
}

static void background_object_deleted(lv_event_t *event)
{
    lv_obj_t **slot = (lv_obj_t **)lv_event_get_user_data(event);
    if (slot != RT_NULL) *slot = RT_NULL;
}

static void apply_page_background(lv_obj_t *obj)
{
    if (obj == RT_NULL || !lv_obj_is_valid(obj)) return;
    lv_obj_set_style_bg_color(obj, s_page_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj,
                            s_wallpaper_active ? LV_OPA_TRANSP : LV_OPA_COVER,
                            LV_PART_MAIN);
}

lv_color_t ft_ui_accent_color(void)
{
    return s_accent;
}

void ft_ui_set_accent(uint32_t rgb)
{
    size_t i;

    s_accent = lv_color_hex(rgb);
    for (i = 0U; i < FT_ACCENT_SLOTS; i++)
    {
        apply_accent(&s_accent_slots[i]);
    }
}

void ft_ui_register_accent(lv_obj_t *obj, ft_accent_target_t target)
{
    size_t i;

    if (obj == RT_NULL)
    {
        return;
    }

    for (i = 0U; i < FT_ACCENT_SLOTS; i++)
    {
        if (s_accent_slots[i].obj == RT_NULL)
        {
            s_accent_slots[i].obj = obj;
            s_accent_slots[i].target = target;
            lv_obj_add_event_cb(obj, accent_object_deleted, LV_EVENT_DELETE, &s_accent_slots[i]);
            apply_accent(&s_accent_slots[i]);
            return;
        }
    }

    s_accent_overflow_count++;
    rt_kprintf("[FeatherTalk UI] accent registry full\n");
}

void ft_ui_set_page_background(uint32_t rgb)
{
    size_t i;
    s_page_background = lv_color_hex(rgb & 0xFFFFFFUL);
    for (i = 0U; i < FT_BACKGROUND_SLOTS; i++)
    {
        if (s_background_slots[i] != RT_NULL)
        {
            if (lv_obj_is_valid(s_background_slots[i]))
                apply_page_background(s_background_slots[i]);
            else
                s_background_slots[i] = RT_NULL;
        }
    }
}

void ft_ui_register_page_background(lv_obj_t *obj)
{
    size_t i;
    if (obj == RT_NULL) return;
    for (i = 0U; i < FT_BACKGROUND_SLOTS; i++)
    {
        if (s_background_slots[i] == RT_NULL)
        {
            s_background_slots[i] = obj;
            lv_obj_add_event_cb(obj, background_object_deleted, LV_EVENT_DELETE,
                                &s_background_slots[i]);
            apply_page_background(obj);
            return;
        }
    }
    rt_kprintf("[FeatherTalk UI] page background registry full\n");
}

static void wallpaper_detach_and_release(void)
{
    s_wallpaper_active = false;
    if (s_wallpaper_image != RT_NULL && lv_obj_is_valid(s_wallpaper_image))
    {
        if (lv_image_get_src(s_wallpaper_image) != RT_NULL)
            lv_image_set_src(s_wallpaper_image, RT_NULL);
        lv_obj_add_flag(s_wallpaper_image, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wallpaper_cache.draw_buf != RT_NULL)
    {
        /* A previously queued VG-Lite operation may still reference the
         * variable image descriptor after lv_image_set_src(NULL). */
        lv_draw_wait_for_finish();
        ft_gallery_release_rendered_image(&s_wallpaper_cache);
    }
}

void ft_ui_set_page_wallpaper(const char *path)
{
    char lv_path[sizeof(s_wallpaper_native_path) + 3U];
    lv_image_header_t header;
    ft_gallery_rendered_image_t rendered;
    const ft_ui_layout_t *layout = ft_layout_get();
    ft_gallery_source_t source;
    bool verified = false;
    bool image_available;
    size_t i;

    rt_memset(&rendered, 0, sizeof(rendered));
    image_available = s_wallpaper_image != RT_NULL &&
                      lv_obj_is_valid(s_wallpaper_image);

    if (!s_media_frozen && path != RT_NULL && path[0] != '\0')
    {
        if (image_available)
            verified = ft_gallery_render_image_path(
                path, (uint32_t)layout->screen_width,
                (uint32_t)layout->screen_height, &rendered);
        else
            verified = ft_gallery_validate_image_path(path, &source, lv_path,
                                                      sizeof(lv_path), &header);
    }
    if (verified)
    {
        rt_strncpy(s_wallpaper_native_path, path,
                   sizeof(s_wallpaper_native_path) - 1U);
        s_wallpaper_native_path[sizeof(s_wallpaper_native_path) - 1U] = '\0';
    }
    else if (!s_media_frozen)
    {
        s_wallpaper_native_path[0] = '\0';
    }

    wallpaper_detach_and_release();
    if (verified && image_available)
    {
        s_wallpaper_cache = rendered;
        rt_memset(&rendered, 0, sizeof(rendered));
        lv_image_set_src(s_wallpaper_image, s_wallpaper_cache.draw_buf);
        lv_obj_remove_flag(s_wallpaper_image, LV_OBJ_FLAG_HIDDEN);
        s_wallpaper_active = true;
    }
    ft_gallery_release_rendered_image(&rendered);
    for (i = 0U; i < FT_BACKGROUND_SLOTS; i++)
    {
        if (s_background_slots[i] != RT_NULL)
        {
            if (lv_obj_is_valid(s_background_slots[i]))
                apply_page_background(s_background_slots[i]);
            else
                s_background_slots[i] = RT_NULL;
        }
    }
    /* Wallpaper mode changes the routed pages between opaque and transparent.
     * Repaint the whole hard-clipped content viewport once so stale pixels at
     * the status/navigation seams cannot survive a partial invalidation. */
    if (image_available)
    {
        lv_obj_t *content = lv_obj_get_parent(s_wallpaper_image);
        if (content != RT_NULL && lv_obj_is_valid(content))
            lv_obj_invalidate(content);
    }
    if (s_status_bar != RT_NULL && lv_obj_is_valid(s_status_bar))
        lv_obj_invalidate(s_status_bar);
    for (i = 0U; i < FT_NAV_COUNT; i++)
        if (s_nav_buttons[i] != RT_NULL && lv_obj_is_valid(s_nav_buttons[i]))
            lv_obj_invalidate(s_nav_buttons[i]);
}

bool ft_ui_page_wallpaper_active(void)
{
    return s_wallpaper_active;
}

typedef enum
{
    FT_UI_MEDIA_ACTION_FREEZE = 0,
    FT_UI_MEDIA_ACTION_THAW
} ft_ui_media_action_t;

typedef struct
{
    struct rt_completion completion;
    ft_ui_media_action_t action;
    int result;
} ft_ui_media_request_t;

static void ui_media_apply_backgrounds(void)
{
    size_t index;

    for (index = 0U; index < FT_BACKGROUND_SLOTS; index++)
    {
        if (s_background_slots[index] == RT_NULL) continue;
        if (lv_obj_is_valid(s_background_slots[index]))
            apply_page_background(s_background_slots[index]);
        else
            s_background_slots[index] = RT_NULL;
    }
}

/* This function is called only by the LVGL thread.  Returning the Gallery to
 * its collection view invokes gallery_clear_image(), while page_leave stops
 * its filesystem monitor.  Force one source-free refresh before dropping the
 * cache: the VG-Lite backend retains image decoder descriptors until its
 * command queue is finished, and file decoders can own an open filesystem
 * handle until that point. */
static int ui_media_freeze_on_lvgl(void)
{
    if (s_media_frozen) return RT_EOK;
    s_media_frozen = true;

    (void)ft_gallery_page_back();
    ft_gallery_page_leave();
    wallpaper_detach_and_release();
    ui_media_apply_backgrounds();

    /* Invalidate even when the detached objects were already hidden, so the
     * refresh path necessarily dispatches and finishes the VG-Lite queue.
     * lv_refr_now() runs here on the LVGL thread while both media are still
     * mounted; only after it returns is it safe to evict decoder cache data. */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(RT_NULL);
    lv_image_cache_drop(RT_NULL);
    return RT_EOK;
}

/* This function is called only by the LVGL thread, after both filesystems have
 * been remounted.  The configured wallpaper is decoded again if its medium is
 * available, and Gallery refreshes its collection/source state. */
static int ui_media_thaw_on_lvgl(void)
{
    const ft_ui_preferences_t *preferences;

    if (!s_media_frozen) return RT_EOK;
    s_media_frozen = false;
    preferences = ft_preferences_get();
    if (preferences != RT_NULL &&
        preferences->background == FT_BACKGROUND_CUSTOM &&
        preferences->wallpaper_path[0] != '\0')
        ft_ui_set_page_wallpaper(preferences->wallpaper_path);
    else
        ft_ui_set_page_wallpaper(RT_NULL);
    ft_gallery_page_enter();
    return RT_EOK;
}

static int ui_media_action_on_lvgl(ft_ui_media_action_t action)
{
    return action == FT_UI_MEDIA_ACTION_FREEZE ?
           ui_media_freeze_on_lvgl() : ui_media_thaw_on_lvgl();
}

static void ui_media_async_cb(void *user_data)
{
    ft_ui_media_request_t *request = (ft_ui_media_request_t *)user_data;

    if (request == RT_NULL) return;
    request->result = ui_media_action_on_lvgl(request->action);
    rt_completion_done(&request->completion);
}

static int ui_media_run_sync(ft_ui_media_action_t action)
{
    ft_ui_media_request_t request;
    lv_result_t schedule_result;
    rt_err_t wait_result;

    /* There cannot be an LVGL-backed media owner before shell construction.
     * Remember an early freeze so initialization also avoids opening paths. */
    if (!s_ui_initialized)
    {
        s_media_frozen = action == FT_UI_MEDIA_ACTION_FREEZE;
        return RT_EOK;
    }
    if (rt_thread_self() == s_ui_thread)
        return ui_media_action_on_lvgl(action);

    rt_memset(&request, 0, sizeof(request));
    request.action = action;
    request.result = -RT_ERROR;
    rt_completion_init(&request.completion);

    /* lv_async_call itself manipulates LVGL timers, so only scheduling is
     * protected by LVGL's OS mutex.  No object is touched by this caller. */
    lv_lock();
    schedule_result = lv_async_call(ui_media_async_cb, &request);
    lv_unlock();
    if (schedule_result != LV_RESULT_OK) return -RT_ENOMEM;
    wait_result = rt_completion_wait(&request.completion, RT_WAITING_FOREVER);
    return wait_result == RT_EOK ? request.result : wait_result;
}

int feathertalk_ui_media_freeze(void)
{
    return ui_media_run_sync(FT_UI_MEDIA_ACTION_FREEZE);
}

int feathertalk_ui_media_thaw(void)
{
    return ui_media_run_sync(FT_UI_MEDIA_ACTION_THAW);
}

size_t ft_ui_accent_object_count(void)
{
    size_t i;
    size_t count = 0U;

    for (i = 0U; i < FT_ACCENT_SLOTS; i++)
    {
        if (s_accent_slots[i].obj != RT_NULL)
        {
            count++;
        }
    }
    return count;
}

void ft_ui_style_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x181818), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, ft_layout_font(14), LV_PART_MAIN);
}

void ft_ui_style_page(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, s_page_background, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_bar(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *bar = lv_obj_create(parent);

    ft_ui_style_page(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, height);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x101010), LV_PART_MAIN);
    lv_obj_set_style_pad_left(bar, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_right(bar, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_column(bar, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return bar;
}

static const char *network_name(uint8_t state)
{
    switch (state)
    {
    case FEATHERTALK_NETWORK_CONNECTED:
        return ft_preferences_text("已连接", "connected");
    case FEATHERTALK_NETWORK_CONNECTING:
        return ft_preferences_text("正在连接", "connecting");
    case FEATHERTALK_NETWORK_DISCONNECTED:
        return ft_preferences_text("未连接", "disconnected");
    default:
        return ft_preferences_text("不可用", "unavailable");
    }
}

static ft_icon_id_t wifi_signal_icon(bool connected, uint8_t signal_percent)
{
    if (!connected) return FT_ICON_WIFI_OFF;
    if (signal_percent == FEATHERTALK_SYSTEM_VALUE_UNKNOWN) return FT_ICON_WIFI;
    if (signal_percent <= 33U) return FT_ICON_WIFI_WEAK;
    if (signal_percent <= 66U) return FT_ICON_WIFI_MEDIUM;
    return FT_ICON_WIFI;
}

static void status_icon_state(lv_obj_t *icon, bool available, bool enabled,
                              bool connected)
{
    lv_color_t color;
    lv_opa_t opacity;
    if (icon == RT_NULL || !lv_obj_is_valid(icon)) return;
    if (!available)
    {
        color = lv_color_hex(0x666666);
        opacity = LV_OPA_50;
    }
    else if (!enabled)
    {
        color = lv_color_hex(0x888888);
        opacity = LV_OPA_70;
    }
    else if (!connected)
    {
        color = lv_color_white();
        opacity = LV_OPA_70;
    }
    else
    {
        color = lv_color_white();
        opacity = LV_OPA_COVER;
    }
    lv_obj_set_style_image_recolor(icon, color, LV_PART_MAIN);
    lv_obj_set_style_opa(icon, opacity, LV_PART_MAIN);
}

static void status_radio_refresh(const feathertalk_quick_status_t *status,
                                 bool valid)
{
    static bool rendered;
    static bool rendered_wifi_available;
    static bool rendered_wifi_enabled;
    static bool rendered_wifi_connected;
    static bool rendered_bluetooth_available;
    static bool rendered_bluetooth_enabled;
    static bool rendered_bluetooth_connected;
    static uint8_t rendered_signal = FEATHERTALK_SYSTEM_VALUE_UNKNOWN;
    bool wifi_available = valid &&
        (status->capabilities & FEATHERTALK_QUICK_CAP_WIFI) != 0U;
    bool wifi_enabled = wifi_available &&
        (status->enabled & FEATHERTALK_QUICK_CAP_WIFI) != 0U;
    bool wifi_connected = wifi_enabled &&
        (status->connected & FEATHERTALK_QUICK_CAP_WIFI) != 0U;
    bool bluetooth_available = valid &&
        (status->capabilities & FEATHERTALK_QUICK_CAP_BLUETOOTH) != 0U;
    bool bluetooth_enabled = bluetooth_available &&
        (status->enabled & FEATHERTALK_QUICK_CAP_BLUETOOTH) != 0U;
    bool bluetooth_connected = bluetooth_enabled &&
        (status->connected & FEATHERTALK_QUICK_CAP_BLUETOOTH) != 0U;
    uint8_t signal = valid ? status->wifi_signal_percent : FEATHERTALK_SYSTEM_VALUE_UNKNOWN;

    if (rendered && rendered_wifi_available == wifi_available &&
        rendered_wifi_enabled == wifi_enabled &&
        rendered_wifi_connected == wifi_connected &&
        rendered_bluetooth_available == bluetooth_available &&
        rendered_bluetooth_enabled == bluetooth_enabled &&
        rendered_bluetooth_connected == bluetooth_connected &&
        rendered_signal == signal) return;

    s_status_wifi_icon_id = wifi_signal_icon(wifi_connected, signal);
    ft_icon_set(s_status_wifi, s_status_wifi_icon_id, ft_layout_icon_size(24U));
    status_icon_state(s_status_wifi, wifi_available, wifi_enabled, wifi_connected);
    status_icon_state(s_status_bluetooth, bluetooth_available,
                      bluetooth_enabled, bluetooth_connected);
    rendered = true;
    rendered_wifi_available = wifi_available;
    rendered_wifi_enabled = wifi_enabled;
    rendered_wifi_connected = wifi_connected;
    rendered_bluetooth_available = bluetooth_available;
    rendered_bluetooth_enabled = bluetooth_enabled;
    rendered_bluetooth_connected = bluetooth_connected;
    rendered_signal = signal;
}

static void quick_view_apply(feathertalk_quick_control_t control)
{
    ft_quick_view_t *view = &s_quick_views[control];
    char state[28];
    if (view->button == RT_NULL || !lv_obj_is_valid(view->button)) return;
    if (view->rendered && view->rendered_available == view->available &&
        view->rendered_enabled == view->enabled &&
        view->rendered_connected == view->connected &&
        view->rendered_value == view->value &&
        view->rendered_signal_percent == view->signal_percent) return;

    if (control == FEATHERTALK_QUICK_BRIGHTNESS)
    {
        if (s_brightness_slider != RT_NULL && lv_obj_is_valid(s_brightness_slider))
        {
            if (view->available)
                lv_obj_remove_state(s_brightness_slider, LV_STATE_DISABLED);
            else
                lv_obj_add_state(s_brightness_slider, LV_STATE_DISABLED);
            if (lv_slider_get_value(s_brightness_slider) != view->value)
                lv_slider_set_value(s_brightness_slider, view->value, LV_ANIM_OFF);
        }
        if (s_brightness_value != RT_NULL && lv_obj_is_valid(s_brightness_value))
        {
            char value[12];
            lv_snprintf(value, sizeof(value), "%u%%", view->value);
            (void)label_set_text_changed(s_brightness_value,
                                         view->available ? value :
                                         ft_preferences_text("不可用", "Unavailable"));
        }
    }

    if (!view->available)
    {
        lv_obj_add_state(view->button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(view->button, lv_color_hex(0x242424), LV_PART_MAIN);
        lv_obj_set_style_image_recolor(view->icon, lv_color_hex(0x777777), LV_PART_MAIN);
        lv_label_set_text(view->state_label,
                          ft_preferences_text("不可用", "Unavailable"));
    }
    else
    {
        lv_obj_remove_state(view->button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(view->button,
                                  view->enabled ? s_accent : lv_color_hex(0x343434),
                                  LV_PART_MAIN);
        lv_obj_set_style_image_recolor(view->icon, lv_color_white(), LV_PART_MAIN);
        if (control == FEATHERTALK_QUICK_BRIGHTNESS)
        {
            lv_snprintf(state, sizeof(state), "%u%%", view->value);
            lv_label_set_text(view->state_label, state);
        }
        else if (control == FEATHERTALK_QUICK_ROTATION)
        {
            lv_snprintf(state, sizeof(state), "%u deg", view->value);
            lv_label_set_text(view->state_label, state);
        }
        else if (control == FEATHERTALK_QUICK_WIFI)
        {
            if (!view->enabled)
                lv_label_set_text(view->state_label,
                                  ft_preferences_text("已关闭", "Off"));
            else if (!view->connected)
                lv_label_set_text(view->state_label,
                                  ft_preferences_text("未连接", "Not connected"));
            else if (view->signal_percent == FEATHERTALK_SYSTEM_VALUE_UNKNOWN)
                lv_label_set_text(view->state_label,
                                  ft_preferences_text("已连接", "Connected"));
            else
            {
                lv_snprintf(state, sizeof(state),
                            ft_preferences_text("已连接 %u%%", "Connected %u%%"),
                            view->signal_percent);
                lv_label_set_text(view->state_label, state);
            }
        }
        else if (control == FEATHERTALK_QUICK_BLUETOOTH)
        {
            if (!view->enabled)
                lv_label_set_text(view->state_label,
                                  ft_preferences_text("已关闭", "Off"));
            else
                lv_label_set_text(view->state_label,
                                  view->connected ?
                                  ft_preferences_text("已连接", "Connected") :
                                  ft_preferences_text("未连接", "Not connected"));
        }
        else
            lv_label_set_text(view->state_label,
                              view->enabled ? ft_preferences_text("已开启", "On") :
                                              ft_preferences_text("已关闭", "Off"));
    }
    view->rendered = true;
    view->rendered_available = view->available;
    view->rendered_enabled = view->enabled;
    view->rendered_connected = view->connected;
    view->rendered_value = view->value;
    view->rendered_signal_percent = view->signal_percent;
}

static void quick_views_refresh(void)
{
    feathertalk_quick_status_t status;
    bool remote_valid = feathertalk_ipc_get_quick_status(&status) == RT_EOK;
    size_t i;
    for (i = 0U; i < FEATHERTALK_QUICK_COUNT; i++)
    {
        ft_quick_view_t *view = &s_quick_views[i];
        uint8_t bit = (uint8_t)(1U << i);
        if (i == FEATHERTALK_QUICK_BRIGHTNESS)
        {
            view->available = ft_platform_brightness_available();
            view->value = ft_platform_get_brightness();
            view->enabled = view->value > 0U;
            view->connected = false;
            view->signal_percent = FEATHERTALK_SYSTEM_VALUE_UNKNOWN;
        }
        else
        {
            view->available = remote_valid && (status.capabilities & bit) != 0U;
            view->enabled = view->available && (status.enabled & bit) != 0U;
            view->connected = view->enabled && (status.connected & bit) != 0U;
            view->signal_percent = i == FEATHERTALK_QUICK_WIFI && view->connected ?
                                   status.wifi_signal_percent :
                                   FEATHERTALK_SYSTEM_VALUE_UNKNOWN;
            view->value = i == FEATHERTALK_QUICK_ROTATION && view->available ? status.rotation : 0U;
        }
        quick_view_apply((feathertalk_quick_control_t)i);
    }
    status_radio_refresh(remote_valid ? &status : RT_NULL, remote_valid);
}

static void quick_button_cb(lv_event_t *event)
{
    feathertalk_quick_control_t control =
        (feathertalk_quick_control_t)(uintptr_t)lv_event_get_user_data(event);
    ft_quick_view_t *view;
    uint8_t target;
    if (control >= FEATHERTALK_QUICK_COUNT) return;
    view = &s_quick_views[control];
    if (!view->available) return;
    if (control == FEATHERTALK_QUICK_BRIGHTNESS)
    {
        target = view->value > 45U ? 30U : 100U;
        if (ft_platform_set_brightness(target) == RT_EOK) quick_views_refresh();
    }
    else
    {
        target = view->enabled ? 0U : 1U;
        if (feathertalk_ipc_set_quick_control((uint8_t)control, target) == RT_EOK)
            lv_label_set_text(view->state_label,
                              ft_preferences_text("处理中…", "Working..."));
    }
}

static void brightness_changed_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    uint8_t value;
    if (!s_quick_views[FEATHERTALK_QUICK_BRIGHTNESS].available) return;
    value = (uint8_t)lv_slider_get_value(slider);
    if (ft_platform_set_brightness(value) == RT_EOK) quick_views_refresh();
}

static void status_metrics_refresh(const ft_ui_metrics_t *metrics)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    char text[64];
    if (metrics == RT_NULL || s_status_metrics == RT_NULL) return;
    if (layout->compact)
        lv_snprintf(text, sizeof(text), "F%lu/%lu M%luK",
                    (unsigned long)metrics->fps,
                    (unsigned long)metrics->refresh_fps,
                    (unsigned long)(metrics->heap_used / 1024U));
    else
        lv_snprintf(text, sizeof(text), "FPS %lu/%lu  MEM %lu/%luK",
                    (unsigned long)metrics->fps,
                    (unsigned long)metrics->refresh_fps,
                    (unsigned long)(metrics->heap_used / 1024U),
                    (unsigned long)(metrics->heap_total / 1024U));
    (void)label_set_text_changed(s_status_metrics, text);
}

static void status_timer_cb(lv_timer_t *timer)
{
    feathertalk_system_status_t status;
    ft_ui_metrics_t metrics;
    uint32_t seconds;
    uint32_t minutes;
    bool time_valid;
    char bar[56];
    char clock[20];
    char timezone[16];
    char system_text[256];
    char metrics_text[768];
    char tile[64];
    const char *battery = "--";
    const char *network = ft_preferences_text("不可用", "unavailable");
    char battery_value[8];
    char network_value[40];

    ft_metrics_get(&metrics);
    status_metrics_refresh(&metrics);
    ft_preferences_refresh_wallpaper();
    if (timer != RT_NULL && (s_notification_dragging || s_notification_animating)) return;
    if (feathertalk_ipc_get_system_status(&status) == RT_EOK)
    {
        const ft_ui_preferences_t *preferences = ft_preferences_get();
        int16_t timezone_minutes = preferences->timezone_offset_minutes;
        char sign = timezone_minutes < 0 ? '-' : '+';
        uint16_t timezone_abs = (uint16_t)(timezone_minutes < 0 ?
                                           -timezone_minutes : timezone_minutes);
        time_valid = (status.flags & FEATHERTALK_SYSTEM_TIME_VALID) != 0U;
        seconds = time_valid ?
                  status.unix_time : status.m33_uptime_ms / 1000U;
        minutes = seconds / 60U;
        ft_preferences_format_clock(seconds, time_valid, clock, sizeof(clock));
        lv_snprintf(timezone, sizeof(timezone), "UTC%c%02u:%02u", sign,
                    (unsigned)(timezone_abs / 60U),
                    (unsigned)(timezone_abs % 60U));
        if ((status.flags & FEATHERTALK_SYSTEM_BATTERY_VALID) != 0U)
        {
            lv_snprintf(battery_value, sizeof(battery_value), "%u%%", status.battery_percent);
            battery = battery_value;
        }
        if ((status.flags & FEATHERTALK_SYSTEM_NETWORK_PRESENT) != 0U)
        {
            network = network_name(status.network_state);
            if (status.network_state == FEATHERTALK_NETWORK_CONNECTED &&
                status.signal_percent != FEATHERTALK_SYSTEM_VALUE_UNKNOWN)
            {
                lv_snprintf(network_value, sizeof(network_value),
                            ft_preferences_text("已连接，信号 %u%%",
                                                "connected, signal %u%%"),
                            status.signal_percent);
                network = network_value;
            }
        }
        if (time_valid)
            lv_snprintf(bar, sizeof(bar), "%s  %s", clock, battery);
        else
            lv_snprintf(bar, sizeof(bar),
                        ft_preferences_text("运行 %02lu:%02lu  %s",
                                            "UP %02lu:%02lu  %s"),
                        (unsigned long)(minutes / 60U),
                        (unsigned long)(minutes % 60U), battery);
        lv_snprintf(system_text, sizeof(system_text), ft_preferences_text(
                    "M33 IPC：在线，序号 %lu，延迟 %lu ms\n"
                    "RTC：%s，本地时间 %s，%s，%s\n电源/电池：%s\nWi-Fi：%s",
                    "M33 IPC: online, seq %lu, age %lums\n"
                    "RTC: %s, local time %s, %s, %s\nPower/battery: %s\nWi-Fi: %s"),
                    (unsigned long)status.sequence,
                    (unsigned long)(rt_tick_get_millisecond() - status.received_ms),
                    ((status.flags & FEATHERTALK_SYSTEM_RTC_PRESENT) != 0U) ?
                        ft_preferences_text("存在", "present") :
                        ft_preferences_text("不可用", "unavailable"),
                    clock, timezone,
                    preferences->use_24_hour ?
                        ft_preferences_text("24 小时制", "24-hour") :
                        ft_preferences_text("12 小时制", "12-hour"),
                    battery, network);
        lv_snprintf(tile, sizeof(tile), "M33 %s  %s", network, clock);
    }
    else
    {
        seconds = rt_tick_get_millisecond() / 1000U;
        minutes = seconds / 60U;
        lv_snprintf(bar, sizeof(bar),
                    ft_preferences_text("运行 %02lu:%02lu  --",
                                        "UP %02lu:%02lu  --"),
                    (unsigned long)(minutes / 60U), (unsigned long)(minutes % 60U));
        lv_snprintf(system_text, sizeof(system_text), ft_preferences_text(
                     "M33 IPC：等待中\nRTC：不可用\n电源/电池：不可用\nWi-Fi：不可用",
                     "M33 IPC: waiting\nRTC: unavailable\nPower/battery: unavailable\nWi-Fi: unavailable"));
        lv_snprintf(tile, sizeof(tile),
                    ft_preferences_text("M33 等待中  运行 %02lu:%02lu",
                                        "M33 waiting  UP %02lu:%02lu"),
                    (unsigned long)(minutes / 60U), (unsigned long)(minutes % 60U));
    }
    lv_snprintf(metrics_text, sizeof(metrics_text), ft_preferences_text(
                "M55 UI：当前 %lu FPS，调度 %lu Hz\n"
                "帧 %lu，刷新 %lu，%lu 像素/秒\n渲染 %lu ms，峰值 %lu ms\n"
                "堆：%lu/%lu 字节，峰值 %lu\nUI 对象峰值：%lu\n"
                "上次路由变化：对象 %ld，堆 %ld\n"
                "绘制任务：GPU %lu/s，SW %lu/s，GPU 占比 %lu%%\n"
                "软件文字 %lu/s，GPU/SW 路由切换 %lu/s\n"
                "GPU 提交 %lu/s（%lu 字节/秒），flush %lu/s，finish %lu/s\n"
                "GPU 等待 %lu ms/秒，单次峰值 %lu ms\n"
                "GPU 硬件忙碌 %lu%%，平均任务 %lu us，峰值 %lu us",
                "M55 UI: present %lu FPS, scheduler %lu Hz\n"
                "Frames %lu, flush %lu, %lu pixels/s\nRender %lu ms, peak %lu ms\n"
                "Heap: %lu/%lu bytes, peak %lu\nUI objects peak: %lu\n"
                "Last route delta: objects %ld, heap %ld\n"
                "Draw tasks: GPU %lu/s, SW %lu/s, GPU share %lu%%\n"
                "SW labels %lu/s, GPU/SW route switches %lu/s\n"
                "GPU submit %lu/s (%lu bytes/s), flush %lu/s, finish %lu/s\n"
                "GPU wait %lu ms/s, single peak %lu ms\n"
                "GPU hardware busy %lu%%, average job %lu us, peak %lu us"),
                (unsigned long)metrics.fps, (unsigned long)metrics.refresh_fps,
                (unsigned long)metrics.render_count, (unsigned long)metrics.flush_count,
                (unsigned long)metrics.flushed_pixels_per_second,
                (unsigned long)metrics.render_time_last_ms,
                (unsigned long)metrics.render_time_max_ms,
                (unsigned long)metrics.heap_used, (unsigned long)metrics.heap_total,
                (unsigned long)metrics.heap_max_used, (unsigned long)metrics.peak_ui_objects,
                (long)metrics.last_route_object_delta, (long)metrics.last_route_heap_delta,
                (unsigned long)metrics.gpu_tasks_per_second,
                (unsigned long)metrics.sw_tasks_per_second,
                (unsigned long)metrics.gpu_task_percent,
                (unsigned long)metrics.sw_label_tasks_per_second,
                (unsigned long)metrics.route_unit_switches_per_second,
                (unsigned long)metrics.gpu_submits_per_second,
                (unsigned long)metrics.gpu_submit_bytes_per_second,
                (unsigned long)metrics.gpu_flushes_per_second,
                (unsigned long)metrics.gpu_finishes_per_second,
                (unsigned long)metrics.gpu_finish_wait_ms_per_second,
                (unsigned long)metrics.gpu_finish_wait_max_ms,
                (unsigned long)metrics.gpu_busy_percent,
                (unsigned long)metrics.gpu_job_average_us,
                (unsigned long)metrics.gpu_job_max_us);
    (void)label_set_text_changed(s_status_uptime, bar);
    ft_pages_update_system_status(system_text, metrics_text);
    ft_pages_live_tile_update(tile);
    quick_views_refresh();
#ifdef FEATHERTALK_UI_TEST_MODE
    if (s_notification_preview_requested)
    {
        s_notification_preview_requested = false;
        ft_notifications_clear();
        (void)ft_notifications_push("FeatherTalk", "系统通知",
                                    "Wi-Fi 与蓝牙服务状态测试。");
        (void)ft_notifications_push("消息", "第二条通知",
                                    "左右滑动删除，或点击清除。");
        notification_render();
        notification_settle(true);
    }
#endif
}

void ft_ui_apply_language(void)
{
    static const char *quick_names_zh[FEATHERTALK_QUICK_COUNT] =
        {"Wi-Fi", "蓝牙", "亮度", "自动旋转"};
    static const char *quick_names_en[FEATHERTALK_QUICK_COUNT] =
        {"Wi-Fi", "Bluetooth", "Brightness", "Auto-rotate"};
    size_t i;

    for (i = 0U; i < FEATHERTALK_QUICK_COUNT; i++)
    {
        ft_quick_view_t *view = &s_quick_views[i];
        if (view->name_label != RT_NULL && lv_obj_is_valid(view->name_label))
            lv_label_set_text(view->name_label,
                              ft_preferences_text(quick_names_zh[i], quick_names_en[i]));
        view->rendered = false;
    }
    if (s_notification_panel_title != RT_NULL &&
        lv_obj_is_valid(s_notification_panel_title))
        lv_label_set_text(s_notification_panel_title,
                          ft_preferences_text("快捷设置", "Quick settings"));
    if (s_notification_clear_label != RT_NULL &&
        lv_obj_is_valid(s_notification_clear_label))
        lv_label_set_text(s_notification_clear_label,
                          ft_preferences_text("清除", "Clear"));
    if (s_notification_brightness_name != RT_NULL &&
        lv_obj_is_valid(s_notification_brightness_name))
        lv_label_set_text(s_notification_brightness_name,
                          ft_preferences_text("亮度", "Brightness"));
    if (s_notification_list_title != RT_NULL &&
        lv_obj_is_valid(s_notification_list_title))
        lv_label_set_text(s_notification_list_title,
                          ft_preferences_text("通知（左右滑动可删除）",
                                              "Notifications (swipe sideways to delete)"));
    s_notification_render_revision = UINT32_MAX;
    if (s_notification_list != RT_NULL && lv_obj_is_valid(s_notification_list))
        notification_render();
    if (s_quick_views[0].button != RT_NULL)
        quick_views_refresh();
}

void ft_ui_preferences_changed(void)
{
    ft_ui_apply_language();
    if (s_status_uptime != RT_NULL && lv_obj_is_valid(s_status_uptime))
        status_timer_cb(RT_NULL);
}

static int32_t notification_closed_y(void)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    return -layout->notification_height;
}

static int32_t notification_open_y(void)
{
    return ft_layout_get()->status_bar_height;
}

static int32_t notification_clamp_y(int32_t y)
{
    int32_t closed_y = notification_closed_y();
    int32_t open_y = notification_open_y();
    if (y < closed_y) return closed_y;
    if (y > open_y) return open_y;
    return y;
}

static int32_t notification_current_y(void)
{
    return lv_obj_get_style_y(s_notification_panel, LV_PART_MAIN);
}

static void notification_mask_update(int32_t panel_y)
{
    int32_t span;
    int32_t progress;
    uint8_t level;
    uint8_t opacity;
    if (s_notification_mask == RT_NULL || !lv_obj_is_valid(s_notification_mask)) return;
    span = notification_open_y() - notification_closed_y();
    if (span <= 0) return;
    progress = panel_y - notification_closed_y();
    if (progress < 0) progress = 0;
    if (progress > span) progress = span;
    level = progress == 0 ? 0U :
            (uint8_t)(((uint32_t)progress * FT_NOTIFICATION_MASK_STEPS +
                       (uint32_t)span - 1U) / (uint32_t)span);
    if (level > FT_NOTIFICATION_MASK_STEPS) level = FT_NOTIFICATION_MASK_STEPS;
    if (level == s_notification_mask_level)
    {
        s_notification_mask_skipped++;
        return;
    }
    s_notification_mask_level = level;
    s_notification_mask_applied++;
    if (level == 0U)
    {
        lv_obj_add_flag(s_notification_mask, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    opacity = (uint8_t)(((uint32_t)level * FT_NOTIFICATION_MASK_OPA) /
                        FT_NOTIFICATION_MASK_STEPS);
    if (lv_obj_has_flag(s_notification_mask, LV_OBJ_FLAG_HIDDEN))
        lv_obj_remove_flag(s_notification_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(s_notification_mask, opacity, LV_PART_MAIN);
}

static void notification_anim_y_cb(void *object, int32_t value)
{
    lv_obj_t *panel = (lv_obj_t *)object;
    if (notification_current_y() != value) lv_obj_set_y(panel, value);
    notification_mask_update(value);
}

static void notification_anim_completed_cb(lv_anim_t *animation)
{
    LV_UNUSED(animation);
    s_notification_animating = false;
    if (!s_notification_visible && s_notification_mask != RT_NULL)
    {
        lv_obj_add_flag(s_notification_mask, LV_OBJ_FLAG_HIDDEN);
        s_notification_mask_level = 0U;
    }
}

static void notification_anim(bool visible)
{
    lv_anim_t animation;
    int32_t current = notification_current_y();
    int32_t target = visible ? notification_open_y() : notification_closed_y();
    lv_anim_delete(s_notification_panel, notification_anim_y_cb);
    s_notification_animating = false;
    if (current == target)
    {
        notification_mask_update(target);
        notification_anim_completed_cb(RT_NULL);
        return;
    }
    if (visible) notification_mask_update(current + 1);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_notification_panel);
    lv_anim_set_exec_cb(&animation, notification_anim_y_cb);
    lv_anim_set_values(&animation, current, target);
    lv_anim_set_duration(&animation, 180U);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, notification_anim_completed_cb);
    s_notification_animating = true;
    lv_anim_start(&animation);
}

static void notification_settle(bool visible)
{
    s_notification_visible = visible;
    if (visible)
    {
        ft_notifications_mark_all_read();
        notification_render();
    }
    notification_anim(visible);
}

static void notification_drag_begin_at(int32_t pointer_y, uint32_t now)
{
    if (s_notification_panel == RT_NULL || !lv_obj_is_valid(s_notification_panel)) return;
    lv_anim_delete(s_notification_panel, notification_anim_y_cb);
    s_notification_animating = false;
    s_notification_dragging = true;
    s_notification_drag_moved = false;
    s_notification_press_y = pointer_y;
    s_notification_start_y = notification_current_y();
    s_notification_last_pointer_y = pointer_y;
    s_notification_velocity_y = 0;
    s_notification_press_ms = now;
    s_notification_last_sample_ms = now;
}

static void notification_drag_begin(int32_t pointer_y)
{
    notification_drag_begin_at(pointer_y, rt_tick_get_millisecond());
}

static void notification_drag_update_at(int32_t pointer_y, uint32_t now)
{
    int32_t total_motion;
    int32_t motion;
    int32_t threshold;
    uint32_t elapsed;
    int32_t y;
    if (!s_notification_dragging) return;
    s_notification_drag_samples++;
    total_motion = pointer_y - s_notification_press_y;
    threshold = ft_layout_px(6);
    if (threshold < 4) threshold = 4;
    motion = pointer_y - s_notification_last_pointer_y;
    elapsed = now - s_notification_last_sample_ms;
    if (elapsed > 0U && elapsed <= 160U)
    {
        int32_t instant_velocity = (motion * 1000) / (int32_t)elapsed;
        s_notification_velocity_y = (s_notification_velocity_y * 2 + instant_velocity) / 3;
    }
    else if (elapsed > 160U)
        s_notification_velocity_y = 0;
    s_notification_last_pointer_y = pointer_y;
    s_notification_last_sample_ms = now;
    if (total_motion >= threshold || total_motion <= -threshold)
        s_notification_drag_moved = true;
    y = notification_clamp_y(s_notification_start_y + total_motion);
    if (y == notification_current_y())
    {
        s_notification_drag_skipped++;
        return;
    }
    lv_obj_set_y(s_notification_panel, y);
    s_notification_drag_applied++;
    notification_mask_update(y);
}

static void notification_drag_update(int32_t pointer_y)
{
    notification_drag_update_at(pointer_y, rt_tick_get_millisecond());
}

static bool notification_drag_finish_at(uint32_t now)
{
    int32_t y;
    int32_t midpoint;
    int32_t velocity_threshold;
    int32_t distance_threshold;
    int32_t total_motion;
    bool moved;
    bool visible;
    if (!s_notification_dragging) return false;
    s_notification_dragging = false;
    moved = s_notification_drag_moved;
    if (!moved)
    {
        notification_anim(s_notification_visible);
        return false;
    }

    y = notification_current_y();
    midpoint = notification_closed_y() +
               (notification_open_y() - notification_closed_y()) / 2;
    if ((now - s_notification_last_sample_ms) > 120U) s_notification_velocity_y = 0;
    velocity_threshold = ft_layout_px(600);
    if (velocity_threshold < 400) velocity_threshold = 400;
    distance_threshold = (notification_open_y() - notification_closed_y()) / 5;
    total_motion = y - s_notification_start_y;
    if (s_notification_velocity_y >= velocity_threshold)
        visible = true;
    else if (s_notification_velocity_y <= -velocity_threshold)
        visible = false;
    else if (total_motion >= distance_threshold)
        visible = true;
    else if (total_motion <= -distance_threshold)
        visible = false;
    else
        visible = y >= midpoint;
    notification_settle(visible);
    return true;
}

static bool notification_drag_finish(void)
{
    return notification_drag_finish_at(rt_tick_get_millisecond());
}

void ft_ui_notification_toggle(void)
{
    notification_settle(!s_notification_visible);
}

bool ft_ui_notification_visible(void)
{
    return s_notification_visible;
}

static void notification_gesture_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point;

    if (code == LV_EVENT_CLICKED)
    {
        if (s_notification_suppress_click)
            s_notification_suppress_click = false;
        else
            ft_ui_notification_toggle();
        return;
    }

    indev = lv_indev_active();
    if (indev == RT_NULL) return;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED)
        notification_drag_begin(point.y);
    else if (code == LV_EVENT_PRESSING)
        notification_drag_update(point.y);
    else if (code == LV_EVENT_RELEASED)
        s_notification_suppress_click = notification_drag_finish();
    else if (code == LV_EVENT_PRESS_LOST)
    {
        (void)notification_drag_finish();
        s_notification_suppress_click = false;
    }
}

static void notification_badge_update(void)
{
    char text[12];
    size_t unread = ft_notifications_unread_count();
    if (s_notification_badge == RT_NULL || !lv_obj_is_valid(s_notification_badge)) return;
    if (unread == 0U)
        lv_obj_add_flag(s_notification_badge, LV_OBJ_FLAG_HIDDEN);
    else
    {
        lv_snprintf(text, sizeof(text), "%lu", (unsigned long)unread);
        lv_label_set_text(s_notification_badge, text);
        lv_obj_remove_flag(s_notification_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

static void notification_remove_async(void *user_data)
{
    uint32_t id = (uint32_t)(uintptr_t)user_data;
    if (ft_notifications_remove(id)) notification_render();
}

static void notification_card_translate_anim_cb(void *object, int32_t value)
{
    lv_obj_t *card = (lv_obj_t *)object;
    if (card != RT_NULL && lv_obj_is_valid(card))
        lv_obj_set_style_translate_x(card, value, LV_PART_MAIN);
}

static void notification_card_restore(lv_obj_t *card)
{
    lv_anim_t animation;
    int32_t offset;
    if (card == RT_NULL || !lv_obj_is_valid(card)) return;
    offset = lv_obj_get_style_translate_x(card, LV_PART_MAIN);
    lv_anim_delete(card, notification_card_translate_anim_cb);
    if (offset == 0) return;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, card);
    lv_anim_set_exec_cb(&animation, notification_card_translate_anim_cb);
    lv_anim_set_values(&animation, offset, 0);
    lv_anim_set_duration(&animation, 160U);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    (void)lv_anim_start(&animation);
}

static void notification_card_swipe_finish(lv_obj_t *card, uint32_t id)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    int32_t offset;
    int32_t width;
    bool remove;
    if (!s_notification_swipe.tracking || s_notification_swipe.card != card) return;
    offset = card != RT_NULL && lv_obj_is_valid(card) ?
             lv_obj_get_style_translate_x(card, LV_PART_MAIN) : 0;
    width = card != RT_NULL && lv_obj_is_valid(card) ?
            lv_obj_get_width(card) : layout->screen_width;
    if ((rt_tick_get_millisecond() - s_notification_swipe.last_ms) > 120U)
        s_notification_swipe.velocity_x = 0;
    remove = s_notification_swipe.horizontal &&
             (LV_ABS(offset) >= width / 4 ||
              LV_ABS(s_notification_swipe.velocity_x) >= ft_layout_px(650));
    s_notification_swipe.tracking = false;
    s_notification_swipe.card = RT_NULL;
    if (remove)
    {
        notification_card_translate_anim_cb(card,
            offset < 0 ? -layout->screen_width : layout->screen_width);
        (void)lv_async_call(notification_remove_async, (void *)(uintptr_t)id);
    }
    else
        notification_card_restore(card);
}

static void notification_card_gesture_cb(lv_event_t *event)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *card = (lv_obj_t *)lv_event_get_current_target(event);
    uint32_t id = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point;
    uint32_t now;
    int32_t dx;
    int32_t dy;
    int32_t threshold = ft_layout_px(8);

    if (code == LV_EVENT_DELETE)
    {
        if (s_notification_swipe.card == card)
        {
            s_notification_swipe.tracking = false;
            s_notification_swipe.card = RT_NULL;
        }
        return;
    }
    if (indev == RT_NULL) return;
    if (code == LV_EVENT_GESTURE)
    {
        if (!s_notification_swipe.horizontal &&
            lv_indev_get_gesture_dir(indev) == LV_DIR_TOP)
            notification_settle(false);
        return;
    }
    lv_indev_get_point(indev, &point);
    now = rt_tick_get_millisecond();
    if (code == LV_EVENT_PRESSED)
    {
        lv_anim_delete(card, notification_card_translate_anim_cb);
        memset(&s_notification_swipe, 0, sizeof(s_notification_swipe));
        s_notification_swipe.card = card;
        s_notification_swipe.notification_id = id;
        s_notification_swipe.press_x = point.x;
        s_notification_swipe.press_y = point.y;
        s_notification_swipe.last_x = point.x;
        s_notification_swipe.last_ms = now;
        s_notification_swipe.tracking = true;
        return;
    }
    if (!s_notification_swipe.tracking || s_notification_swipe.card != card) return;
    if (code == LV_EVENT_PRESSING)
    {
        dx = point.x - s_notification_swipe.press_x;
        dy = point.y - s_notification_swipe.press_y;
        if (!s_notification_swipe.horizontal && !s_notification_swipe.vertical &&
            (LV_ABS(dx) >= threshold || LV_ABS(dy) >= threshold))
        {
            s_notification_swipe.horizontal = LV_ABS(dx) > LV_ABS(dy);
            s_notification_swipe.vertical = !s_notification_swipe.horizontal;
        }
        if (!s_notification_swipe.horizontal) return;
        if (now > s_notification_swipe.last_ms)
            s_notification_swipe.velocity_x =
                (point.x - s_notification_swipe.last_x) * 1000 /
                (int32_t)(now - s_notification_swipe.last_ms);
        s_notification_swipe.last_x = point.x;
        s_notification_swipe.last_ms = now;
        if (dx > layout->screen_width) dx = layout->screen_width;
        if (dx < -layout->screen_width) dx = -layout->screen_width;
        lv_obj_set_style_translate_x(card, dx, LV_PART_MAIN);
        lv_event_stop_bubbling(event);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        notification_card_swipe_finish(card, id);
}

static void notification_child_gesture_cb(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_active();
    LV_UNUSED(event);
    if (indev != RT_NULL && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP)
        notification_settle(false);
}

static void notification_mask_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    notification_settle(false);
}

static void notification_clear_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    ft_notifications_clear();
    notification_render();
}

static void notification_render(void)
{
    char summary[40];
    uint32_t revision = ft_notifications_revision();
    size_t count = ft_notifications_count();
    size_t unread = ft_notifications_unread_count();
    size_t i;
    if (s_notification_list != RT_NULL && lv_obj_is_valid(s_notification_list) &&
        s_notification_render_revision == revision)
    {
        s_notification_render_skipped++;
        return;
    }
    s_notification_render_revision = revision;
    s_notification_render_count++;
    notification_badge_update();
    if (s_notification_summary != RT_NULL && lv_obj_is_valid(s_notification_summary))
    {
        lv_snprintf(summary, sizeof(summary),
                    ft_preferences_text("%lu 条未读 / 共 %lu 条",
                                        "%lu unread / %lu total"),
                    (unsigned long)unread, (unsigned long)count);
        lv_label_set_text(s_notification_summary, summary);
    }
    if (s_notification_clear != RT_NULL && lv_obj_is_valid(s_notification_clear))
    {
        if (count == 0U) lv_obj_add_state(s_notification_clear, LV_STATE_DISABLED);
        else lv_obj_remove_state(s_notification_clear, LV_STATE_DISABLED);
    }
    if (s_notification_list == RT_NULL || !lv_obj_is_valid(s_notification_list)) return;
    lv_obj_clean(s_notification_list);
    if (count == 0U)
    {
        lv_obj_t *empty = lv_label_create(s_notification_list);
        lv_label_set_text(empty, ft_preferences_text("暂无通知", "No notifications"));
        lv_obj_set_style_text_font(empty, ft_layout_font(14), LV_PART_MAIN);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
        lv_obj_set_style_pad_top(empty, ft_layout_px(16), LV_PART_MAIN);
        return;
    }
    for (i = 0U; i < count; i++)
    {
        ft_notification_t item;
        lv_obj_t *card;
        lv_obj_t *meta;
        lv_obj_t *title;
        lv_obj_t *body;
        char meta_text[48];
        if (!ft_notifications_get(i, &item)) continue;
        card = lv_obj_create(s_notification_list);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_style_bg_color(card, lv_color_hex(item.unread ? 0x292929 : 0x202020), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, item.unread ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
        ft_ui_register_accent(card, FT_ACCENT_BORDER);
        lv_obj_set_style_radius(card, ft_layout_px(4), LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, ft_layout_px(10), LV_PART_MAIN);
        lv_obj_set_style_pad_row(card, ft_layout_px(3), LV_PART_MAIN);
        lv_obj_set_style_text_font(card, ft_layout_font(14), LV_PART_MAIN);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_event_cb(card, notification_card_gesture_cb, LV_EVENT_ALL,
                            (void *)(uintptr_t)item.id);
        lv_snprintf(meta_text, sizeof(meta_text),
                    ft_preferences_text("%s  %lu 秒", "%s  %lus"),
                    item.source[0] != '\0' ? item.source : "FeatherTalk",
                    (unsigned long)(item.created_ms / 1000U));
        meta = lv_label_create(card);
        lv_label_set_text(meta, meta_text);
        lv_obj_set_style_text_font(meta, ft_layout_font(12), LV_PART_MAIN);
        lv_obj_set_style_text_color(meta, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
        title = lv_label_create(card);
        lv_label_set_text(title, item.title);
        lv_obj_set_style_text_font(title, ft_layout_font(16), LV_PART_MAIN);
        lv_obj_set_width(title, lv_pct(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        body = lv_label_create(card);
        lv_label_set_text(body, item.body);
        lv_obj_set_style_text_font(body, ft_layout_font(14), LV_PART_MAIN);
        lv_obj_set_width(body, lv_pct(100));
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(body, lv_color_hex(0xD0D0D0), LV_PART_MAIN);
    }
}

void feathertalk_ui_notify(const char *source, const char *title, const char *message)
{
    (void)ft_notifications_push(source, title, message);
    notification_render();
}

static void alert_deleted_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_alert = RT_NULL;
    s_alert_button = RT_NULL;
}

void feathertalk_ui_alert(const char *title, const char *message)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    int32_t available_width;
    int32_t preferred_width;
    if (s_alert != RT_NULL && lv_obj_is_valid(s_alert)) lv_msgbox_close(s_alert);
    s_alert = lv_msgbox_create(RT_NULL);
    available_width = layout->screen_width - 2 * layout->page_padding;
    preferred_width = ft_layout_px(420);
    lv_obj_set_width(s_alert, preferred_width < available_width ? preferred_width : available_width);
    lv_obj_set_style_text_font(s_alert, ft_layout_font(14), LV_PART_MAIN);
    lv_msgbox_add_title(s_alert, title != RT_NULL ? title : "FeatherTalk");
    lv_msgbox_add_text(s_alert, message != RT_NULL ? message : "");
    s_alert_button = lv_msgbox_add_close_button(s_alert);
    lv_obj_add_event_cb(s_alert, alert_deleted_cb, LV_EVENT_DELETE, RT_NULL);
}

static void nav_back_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if (s_notification_visible ||
        (s_notification_panel != RT_NULL &&
         notification_current_y() > notification_closed_y()))
    {
        notification_settle(false);
        return;
    }
    (void)ft_router_back();
}

static void nav_home_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if (s_notification_visible ||
        (s_notification_panel != RT_NULL &&
         notification_current_y() > notification_closed_y()))
        notification_settle(false);
    ft_router_home();
}

static void nav_search_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if (s_notification_visible ||
        (s_notification_panel != RT_NULL &&
         notification_current_y() > notification_closed_y()))
        notification_settle(false);
    ft_pages_open_search();
}

static lv_obj_t *create_nav_button(lv_obj_t *parent, ft_icon_id_t icon_id,
                                   lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *icon;

    lv_obj_set_height(button, lv_pct(100));
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, RT_NULL);

    icon = ft_icon_create(button, icon_id, ft_layout_icon_size(32U), true);
    lv_obj_center(icon);
    return button;
}

static lv_obj_t *create_quick_button(lv_obj_t *parent,
                                     feathertalk_quick_control_t control,
                                     ft_icon_id_t icon_id,
                                     const char *name)
{
    ft_quick_view_t *view = &s_quick_views[control];
    view->button = lv_button_create(parent);
    lv_obj_set_height(view->button, lv_pct(100));
    lv_obj_set_width(view->button, 0);
    lv_obj_set_flex_grow(view->button, 1);
    lv_obj_set_style_radius(view->button, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(view->button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(view->button, ft_layout_px(5), LV_PART_MAIN);
    lv_obj_set_style_pad_row(view->button, ft_layout_px(2), LV_PART_MAIN);
    lv_obj_set_flex_flow(view->button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view->button, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(view->button, quick_button_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)control);
    view->icon = ft_icon_create(view->button, icon_id,
                                ft_layout_icon_size(32U), false);
    view->name_label = lv_label_create(view->button);
    lv_label_set_text(view->name_label, name);
    lv_obj_set_width(view->name_label, lv_pct(100));
    lv_obj_set_style_text_align(view->name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(view->name_label, ft_layout_font(12), LV_PART_MAIN);
    lv_label_set_long_mode(view->name_label, LV_LABEL_LONG_WRAP);
    view->state_label = lv_label_create(view->button);
    lv_label_set_text(view->state_label, ft_preferences_text("不可用", "Unavailable"));
    lv_obj_set_width(view->state_label, lv_pct(100));
    lv_obj_set_style_text_align(view->state_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(view->state_label, ft_layout_font(12), LV_PART_MAIN);
    return view->button;
}

#ifdef FEATHERTALK_UI_TEST_MODE
lv_obj_t *ft_ui_test_get_nav_button(ft_nav_button_id_t button_id)
{
    if ((button_id < 0) || (button_id >= FT_NAV_COUNT))
    {
        return RT_NULL;
    }
    return s_nav_buttons[button_id];
}

lv_obj_t *ft_ui_test_get_status_bar(void) { return s_status_bar; }
bool ft_ui_test_status_monitor_visible(void)
{
    lv_area_t bar_area;
    lv_area_t monitor_area;
    const char *text;
    if (s_status_bar == RT_NULL || !lv_obj_is_valid(s_status_bar) ||
        s_status_metrics == RT_NULL || !lv_obj_is_valid(s_status_metrics) ||
        lv_obj_has_flag(s_status_metrics, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_obj_update_layout(s_status_bar);
    lv_obj_get_coords(s_status_bar, &bar_area);
    lv_obj_get_coords(s_status_metrics, &monitor_area);
    text = lv_label_get_text(s_status_metrics);
    return text != RT_NULL && text[0] == 'F' && strstr(text, "M") != RT_NULL &&
           monitor_area.x1 >= bar_area.x1 && monitor_area.x2 <= bar_area.x2 &&
           monitor_area.y1 >= bar_area.y1 && monitor_area.y2 <= bar_area.y2;
}
bool ft_ui_test_wallpaper_cached(void)
{
    return s_wallpaper_active && s_wallpaper_cache.draw_buf != RT_NULL &&
           s_wallpaper_cache.draw_buf->header.cf == LV_COLOR_FORMAT_RGB565 &&
           s_wallpaper_cache.non_black_pixels > 0U &&
           s_wallpaper_cache.checksum != 0U;
}
bool ft_ui_test_shell_seams_closed(void)
{
    lv_area_t status_area;
    lv_area_t content_area;
    lv_area_t wallpaper_area;
    lv_area_t nav_area;

    if (s_status_bar == RT_NULL || !lv_obj_is_valid(s_status_bar) ||
        s_content_viewport == RT_NULL || !lv_obj_is_valid(s_content_viewport) ||
        s_nav_bar == RT_NULL || !lv_obj_is_valid(s_nav_bar) ||
        s_wallpaper_image == RT_NULL || !lv_obj_is_valid(s_wallpaper_image))
        return false;
    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(s_status_bar, &status_area);
    lv_obj_get_coords(s_content_viewport, &content_area);
    lv_obj_get_coords(s_wallpaper_image, &wallpaper_area);
    lv_obj_get_coords(s_nav_bar, &nav_area);
    return status_area.y2 + 1 == content_area.y1 &&
           content_area.y2 + 1 == nav_area.y1 &&
           wallpaper_area.x1 == content_area.x1 &&
           wallpaper_area.x2 == content_area.x2 &&
           wallpaper_area.y1 == content_area.y1 &&
           wallpaper_area.y2 == content_area.y2 &&
           lv_obj_get_style_pad_row(lv_screen_active(), LV_PART_MAIN) == 0 &&
           lv_obj_get_style_bg_opa(lv_screen_active(), LV_PART_MAIN) ==
               LV_OPA_COVER;
}
lv_obj_t *ft_ui_test_get_notification_panel(void) { return s_notification_panel; }
int32_t ft_ui_test_notification_y(void)
{
    return s_notification_panel != RT_NULL ? notification_current_y() : 0;
}
void ft_ui_test_notification_drag_begin(int32_t pointer_y)
{
    s_notification_suppress_click = false;
    notification_drag_begin(pointer_y);
}
void ft_ui_test_notification_drag_move(int32_t pointer_y)
{
    notification_drag_update(pointer_y);
}
void ft_ui_test_notification_drag_end(void)
{
    (void)notification_drag_finish();
    s_notification_suppress_click = false;
}
void ft_ui_test_notification_fling(int32_t start_y, int32_t end_y,
                                   uint32_t duration_ms, uint32_t release_delay_ms)
{
    uint32_t base = 1000U;
    s_notification_suppress_click = false;
    notification_drag_begin_at(start_y, base);
    notification_drag_update_at(end_y, base + duration_ms);
    (void)notification_drag_finish_at(base + duration_ms + release_delay_ms);
}
bool ft_ui_test_notification_mask_visible(void)
{
    return s_notification_mask != RT_NULL && lv_obj_is_valid(s_notification_mask) &&
           !lv_obj_has_flag(s_notification_mask, LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t *ft_ui_test_get_notification_mask(void) { return s_notification_mask; }
lv_obj_t *ft_ui_test_get_notification_clear(void) { return s_notification_clear; }
lv_obj_t *ft_ui_test_get_quick_button(feathertalk_quick_control_t control)
{
    return control < FEATHERTALK_QUICK_COUNT ? s_quick_views[control].button : RT_NULL;
}
lv_obj_t *ft_ui_test_get_brightness_slider(void) { return s_brightness_slider; }
bool ft_ui_test_quick_available(feathertalk_quick_control_t control)
{
    return control < FEATHERTALK_QUICK_COUNT && s_quick_views[control].available;
}
bool ft_ui_test_quick_enabled(feathertalk_quick_control_t control)
{
    return control < FEATHERTALK_QUICK_COUNT && s_quick_views[control].enabled;
}
bool ft_ui_test_quick_connected(feathertalk_quick_control_t control)
{
    return control < FEATHERTALK_QUICK_COUNT && s_quick_views[control].connected;
}
uint8_t ft_ui_test_quick_signal(void)
{
    return s_quick_views[FEATHERTALK_QUICK_WIFI].signal_percent;
}
bool ft_ui_test_status_radio_icons_present(void)
{
    return s_status_wifi != RT_NULL && lv_obj_is_valid(s_status_wifi) &&
           s_status_bluetooth != RT_NULL && lv_obj_is_valid(s_status_bluetooth);
}
ft_icon_id_t ft_ui_test_wifi_signal_icon(bool connected, uint8_t signal_percent)
{
    return wifi_signal_icon(connected, signal_percent);
}
uint8_t ft_ui_test_brightness(void) { return ft_platform_get_brightness(); }
size_t ft_ui_test_notification_count(void) { return ft_notifications_count(); }
size_t ft_ui_test_notification_unread(void) { return ft_notifications_unread_count(); }
bool ft_ui_test_notification_remove(size_t index)
{
    ft_notification_t notification;
    if (!ft_notifications_get(index, &notification)) return false;
    if (!ft_notifications_remove(notification.id)) return false;
    notification_render();
    return true;
}
void ft_ui_test_notification_reset(void)
{
    ft_notifications_clear();
    notification_render();
}
bool ft_ui_test_language_surface(ft_language_t language)
{
    const char *bluetooth = language == FT_LANGUAGE_ZH_CN ? "蓝牙" : "Bluetooth";
    const char *panel = language == FT_LANGUAGE_ZH_CN ? "快捷设置" : "Quick settings";
    return s_quick_views[FEATHERTALK_QUICK_BLUETOOTH].name_label != RT_NULL &&
           lv_obj_is_valid(s_quick_views[FEATHERTALK_QUICK_BLUETOOTH].name_label) &&
           strcmp(lv_label_get_text(s_quick_views[FEATHERTALK_QUICK_BLUETOOTH].name_label),
                  bluetooth) == 0 &&
           s_notification_panel_title != RT_NULL &&
           lv_obj_is_valid(s_notification_panel_title) &&
           strcmp(lv_label_get_text(s_notification_panel_title), panel) == 0;
}
uint32_t ft_ui_test_notification_drag_applied(void) { return s_notification_drag_applied; }
uint32_t ft_ui_test_notification_drag_skipped(void) { return s_notification_drag_skipped; }
uint32_t ft_ui_test_notification_mask_applied(void) { return s_notification_mask_applied; }
uint32_t ft_ui_test_notification_render_count(void) { return s_notification_render_count; }
lv_obj_t *ft_ui_test_get_alert_button(void)
{
    return (s_alert_button != RT_NULL && lv_obj_is_valid(s_alert_button)) ? s_alert_button : RT_NULL;
}
bool ft_ui_test_notification_is_visible(void) { return s_notification_visible; }
#endif

int feathertalk_ui_init(void)
{
    lv_obj_t *screen;
    lv_obj_t *status;
    lv_obj_t *content;
    lv_obj_t *nav;
    lv_obj_t *brand;
    lv_obj_t *status_info;
    lv_obj_t *panel_header;
    lv_obj_t *panel_title;
    lv_obj_t *clear_label;
    lv_obj_t *quick_row;
    lv_obj_t *brightness_row;
    lv_obj_t *brightness_icon;
    lv_obj_t *brightness_name;
    lv_obj_t *notifications_title;
    lv_display_t *display;
    const ft_ui_layout_t *layout;
    int result;
    size_t app_count;

    if (s_ui_initialized)
    {
        return RT_EOK;
    }

    s_ui_thread = rt_thread_self();

    display = lv_display_get_default();
    if (display == RT_NULL) return -RT_ERROR;
#if LV_USE_SYSMON && LV_USE_PERF_MONITOR
    /* Keep LVGL's backend active, but replace its bottom-left overlay with the
     * product status-bar monitor below.  The default label crosses the hard
     * content/navigation boundary on 480 x 800 and previously looked like a
     * stale dirty block. */
    lv_sysmon_hide_performance(display);
#endif
    result = ft_platform_touch_configure();
    rt_kprintf("[FeatherTalk UI] touch input: %s (long-press=500ms scroll-limit=18px)\n",
               result == RT_EOK ? "ready" : "unavailable");
    ft_layout_init(display);
    layout = ft_layout_get();
    /* LVGL creates each display with LV_FONT_DEFAULT (Montserrat 14).  Most
     * FeatherTalk labels select an application font explicitly, but labels
     * created by stock widgets or simple status rows otherwise keep that
     * Latin-only theme font and render Chinese as placeholder boxes.  Make
     * the 14 px Noto Sans SC build the display-wide normal font; its fallback
     * remains Montserrat, so LVGL symbols and Latin text are preserved. */
    (void)lv_theme_default_init(display,
                                lv_palette_main(LV_PALETTE_BLUE),
                                lv_palette_main(LV_PALETTE_RED),
                                LV_THEME_DEFAULT_DARK,
                                ft_layout_font(14));
    s_accent = lv_color_hex(FT_DEFAULT_ACCENT);
    s_page_background = lv_color_black();
    ft_preferences_init();
    ft_notifications_init();
    s_notification_render_revision = 0U;
    s_notification_mask_level = 0U;
    s_notification_animating = false;
    s_notification_drag_samples = 0U;
    s_notification_drag_applied = 0U;
    s_notification_drag_skipped = 0U;
    s_notification_mask_applied = 0U;
    s_notification_mask_skipped = 0U;
    s_notification_render_count = 0U;
    s_notification_render_skipped = 0U;
    screen = lv_screen_active();
    lv_obj_clean(screen);
    ft_ui_style_page(screen);
    /* The screen is the opaque shell clear plane, not a routed page.  A
     * wallpaper lives only inside the content viewport; making this parent
     * transparent exposes stale display pixels wherever Flex leaves a gap. */
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_row(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(screen, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    status = create_bar(screen, layout->status_bar_height);
    s_status_bar = status;
    lv_obj_add_flag(status, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(status, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(status, notification_gesture_cb, LV_EVENT_ALL, RT_NULL);
    brand = lv_label_create(status);
    lv_label_set_text(brand, layout->compact ? "FT" : "FeatherTalk");
    lv_obj_set_style_text_font(brand, ft_layout_font(14), LV_PART_MAIN);
    ft_ui_register_accent(brand, FT_ACCENT_TEXT);

    s_status_metrics = lv_label_create(status);
    lv_label_set_text(s_status_metrics, layout->compact ? "F--/-- M--K" :
                      "FPS --/--  MEM --/--K");
    lv_obj_set_style_text_font(s_status_metrics,
                               ft_layout_font(layout->compact ? 10 : 12),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_metrics, lv_color_hex(0xB8B8B8),
                                LV_PART_MAIN);

    status_info = lv_obj_create(status);
    lv_obj_remove_style_all(status_info);
    lv_obj_remove_flag(status_info, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(status_info, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_style_pad_column(status_info, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_set_flex_flow(status_info, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_info, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_status_wifi = ft_icon_create(status_info, FT_ICON_WIFI_OFF,
                                   ft_layout_icon_size(24U), false);
    s_status_bluetooth = ft_icon_create(status_info, FT_ICON_BLUETOOTH,
                                        ft_layout_icon_size(24U), false);
    status_radio_refresh(RT_NULL, false);
    s_notification_badge = lv_label_create(status_info);
    lv_label_set_text(s_notification_badge, "0");
    lv_obj_set_style_text_font(s_notification_badge, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_notification_badge, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_notification_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_notification_badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_notification_badge, ft_layout_px(5), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_notification_badge, ft_layout_px(1), LV_PART_MAIN);
    lv_obj_add_flag(s_notification_badge, LV_OBJ_FLAG_HIDDEN);
    ft_ui_register_accent(s_notification_badge, FT_ACCENT_BACKGROUND);
    s_status_uptime = lv_label_create(status_info);
    lv_obj_set_style_text_font(s_status_uptime, ft_layout_font(12), LV_PART_MAIN);
    lv_label_set_text(s_status_uptime, "Starting system status...");
    (void)ft_icon_create(status_info, FT_ICON_BATTERY, ft_layout_icon_size(24U), false);

    content = lv_obj_create(screen);
    s_content_viewport = content;
    ft_ui_style_page(content);
    /* This object is the hard clip viewport between the status and navigation
     * bars.  Tile handles may overflow their local desktop container, but the
     * content subtree must never draw through the navigation seam. */
    lv_obj_remove_flag(content, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    s_wallpaper_image = lv_image_create(content);
    lv_obj_set_size(s_wallpaper_image, lv_pct(100), lv_pct(100));
    lv_obj_center(s_wallpaper_image);
    lv_image_set_inner_align(s_wallpaper_image, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_remove_flag(s_wallpaper_image,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                       LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(s_wallpaper_image,
                    LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_wallpaper_image);
    if (s_wallpaper_native_path[0] != '\0')
    {
        char wallpaper_path[sizeof(s_wallpaper_native_path)];
        rt_strncpy(wallpaper_path, s_wallpaper_native_path,
                   sizeof(wallpaper_path) - 1U);
        wallpaper_path[sizeof(wallpaper_path) - 1U] = '\0';
        ft_ui_set_page_wallpaper(wallpaper_path);
    }

    nav = create_bar(screen, layout->nav_bar_height);
    s_nav_bar = nav;
    lv_obj_set_style_pad_all(nav, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(nav, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    ft_ui_register_accent(nav, FT_ACCENT_BORDER);
    s_nav_buttons[FT_NAV_BACK] = create_nav_button(nav, FT_ICON_BACK, nav_back_cb);
    s_nav_buttons[FT_NAV_HOME] = create_nav_button(nav, FT_ICON_HOME, nav_home_cb);
    s_nav_buttons[FT_NAV_SEARCH] = create_nav_button(nav, FT_ICON_SEARCH, nav_search_cb);

    s_notification_mask = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_notification_mask);
    lv_obj_set_size(s_notification_mask, lv_pct(100),
                    layout->screen_height - layout->status_bar_height - layout->nav_bar_height);
    lv_obj_set_pos(s_notification_mask, 0, layout->status_bar_height);
    lv_obj_set_style_bg_color(s_notification_mask, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_notification_mask, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_notification_mask, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_notification_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_notification_mask, notification_mask_clicked_cb,
                        LV_EVENT_CLICKED, RT_NULL);

    s_notification_panel = lv_obj_create(lv_layer_top());
    ft_ui_style_panel(s_notification_panel);
    lv_obj_set_size(s_notification_panel, lv_pct(100), layout->notification_height);
    lv_obj_set_pos(s_notification_panel, 0, -layout->notification_height);
    lv_obj_add_flag(s_notification_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(s_notification_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_notification_panel, notification_gesture_cb, LV_EVENT_ALL, RT_NULL);
    lv_obj_set_style_pad_all(s_notification_panel, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_notification_panel, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_notification_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(s_notification_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_notification_panel, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    ft_ui_register_accent(s_notification_panel, FT_ACCENT_BORDER);

    panel_header = lv_obj_create(s_notification_panel);
    lv_obj_remove_style_all(panel_header);
    lv_obj_set_size(panel_header, lv_pct(100), ft_layout_px(34));
    lv_obj_set_flex_flow(panel_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel_header, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(panel_header, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_add_event_cb(panel_header, notification_child_gesture_cb,
                        LV_EVENT_GESTURE, RT_NULL);
    panel_title = lv_label_create(panel_header);
    s_notification_panel_title = panel_title;
    lv_label_set_text(panel_title, ft_preferences_text("快捷设置", "Quick settings"));
    lv_obj_set_style_text_font(panel_title, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_set_flex_grow(panel_title, 1);
    s_notification_summary = lv_label_create(panel_header);
    lv_label_set_text(s_notification_summary,
                      ft_preferences_text("0 条未读 / 共 0 条", "0 unread / 0 total"));
    lv_obj_set_style_text_font(s_notification_summary, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_notification_summary, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
    s_notification_clear = lv_button_create(panel_header);
    lv_obj_set_size(s_notification_clear, LV_SIZE_CONTENT, ft_layout_px(30));
    lv_obj_set_style_bg_opa(s_notification_clear, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_notification_clear, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_notification_clear, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_add_event_cb(s_notification_clear, notification_clear_cb, LV_EVENT_CLICKED, RT_NULL);
    clear_label = lv_label_create(s_notification_clear);
    s_notification_clear_label = clear_label;
    lv_label_set_text(clear_label, ft_preferences_text("清除", "Clear"));
    lv_obj_set_style_text_font(clear_label, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_center(clear_label);

    quick_row = lv_obj_create(s_notification_panel);
    lv_obj_remove_style_all(quick_row);
    lv_obj_set_size(quick_row, lv_pct(100), layout->compact ? ft_layout_px(82) : ft_layout_px(104));
    lv_obj_set_style_pad_column(quick_row, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_set_flex_flow(quick_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(quick_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(quick_row, notification_child_gesture_cb, LV_EVENT_GESTURE, RT_NULL);
    (void)create_quick_button(quick_row, FEATHERTALK_QUICK_WIFI, FT_ICON_WIFI, "Wi-Fi");
    (void)create_quick_button(quick_row, FEATHERTALK_QUICK_BLUETOOTH,
                              FT_ICON_BLUETOOTH,
                              ft_preferences_text("蓝牙", "Bluetooth"));
    (void)create_quick_button(quick_row, FEATHERTALK_QUICK_BRIGHTNESS,
                              FT_ICON_BRIGHTNESS,
                              ft_preferences_text("亮度", "Brightness"));
    (void)create_quick_button(quick_row, FEATHERTALK_QUICK_ROTATION,
                              FT_ICON_ROTATION,
                              ft_preferences_text("自动旋转", "Auto-rotate"));

    brightness_row = lv_obj_create(s_notification_panel);
    lv_obj_remove_style_all(brightness_row);
    lv_obj_set_size(brightness_row, lv_pct(100), ft_layout_px(42));
    lv_obj_set_style_pad_column(brightness_row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(brightness_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brightness_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    brightness_icon = ft_icon_create(brightness_row, FT_ICON_BRIGHTNESS,
                                     ft_layout_icon_size(24U), false);
    LV_UNUSED(brightness_icon);
    brightness_name = lv_label_create(brightness_row);
    s_notification_brightness_name = brightness_name;
    lv_label_set_text(brightness_name, ft_preferences_text("亮度", "Brightness"));
    lv_obj_set_style_text_font(brightness_name, ft_layout_font(14), LV_PART_MAIN);
    s_brightness_slider = lv_slider_create(brightness_row);
    lv_obj_set_width(s_brightness_slider, 0);
    lv_obj_set_flex_grow(s_brightness_slider, 1);
    lv_slider_set_range(s_brightness_slider, 0, 100);
    lv_slider_set_value(s_brightness_slider, ft_platform_get_brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_brightness_slider, brightness_changed_cb,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    s_brightness_value = lv_label_create(brightness_row);
    lv_label_set_text(s_brightness_value, "100%");
    lv_obj_set_style_text_font(s_brightness_value, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_set_width(s_brightness_value, ft_layout_px(78));
    lv_obj_set_style_text_align(s_brightness_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    notifications_title = lv_label_create(s_notification_panel);
    s_notification_list_title = notifications_title;
    lv_label_set_text(notifications_title,
                      ft_preferences_text("通知（左右滑动可删除）",
                                          "Notifications (swipe sideways to delete)"));
    lv_obj_set_style_text_font(notifications_title, ft_layout_font(14), LV_PART_MAIN);
    s_notification_list = lv_obj_create(s_notification_panel);
    ft_ui_style_panel(s_notification_list);
    lv_obj_set_width(s_notification_list, lv_pct(100));
    lv_obj_set_height(s_notification_list, 0);
    lv_obj_set_flex_grow(s_notification_list, 1);
    lv_obj_set_style_pad_all(s_notification_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_notification_list, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_notification_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_notification_list, LV_DIR_VER);
    lv_obj_add_event_cb(s_notification_list, notification_child_gesture_cb,
                        LV_EVENT_GESTURE, RT_NULL);
    ft_ui_apply_language();
    quick_views_refresh();
    notification_render();

    result = ft_router_init(content);
    if (result != RT_EOK)
    {
        rt_kprintf("[FeatherTalk UI] router init failed: %d\n", result);
        return result;
    }

    ft_metrics_init(display, screen);
    status_timer_cb(RT_NULL);
    (void)lv_timer_create(status_timer_cb, 1000U, RT_NULL);
    /* Switching the product port to full-frame direct scanout replaces the
     * display draw buffers after LVGL created its default screen.  Rebuild the
     * first invalid area only after the complete shell exists; otherwise the
     * one pre-port refresh can be consumed while the scene is still empty and
     * the refresh timer remains paused forever. */
    lv_obj_invalidate(screen);
    s_ui_initialized = true;
    (void)ft_apps_get(&app_count);
    rt_kprintf("[FeatherTalk UI] shell ready: %ldx%ld apps=%lu route-depth=%lu\n",
               (long)lv_display_get_horizontal_resolution(display),
               (long)lv_display_get_vertical_resolution(display),
               (unsigned long)app_count,
               (unsigned long)ft_router_depth());
#ifdef FEATHERTALK_UI_TEST_MODE
    ft_ui_test_start();
#endif
    return RT_EOK;
}

#ifdef RT_USING_FINSH
typedef enum
{
    FT_BENCH_SCENE_HOME = 0,
    FT_BENCH_SCENE_SEARCH,
    FT_BENCH_SCENE_SYSTEM,
    FT_BENCH_SCENE_SETTINGS,
    FT_BENCH_SCENE_MEDIA,
    FT_BENCH_SCENE_RECORDER,
    FT_BENCH_SCENE_GALLERY,
    FT_BENCH_SCENE_FILES,
    FT_BENCH_SCENE_ABOUT,
    FT_BENCH_SCENE_SETTINGS_DISPLAY,
    FT_BENCH_SCENE_SETTINGS_AUDIO,
    FT_BENCH_SCENE_SETTINGS_WIFI,
    FT_BENCH_SCENE_SETTINGS_BLUETOOTH,
    FT_BENCH_SCENE_SETTINGS_STORAGE,
    FT_BENCH_SCENE_SETTINGS_USB,
    FT_BENCH_SCENE_SETTINGS_TIME_LANGUAGE,
    FT_BENCH_SCENE_SETTINGS_PERSONALIZATION,
    FT_BENCH_SCENE_ALL_APPS,
    FT_BENCH_SCENE_SHADE_OPEN,
    FT_BENCH_SCENE_SHADE_DRAG,
    FT_BENCH_SCENE_SEARCH_KEYBOARD,
    FT_BENCH_SCENE_SETTINGS_KEYBOARD,
    FT_BENCH_SCENE_TILE_EDIT,
    FT_BENCH_SCENE_GALLERY_VIEWER,
    FT_BENCH_SCENE_FILES_ACTION,
    FT_BENCH_SCENE_MEDIA_PLAYING,
    FT_BENCH_SCENE_ALERT,
    FT_BENCH_SCENE_COUNT
} ft_bench_scene_t;

static const char *const s_bench_scene_names[FT_BENCH_SCENE_COUNT] =
{
    "home", "search", "system", "settings", "media", "recorder",
    "gallery", "files", "about", "settings-display", "settings-audio",
    "settings-wifi", "settings-bluetooth", "settings-storage", "settings-usb",
    "settings-time-language", "settings-personalization", "all-apps",
    "shade-open", "shade-drag", "search-keyboard", "settings-keyboard",
    "tile-edit", "gallery-viewer", "files-action", "media-playing", "alert"
};

static void benchmark_scene_reset(void)
{
    if (s_alert != RT_NULL && lv_obj_is_valid(s_alert))
        lv_msgbox_close(s_alert);
    ft_tiles_exit_edit();
    if (s_notification_panel != RT_NULL && lv_obj_is_valid(s_notification_panel))
    {
        lv_anim_delete(s_notification_panel, notification_anim_y_cb);
        s_notification_visible = false;
        s_notification_dragging = false;
        s_notification_animating = false;
        notification_anim_y_cb(s_notification_panel, notification_closed_y());
        notification_anim_completed_cb(RT_NULL);
    }
    ft_router_home();
}

static int benchmark_scene_open_page(ft_page_id_t page)
{
    int result;

    if (page == FT_PAGE_HOME) return RT_EOK;
    if (page >= FT_PAGE_SETTINGS_DISPLAY)
    {
        result = ft_router_push(FT_PAGE_SETTINGS);
        if (result != RT_EOK) return result;
    }
    return ft_router_push(page);
}

static void benchmark_scene_async_cb(void *user_data)
{
    ft_bench_scene_t scene = (ft_bench_scene_t)((uintptr_t)user_data - 1U);
    uint32_t start_ms = rt_tick_get_millisecond();
    int result = RT_EOK;
    bool scene_result = true;

    if ((unsigned)scene >= FT_BENCH_SCENE_COUNT) return;
    benchmark_scene_reset();
    if (scene <= FT_BENCH_SCENE_SETTINGS_PERSONALIZATION)
        result = benchmark_scene_open_page((ft_page_id_t)scene);
    else
    {
        switch (scene)
        {
        case FT_BENCH_SCENE_ALL_APPS:
            ft_pages_show_all_apps();
            break;
        case FT_BENCH_SCENE_SHADE_OPEN:
            notification_settle(true);
            break;
        case FT_BENCH_SCENE_SHADE_DRAG:
        {
            int32_t middle = notification_closed_y() +
                             (notification_open_y() - notification_closed_y()) / 2;
            ft_notifications_mark_all_read();
            notification_render();
            s_notification_visible = true;
            notification_anim_y_cb(s_notification_panel, middle);
            break;
        }
        case FT_BENCH_SCENE_SEARCH_KEYBOARD:
            result = benchmark_scene_open_page(FT_PAGE_SEARCH);
            if (result == RT_EOK)
                scene_result = ft_pages_benchmark_set_keyboard_visible(true);
            break;
        case FT_BENCH_SCENE_SETTINGS_KEYBOARD:
            result = benchmark_scene_open_page(FT_PAGE_SETTINGS);
            if (result == RT_EOK)
                scene_result = ft_pages_benchmark_set_keyboard_visible(true);
            break;
        case FT_BENCH_SCENE_TILE_EDIT:
            scene_result = ft_tiles_preview_edit(0U);
            break;
        case FT_BENCH_SCENE_GALLERY_VIEWER:
        {
            const ft_ui_preferences_t *preferences = ft_preferences_get();
            const char *path = preferences->wallpaper_path[0] != '\0' ?
                               preferences->wallpaper_path : "/flash/Pictures/02.jpg";
            scene_result = ft_gallery_request_open_file(path);
            if (scene_result) result = benchmark_scene_open_page(FT_PAGE_GALLERY);
            break;
        }
        case FT_BENCH_SCENE_FILES_ACTION:
            result = benchmark_scene_open_page(FT_PAGE_FILES);
            if (result == RT_EOK)
                scene_result = ft_pages_benchmark_open_file_action();
            break;
        case FT_BENCH_SCENE_MEDIA_PLAYING:
            result = benchmark_scene_open_page(FT_PAGE_MEDIA);
            if (result == RT_EOK)
                scene_result = ft_pages_benchmark_set_media_playing(true);
            break;
        case FT_BENCH_SCENE_ALERT:
            feathertalk_ui_alert("FeatherTalk", "GPU/CPU pipeline performance scene");
            break;
        default:
            result = -RT_EINVAL;
            break;
        }
    }
    if (!scene_result && result == RT_EOK) result = -RT_ERROR;
    if (lv_screen_active() != RT_NULL)
        lv_obj_invalidate(lv_screen_active());
    rt_kprintf("[UI-SCENE] ready id=%u name=%s page=%d depth=%lu setup=%lums result=%d\n",
               (unsigned)scene, s_bench_scene_names[scene],
               (int)ft_router_current_page(), (unsigned long)ft_router_depth(),
               (unsigned long)(rt_tick_get_millisecond() - start_ms), result);
}

static int feather_ui_scene(int argc, char **argv)
{
    unsigned long scene;
    char *end = RT_NULL;
    lv_result_t result;
    size_t i;

    if (!s_ui_initialized) return -RT_ERROR;
    if (argc < 2 || strcmp(argv[1], "list") == 0)
    {
        for (i = 0U; i < FT_BENCH_SCENE_COUNT; i++)
            rt_kprintf("%u\t%s\n", (unsigned)i, s_bench_scene_names[i]);
        return argc < 2 ? -RT_EINVAL : RT_EOK;
    }
    scene = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || scene >= FT_BENCH_SCENE_COUNT)
        return -RT_EINVAL;
    lv_lock();
    result = lv_async_call(benchmark_scene_async_cb,
                           (void *)(uintptr_t)(scene + 1U));
    lv_unlock();
    if (result != LV_RESULT_OK) return -RT_ENOMEM;
    rt_kprintf("[UI-SCENE] queued id=%lu name=%s\n", scene,
               s_bench_scene_names[scene]);
    return RT_EOK;
}
MSH_CMD_EXPORT(feather_ui_scene,
               Select one repeatable UI performance scene; use list for IDs.);

static void tile_preview_async_cb(void *user_data)
{
    uintptr_t request = (uintptr_t)user_data;
    if (request == 0U)
        ft_tiles_exit_edit();
    else if (!ft_tiles_preview_edit((size_t)(request - 1U)))
        rt_kprintf("FeatherTalk UI: tile preview index is unavailable\n");
}

static int feather_ui_tile_preview(int argc, char **argv)
{
    uintptr_t request;
    lv_result_t result;

    if (!s_ui_initialized) return -RT_ERROR;
    if (argc < 2)
    {
        rt_kprintf("usage: feather_ui_tile_preview <index|off>\n");
        return -RT_EINVAL;
    }
    if (strcmp(argv[1], "off") == 0)
        request = 0U;
    else
    {
        char *end = RT_NULL;
        unsigned long index = strtoul(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0') return -RT_EINVAL;
        request = (uintptr_t)index + 1U;
    }

    lv_lock();
    result = lv_async_call(tile_preview_async_cb, (void *)request);
    lv_unlock();
    if (result != LV_RESULT_OK) return -RT_ENOMEM;
    rt_kprintf("FeatherTalk UI: tile edit preview queued\n");
    return RT_EOK;
}
MSH_CMD_EXPORT(feather_ui_tile_preview,
               Preview or close Tile edit animation without synthetic storage tests.);

static int feather_ui_status(void)
{
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    const ft_ui_layout_t *layout = ft_layout_get();
    ft_preferences_store_status_t preference_store;
    feathertalk_system_status_t system_status;
    feathertalk_quick_status_t quick_status;
    size_t selected_tile = ft_tiles_selected();
    rt_kprintf("FeatherTalk UI: initialized=%d page=%d route-depth=%lu accent=#%06lx objects=%lu overflow=%lu\n",
               s_ui_initialized ? 1 : 0,
               (int)ft_router_current_page(),
               (unsigned long)ft_router_depth(),
               (unsigned long)(lv_color_to_u32(s_accent) & 0xFFFFFFUL),
               (unsigned long)ft_ui_accent_object_count(),
               (unsigned long)s_accent_overflow_count);
    rt_kprintf("FeatherTalk UI preferences: revision=%lu tile-opacity=%u background=%d "
               "wallpaper=%s active=%d notification=%d\n",
               (unsigned long)preferences->revision, preferences->tile_opa,
               (int)preferences->background,
               preferences->wallpaper_path[0] != '\0' ?
                   preferences->wallpaper_path : "(none)",
               ft_ui_page_wallpaper_active() ? 1 : 0,
               s_notification_visible ? 1 : 0);
    rt_kprintf("FeatherTalk UI wallpaper cache: %lux%lu RGB565 source=%lux%lu "
               "non-black=%lu checksum=0x%08lx\n",
               s_wallpaper_cache.draw_buf != RT_NULL ?
                   (unsigned long)s_wallpaper_cache.draw_buf->header.w : 0UL,
               s_wallpaper_cache.draw_buf != RT_NULL ?
                   (unsigned long)s_wallpaper_cache.draw_buf->header.h : 0UL,
               (unsigned long)s_wallpaper_cache.source_width,
               (unsigned long)s_wallpaper_cache.source_height,
               (unsigned long)s_wallpaper_cache.non_black_pixels,
               (unsigned long)s_wallpaper_cache.checksum);
    if (ft_preferences_store_get_status(&preference_store) == RT_EOK)
        rt_kprintf("FeatherTalk UI preference store: loaded=%d slots=0x%02x active=%d "
                   "generation=%lu dirty=%d frozen=%d test=%d writes=%lu/%lu error=%d\n",
                   preference_store.loaded_from_storage ? 1 : 0,
                   preference_store.valid_slots, preference_store.active_slot,
                   (unsigned long)preference_store.generation,
                   preference_store.dirty ? 1 : 0,
                   preference_store.frozen ? 1 : 0,
                   preference_store.test_suspended ? 1 : 0,
                   (unsigned long)preference_store.successful_writes,
                   (unsigned long)preference_store.failed_writes,
                   preference_store.last_error);
    else
        rt_kprintf("FeatherTalk UI preference store: unavailable\n");
    rt_kprintf("FeatherTalk UI layout: %ldx%ld scale=%ld%% compact=%d landscape=%d "
               "tiles=%u column=%ld bars=%ld/%ld keyboard=%ld\n",
               (long)layout->screen_width, (long)layout->screen_height,
               (long)layout->scale_percent, layout->compact ? 1 : 0,
               layout->landscape ? 1 : 0, layout->tile_columns,
               (long)layout->tile_column_width, (long)layout->status_bar_height,
               (long)layout->nav_bar_height, (long)layout->keyboard_height);
    if (feathertalk_ipc_get_system_status(&system_status) == RT_EOK)
        rt_kprintf("FeatherTalk UI system IPC: seq=%lu age=%lums time=%lu flags=0x%02x network=%u battery=%u\n",
                   (unsigned long)system_status.sequence,
                   (unsigned long)(rt_tick_get_millisecond() - system_status.received_ms),
                   (unsigned long)system_status.unix_time, system_status.flags,
                   system_status.network_state, system_status.battery_percent);
    else
        rt_kprintf("FeatherTalk UI system IPC: unavailable\n");
    if (feathertalk_ipc_get_quick_status(&quick_status) == RT_EOK)
        rt_kprintf("FeatherTalk UI quick IPC: seq=%lu caps=0x%02x enabled=0x%02x "
                   "connected=0x%02x wifi-signal=%u brightness=%u rotation=%u result=%u\n",
                   (unsigned long)quick_status.sequence, quick_status.capabilities,
                   quick_status.enabled, quick_status.connected,
                   quick_status.wifi_signal_percent, quick_status.brightness_percent,
                   quick_status.rotation, quick_status.result);
    else
        rt_kprintf("FeatherTalk UI quick IPC: unavailable\n");
    rt_kprintf("FeatherTalk UI shade: y=%ld mask=%d notifications=%lu unread=%lu "
               "brightness=%u pwm-routed=%d\n",
               s_notification_panel != RT_NULL ? (long)notification_current_y() : 0L,
               s_notification_mask != RT_NULL &&
               !lv_obj_has_flag(s_notification_mask, LV_OBJ_FLAG_HIDDEN) ? 1 : 0,
               (unsigned long)ft_notifications_count(),
               (unsigned long)ft_notifications_unread_count(),
               ft_platform_get_brightness(),
               ft_platform_brightness_available() ? 1 : 0);
    rt_kprintf("FeatherTalk UI shade perf: drag=%lu applied=%lu skipped=%lu "
               "mask=%lu/%lu render=%lu/%lu revision=%lu\n",
               (unsigned long)s_notification_drag_samples,
               (unsigned long)s_notification_drag_applied,
               (unsigned long)s_notification_drag_skipped,
               (unsigned long)s_notification_mask_applied,
               (unsigned long)s_notification_mask_skipped,
               (unsigned long)s_notification_render_count,
               (unsigned long)s_notification_render_skipped,
               (unsigned long)ft_notifications_revision());
    rt_kprintf("FeatherTalk UI tiles: editing=%d selected=%ld\n",
               ft_tiles_editing() ? 1 : 0,
               selected_tile == SIZE_MAX ? -1L : (long)selected_tile);
    ft_platform_touch_print_status();
    ft_metrics_print_status();
#ifdef FEATHERTALK_UI_TEST_MODE
    ft_ui_test_print_status();
#endif
    return 0;
}
MSH_CMD_EXPORT(feather_ui_status, Show FeatherTalk M55 UI shell status);
static int feather_ui_bench(void)
{
    int result;
    if (!s_ui_initialized) return -RT_ERROR;
    result = ft_metrics_bench_request(60U);
    if (result == RT_EOK)
        rt_kprintf("FeatherTalk UI: queued 60-frame full-screen GPU benchmark\n");
    else
        rt_kprintf("FeatherTalk UI: benchmark request failed (%d)\n", result);
    return result;
}
MSH_CMD_EXPORT(feather_ui_bench, Run a repeatable 60-frame full-screen GPU benchmark);
#ifdef FEATHERTALK_UI_TEST_MODE
static int feather_ui_notification_preview(void)
{
    if (!s_ui_initialized) return -RT_ERROR;
    /* The LVGL timer consumes this request in its own thread. */
    s_notification_preview_requested = true;
    rt_kprintf("FeatherTalk UI: notification preview requested\n");
    return 0;
}
MSH_CMD_EXPORT(feather_ui_notification_preview,
               Open the Simplified-Chinese notification preview);
#endif
#endif
