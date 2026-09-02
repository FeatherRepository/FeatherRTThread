#include "feather_ui_icons.h"

static int16_t ix(const fui_rect_t *bounds, int value)
{
    return (int16_t)(bounds->x + ((int32_t)bounds->width * value + 12) / 24);
}

static int16_t iy(const fui_rect_t *bounds, int value)
{
    return (int16_t)(bounds->y + ((int32_t)bounds->height * value + 12) / 24);
}

static uint16_t icon_stroke(const fui_rect_t *bounds)
{
    int16_t extent = bounds->width < bounds->height ? bounds->width : bounds->height;
    uint16_t stroke = (uint16_t)(extent / 10);
    return stroke < 1U ? 1U : stroke;
}

static bool iline(fui_painter_t *painter, const fui_rect_t *bounds,
                  int x1, int y1, int x2, int y2, fui_color_t color)
{
    return fui_painter_line_batch(painter, ix(bounds, x1), iy(bounds, y1),
                                  ix(bounds, x2), iy(bounds, y2),
                                  icon_stroke(bounds), color);
}

static bool ibox(fui_painter_t *painter, const fui_rect_t *bounds,
                 int x, int y, int width, int height, fui_color_t color)
{
    bool ok = iline(painter, bounds, x, y, x + width, y, color);
    ok = iline(painter, bounds, x + width, y, x + width, y + height, color) && ok;
    ok = iline(painter, bounds, x + width, y + height, x, y + height, color) && ok;
    return iline(painter, bounds, x, y + height, x, y, color) && ok;
}

static bool idot(fui_painter_t *painter, const fui_rect_t *bounds,
                 int x, int y, int diameter, fui_color_t color)
{
    fui_rect_t dot = {ix(bounds, x), iy(bounds, y),
                      (int16_t)(((int32_t)bounds->width * diameter + 12) / 24),
                      (int16_t)(((int32_t)bounds->height * diameter + 12) / 24)};
    if (dot.width < 2) dot.width = 2;
    if (dot.height < 2) dot.height = 2;
    return fui_painter_rect(painter, dot,
                            (uint16_t)((dot.width < dot.height ? dot.width :
                                       dot.height) / 2), color);
}

static bool icircle(fui_painter_t *painter, const fui_rect_t *bounds,
                    int center_x, int center_y, int radius, fui_color_t color)
{
    bool ok = iline(painter, bounds, center_x - radius, center_y,
                    center_x - radius * 7 / 10, center_y - radius * 7 / 10,
                    color);
    ok = iline(painter, bounds, center_x - radius * 7 / 10,
               center_y - radius * 7 / 10, center_x, center_y - radius,
               color) && ok;
    ok = iline(painter, bounds, center_x, center_y - radius,
               center_x + radius * 7 / 10, center_y - radius * 7 / 10,
               color) && ok;
    ok = iline(painter, bounds, center_x + radius * 7 / 10,
               center_y - radius * 7 / 10, center_x + radius, center_y,
               color) && ok;
    ok = iline(painter, bounds, center_x + radius, center_y,
               center_x + radius * 7 / 10, center_y + radius * 7 / 10,
               color) && ok;
    ok = iline(painter, bounds, center_x + radius * 7 / 10,
               center_y + radius * 7 / 10, center_x, center_y + radius,
               color) && ok;
    ok = iline(painter, bounds, center_x, center_y + radius,
               center_x - radius * 7 / 10, center_y + radius * 7 / 10,
               color) && ok;
    return iline(painter, bounds, center_x - radius * 7 / 10,
                 center_y + radius * 7 / 10, center_x - radius, center_y,
                 color) && ok;
}

static bool draw_gear(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = iline(p, b, 12, 2, 12, 22, c);
    ok = iline(p, b, 2, 12, 22, 12, c) && ok;
    ok = iline(p, b, 5, 5, 19, 19, c) && ok;
    ok = iline(p, b, 19, 5, 5, 19, c) && ok;
    return idot(p, b, 9, 9, 6, c) && ok;
}

static bool draw_folder(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = iline(p, b, 2, 7, 9, 7, c);
    ok = iline(p, b, 9, 7, 11, 4, c) && ok;
    ok = iline(p, b, 11, 4, 16, 4, c) && ok;
    ok = iline(p, b, 16, 4, 19, 7, c) && ok;
    ok = iline(p, b, 19, 7, 22, 7, c) && ok;
    ok = iline(p, b, 22, 7, 20, 20, c) && ok;
    return iline(p, b, 20, 20, 4, 20, c) && ok;
}

static bool draw_picture(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = ibox(p, b, 2, 3, 20, 18, c);
    ok = iline(p, b, 4, 18, 9, 12, c) && ok;
    return iline(p, b, 9, 12, 18, 18, c) && ok;
}

static bool draw_speaker(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = iline(p, b, 3, 9, 7, 9, c);
    ok = iline(p, b, 7, 9, 13, 4, c) && ok;
    ok = iline(p, b, 13, 4, 13, 21, c) && ok;
    ok = iline(p, b, 13, 21, 7, 16, c) && ok;
    ok = iline(p, b, 7, 16, 3, 16, c) && ok;
    return iline(p, b, 17, 8, 20, 12, c) && ok;
}

static bool draw_microphone(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = iline(p, b, 9, 3, 9, 13, c);
    ok = iline(p, b, 15, 3, 15, 13, c) && ok;
    ok = iline(p, b, 6, 12, 9, 17, c) && ok;
    ok = iline(p, b, 9, 17, 15, 17, c) && ok;
    ok = iline(p, b, 15, 17, 18, 12, c) && ok;
    ok = iline(p, b, 12, 18, 12, 22, c) && ok;
    return iline(p, b, 8, 22, 16, 22, c) && ok;
}

static bool draw_chip(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = ibox(p, b, 5, 5, 14, 14, c);
    ok = iline(p, b, 2, 8, 5, 8, c) && ok;
    ok = iline(p, b, 19, 8, 22, 8, c) && ok;
    return idot(p, b, 9, 9, 6, c) && ok;
}

static bool draw_usb(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = iline(p, b, 12, 21, 12, 4, c);
    ok = iline(p, b, 12, 4, 9, 7, c) && ok;
    ok = iline(p, b, 12, 4, 15, 7, c) && ok;
    ok = iline(p, b, 12, 12, 6, 9, c) && ok;
    return iline(p, b, 12, 16, 19, 12, c) && ok;
}

static bool draw_wifi(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = iline(p, b, 2, 8, 12, 3, c);
    ok = iline(p, b, 12, 3, 22, 8, c) && ok;
    ok = iline(p, b, 6, 14, 12, 10, c) && ok;
    ok = iline(p, b, 12, 10, 18, 14, c) && ok;
    return idot(p, b, 10, 18, 4, c) && ok;
}

static bool draw_bluetooth(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = iline(p, b, 11, 2, 11, 22, c);
    ok = iline(p, b, 11, 2, 18, 8, c) && ok;
    ok = iline(p, b, 18, 8, 6, 18, c) && ok;
    ok = iline(p, b, 6, 6, 18, 17, c) && ok;
    return iline(p, b, 18, 17, 11, 22, c) && ok;
}

static bool draw_clock(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = icircle(p, b, 12, 12, 10, c);
    ok = iline(p, b, 12, 6, 12, 13, c) && ok;
    return iline(p, b, 12, 13, 17, 16, c) && ok;
}

static bool draw_display(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = ibox(p, b, 2, 3, 20, 15, c);
    ok = iline(p, b, 12, 18, 12, 22, c) && ok;
    return iline(p, b, 7, 22, 17, 22, c) && ok;
}

static bool draw_file(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = iline(p, b, 5, 2, 15, 2, c);
    ok = iline(p, b, 15, 2, 20, 7, c) && ok;
    ok = iline(p, b, 20, 7, 20, 22, c) && ok;
    ok = iline(p, b, 20, 22, 5, 22, c) && ok;
    ok = iline(p, b, 5, 22, 5, 2, c) && ok;
    ok = iline(p, b, 15, 2, 15, 7, c) && ok;
    return iline(p, b, 15, 7, 20, 7, c) && ok;
}

static bool draw_drive(fui_painter_t *p, const fui_rect_t *b, fui_color_t c)
{
    bool ok = ibox(p, b, 3, 4, 18, 16, c);
    ok = iline(p, b, 3, 15, 21, 15, c) && ok;
    ok = idot(p, b, 16, 17, 2, c) && ok;
    return idot(p, b, 19, 17, 2, c) && ok;
}

static bool draw_badge(fui_painter_t *p, const fui_rect_t *b,
                       unsigned code, fui_color_t c)
{
    bool ok = true;
    if ((code & 1U) != 0U) ok = iline(p, b, 18, 2, 22, 2, c) && ok;
    if ((code & 2U) != 0U) ok = iline(p, b, 22, 2, 22, 6, c) && ok;
    if ((code & 4U) != 0U) ok = iline(p, b, 18, 6, 22, 6, c) && ok;
    return ok;
}

bool fui_icon_is_valid(fui_icon_id_t id)
{
    return id > FUI_ICON_NONE && id < FUI_ICON_COUNT;
}

bool fui_icon_draw(fui_painter_t *p, fui_icon_id_t id,
                   fui_rect_t b, fui_color_t c)
{
    bool ok = true;
    if (p == NULL || b.width <= 0 || b.height <= 0) return false;
    switch (id)
    {
    case FUI_ICON_NONE: return true;
    case FUI_ICON_APP_SETTINGS: return draw_gear(p, &b, c);
    case FUI_ICON_APP_FILES: return draw_folder(p, &b, c);
    case FUI_ICON_APP_GALLERY: return draw_picture(p, &b, c);
    case FUI_ICON_APP_AUDIO: return draw_speaker(p, &b, c);
    case FUI_ICON_APP_RECORDER: return draw_microphone(p, &b, c);
    case FUI_ICON_APP_SYSTEM: return draw_chip(p, &b, c);
    case FUI_ICON_APP_USB_SD:
        ok = draw_usb(p, &b, c); return draw_badge(p, &b, 1U, c) && ok;
    case FUI_ICON_APP_WIFI: return draw_wifi(p, &b, c);
    case FUI_ICON_APP_BLUETOOTH: return draw_bluetooth(p, &b, c);
    case FUI_ICON_APP_MEDIA:
        ok = iline(p, &b, 7, 4, 19, 12, c);
        ok = iline(p, &b, 19, 12, 7, 20, c) && ok;
        return iline(p, &b, 7, 20, 7, 4, c) && ok;

    case FUI_ICON_SETTING_DISPLAY: return draw_display(p, &b, c);
    case FUI_ICON_SETTING_AUDIO:
        ok = draw_speaker(p, &b, c); return draw_badge(p, &b, 3U, c) && ok;
    case FUI_ICON_SETTING_WIFI:
        ok = draw_wifi(p, &b, c); return draw_badge(p, &b, 1U, c) && ok;
    case FUI_ICON_SETTING_BLUETOOTH:
        ok = draw_bluetooth(p, &b, c); return draw_badge(p, &b, 2U, c) && ok;
    case FUI_ICON_SETTING_STORAGE: return draw_drive(p, &b, c);
    case FUI_ICON_SETTING_USB:
        ok = draw_usb(p, &b, c); return draw_badge(p, &b, 3U, c) && ok;
    case FUI_ICON_SETTING_TIME_LANGUAGE:
        ok = draw_clock(p, &b, c); return draw_badge(p, &b, 5U, c) && ok;
    case FUI_ICON_SETTING_PERSONALIZATION:
        ok = icircle(p, &b, 9, 9, 6, c);
        ok = iline(p, &b, 13, 13, 21, 21, c) && ok;
        return iline(p, &b, 17, 21, 21, 17, c) && ok;
    case FUI_ICON_SETTING_SYSTEM:
        ok = draw_chip(p, &b, c); return draw_badge(p, &b, 4U, c) && ok;
    case FUI_ICON_SETTING_ABOUT:
        ok = icircle(p, &b, 12, 12, 10, c);
        ok = idot(p, &b, 11, 6, 2, c) && ok;
        return iline(p, &b, 12, 11, 12, 18, c) && ok;

    case FUI_ICON_BRIGHTNESS:
        ok = icircle(p, &b, 12, 12, 5, c);
        ok = iline(p, &b, 12, 1, 12, 4, c) && ok;
        ok = iline(p, &b, 12, 20, 12, 23, c) && ok;
        ok = iline(p, &b, 1, 12, 4, 12, c) && ok;
        return iline(p, &b, 20, 12, 23, 12, c) && ok;
    case FUI_ICON_ROTATION:
        ok = iline(p, &b, 4, 8, 8, 4, c);
        ok = iline(p, &b, 8, 4, 17, 4, c) && ok;
        ok = iline(p, &b, 17, 4, 21, 8, c) && ok;
        ok = iline(p, &b, 20, 16, 16, 20, c) && ok;
        ok = iline(p, &b, 16, 20, 7, 20, c) && ok;
        ok = iline(p, &b, 7, 20, 3, 16, c) && ok;
        ok = iline(p, &b, 3, 5, 3, 10, c) && ok;
        return iline(p, &b, 21, 19, 21, 14, c) && ok;
    case FUI_ICON_OUTPUT_VOLUME:
        ok = draw_speaker(p, &b, c); return draw_badge(p, &b, 1U, c) && ok;
    case FUI_ICON_INPUT_GAIN:
        ok = draw_microphone(p, &b, c); return draw_badge(p, &b, 2U, c) && ok;
    case FUI_ICON_SAMPLE_RATE:
        ok = iline(p, &b, 2, 13, 5, 13, c);
        ok = iline(p, &b, 5, 13, 8, 5, c) && ok;
        ok = iline(p, &b, 8, 5, 12, 20, c) && ok;
        ok = iline(p, &b, 12, 20, 16, 7, c) && ok;
        return iline(p, &b, 16, 7, 22, 12, c) && ok;
    case FUI_ICON_SAMPLE_DEPTH:
        ok = ibox(p, &b, 3, 4, 18, 16, c);
        ok = iline(p, &b, 7, 8, 7, 16, c) && ok;
        ok = iline(p, &b, 12, 6, 12, 18, c) && ok;
        return iline(p, &b, 17, 10, 17, 14, c) && ok;
    case FUI_ICON_CHANNELS:
        ok = iline(p, &b, 5, 3, 5, 21, c);
        ok = iline(p, &b, 12, 6, 12, 18, c) && ok;
        return iline(p, &b, 19, 3, 19, 21, c) && ok;
    case FUI_ICON_SPEAKER:
        ok = draw_speaker(p, &b, c); return draw_badge(p, &b, 5U, c) && ok;
    case FUI_ICON_MICROPHONE:
        ok = draw_microphone(p, &b, c); return draw_badge(p, &b, 5U, c) && ok;
    case FUI_ICON_WIFI: return draw_wifi(p, &b, c);
    case FUI_ICON_BLUETOOTH: return draw_bluetooth(p, &b, c);
    case FUI_ICON_NETWORK_SCAN:
        ok = draw_wifi(p, &b, c); return draw_badge(p, &b, 7U, c) && ok;
    case FUI_ICON_PAIRED_DEVICES:
        ok = draw_bluetooth(p, &b, c); return draw_badge(p, &b, 7U, c) && ok;
    case FUI_ICON_FLASH:
        ok = draw_drive(p, &b, c); return draw_badge(p, &b, 1U, c) && ok;
    case FUI_ICON_SD_CARD:
        ok = iline(p, &b, 5, 2, 17, 2, c);
        ok = iline(p, &b, 17, 2, 21, 6, c) && ok;
        ok = iline(p, &b, 21, 6, 21, 22, c) && ok;
        ok = iline(p, &b, 21, 22, 5, 22, c) && ok;
        ok = iline(p, &b, 5, 22, 5, 2, c) && ok;
        return iline(p, &b, 9, 2, 9, 7, c) && ok;
    case FUI_ICON_CAPACITY:
        ok = icircle(p, &b, 12, 12, 9, c);
        ok = iline(p, &b, 12, 12, 12, 3, c) && ok;
        return iline(p, &b, 12, 12, 20, 16, c) && ok;
    case FUI_ICON_BROWSE:
        ok = draw_folder(p, &b, c); return draw_badge(p, &b, 2U, c) && ok;
    case FUI_ICON_FORMAT:
        ok = draw_drive(p, &b, c);
        ok = iline(p, &b, 6, 10, 18, 10, c) && ok;
        return iline(p, &b, 12, 7, 12, 13, c) && ok;
    case FUI_ICON_USB_ROLE: return draw_usb(p, &b, c);
    case FUI_ICON_USB_STORAGE:
        ok = draw_drive(p, &b, c); return draw_badge(p, &b, 3U, c) && ok;
    case FUI_ICON_USB_AUDIO:
        ok = draw_speaker(p, &b, c); return draw_badge(p, &b, 6U, c) && ok;
    case FUI_ICON_USB_OUTPUT:
        ok = draw_usb(p, &b, c); return iline(p, &b, 17, 17, 22, 17, c) && ok;
    case FUI_ICON_USB_INPUT:
        ok = draw_usb(p, &b, c); return iline(p, &b, 17, 20, 22, 20, c) && ok;
    case FUI_ICON_USB_STATUS:
        ok = draw_usb(p, &b, c); return draw_badge(p, &b, 7U, c) && ok;
    case FUI_ICON_CLOCK: return draw_clock(p, &b, c);
    case FUI_ICON_TIMEZONE:
        ok = icircle(p, &b, 12, 12, 10, c);
        ok = iline(p, &b, 2, 12, 22, 12, c) && ok;
        ok = iline(p, &b, 12, 2, 8, 12, c) && ok;
        return iline(p, &b, 8, 12, 12, 22, c) && ok;
    case FUI_ICON_LANGUAGE:
        ok = iline(p, &b, 4, 5, 13, 5, c);
        ok = iline(p, &b, 8, 3, 8, 14, c) && ok;
        ok = iline(p, &b, 4, 10, 12, 10, c) && ok;
        ok = iline(p, &b, 15, 20, 19, 8, c) && ok;
        return iline(p, &b, 16, 16, 22, 16, c) && ok;
    case FUI_ICON_KEYBOARD:
        ok = ibox(p, &b, 2, 6, 20, 13, c);
        ok = iline(p, &b, 5, 10, 19, 10, c) && ok;
        ok = iline(p, &b, 5, 14, 19, 14, c) && ok;
        return iline(p, &b, 8, 17, 16, 17, c) && ok;
    case FUI_ICON_ACCENT:
        ok = icircle(p, &b, 12, 12, 9, c);
        ok = idot(p, &b, 7, 6, 3, c) && ok;
        ok = idot(p, &b, 14, 5, 3, c) && ok;
        return idot(p, &b, 16, 13, 3, c) && ok;
    case FUI_ICON_OPACITY:
        ok = ibox(p, &b, 3, 3, 14, 14, c);
        return ibox(p, &b, 7, 7, 14, 14, c) && ok;
    case FUI_ICON_BACKGROUND:
        ok = ibox(p, &b, 2, 4, 20, 16, c);
        return iline(p, &b, 2, 16, 22, 8, c) && ok;
    case FUI_ICON_WALLPAPER:
        ok = draw_picture(p, &b, c); return draw_badge(p, &b, 7U, c) && ok;
    case FUI_ICON_PROCESSOR: return draw_chip(p, &b, c);
    case FUI_ICON_ONCHIP_MEMORY:
        ok = draw_chip(p, &b, c); return iline(p, &b, 8, 12, 16, 12, c) && ok;
    case FUI_ICON_EXTERNAL_MEMORY:
        ok = draw_drive(p, &b, c); return iline(p, &b, 1, 12, 5, 12, c) && ok;
    case FUI_ICON_GPU2D:
        ok = draw_chip(p, &b, c);
        ok = iline(p, &b, 8, 16, 12, 8, c) && ok;
        return iline(p, &b, 12, 8, 17, 16, c) && ok;
    case FUI_ICON_PERIPHERALS:
        ok = idot(p, &b, 10, 10, 4, c);
        ok = iline(p, &b, 12, 12, 12, 2, c) && ok;
        ok = iline(p, &b, 12, 12, 22, 12, c) && ok;
        ok = iline(p, &b, 12, 12, 12, 22, c) && ok;
        return iline(p, &b, 12, 12, 2, 12, c) && ok;
    case FUI_ICON_PRODUCT:
        ok = ibox(p, &b, 5, 2, 14, 20, c); return idot(p, &b, 11, 18, 2, c) && ok;
    case FUI_ICON_UI_ENGINE:
        ok = draw_gear(p, &b, c); return draw_badge(p, &b, 6U, c) && ok;
    case FUI_ICON_RENDER_CONTRACT:
        ok = iline(p, &b, 4, 12, 9, 17, c);
        ok = iline(p, &b, 9, 17, 20, 5, c) && ok;
        return ibox(p, &b, 2, 2, 20, 20, c) && ok;
    case FUI_ICON_SOFTWARE:
        ok = draw_file(p, &b, c);
        ok = iline(p, &b, 8, 10, 16, 10, c) && ok;
        return iline(p, &b, 8, 15, 16, 15, c) && ok;
    case FUI_ICON_RECORDINGS:
        ok = draw_folder(p, &b, c); return draw_badge(p, &b, 6U, c) && ok;
    case FUI_ICON_FOLDER: return draw_folder(p, &b, c);
    case FUI_ICON_FILE: return draw_file(p, &b, c);
    case FUI_ICON_LOADING:
        ok = icircle(p, &b, 12, 12, 9, c);
        return iline(p, &b, 12, 3, 18, 5, c) && ok;
    case FUI_ICON_ERROR:
        ok = icircle(p, &b, 12, 12, 10, c);
        ok = iline(p, &b, 12, 6, 12, 15, c) && ok;
        return idot(p, &b, 11, 18, 2, c) && ok;
    case FUI_ICON_STATUS_WIFI:
        ok = draw_wifi(p, &b, c); return draw_badge(p, &b, 2U, c) && ok;
    case FUI_ICON_STATUS_BLUETOOTH:
        ok = draw_bluetooth(p, &b, c); return draw_badge(p, &b, 4U, c) && ok;
    case FUI_ICON_QUICK_BRIGHTNESS:
        ok = fui_icon_draw(p, FUI_ICON_BRIGHTNESS, b, c);
        return draw_badge(p, &b, 3U, c) && ok;
    case FUI_ICON_QUICK_ROTATION:
        ok = fui_icon_draw(p, FUI_ICON_ROTATION, b, c);
        return draw_badge(p, &b, 6U, c) && ok;
    default: return false;
    }
}
