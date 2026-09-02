#include <string.h>
#include "fui_internal.h"

static fui_rect_t rect_intersect(fui_rect_t a, fui_rect_t b)
{
    int32_t x1 = a.x > b.x ? a.x : b.x;
    int32_t y1 = a.y > b.y ? a.y : b.y;
    int32_t x2a = (int32_t)a.x + a.width;
    int32_t y2a = (int32_t)a.y + a.height;
    int32_t x2b = (int32_t)b.x + b.width;
    int32_t y2b = (int32_t)b.y + b.height;
    int32_t x2 = x2a < x2b ? x2a : x2b;
    int32_t y2 = y2a < y2b ? y2a : y2b;
    fui_rect_t result;

    result.x = (int16_t)x1;
    result.y = (int16_t)y1;
    result.width = x2 > x1 ? (int16_t)(x2 - x1) : 0;
    result.height = y2 > y1 ? (int16_t)(y2 - y1) : 0;
    return result;
}

enum
{
    FUI_CLIP_LEFT = 1U,
    FUI_CLIP_RIGHT = 2U,
    FUI_CLIP_TOP = 4U,
    FUI_CLIP_BOTTOM = 8U
};

static uint8_t line_outcode(const fui_rect_t *clip, int32_t x, int32_t y)
{
    uint8_t code = 0U;
    int32_t right = (int32_t)clip->x + clip->width - 1;
    int32_t bottom = (int32_t)clip->y + clip->height - 1;
    if (x < clip->x) code |= FUI_CLIP_LEFT;
    else if (x > right) code |= FUI_CLIP_RIGHT;
    if (y < clip->y) code |= FUI_CLIP_TOP;
    else if (y > bottom) code |= FUI_CLIP_BOTTOM;
    return code;
}

static bool clip_line(const fui_rect_t *clip, int32_t *x1, int32_t *y1,
                      int32_t *x2, int32_t *y2)
{
    uint8_t c1;
    uint8_t c2;
    int32_t right;
    int32_t bottom;
    if (clip->width <= 0 || clip->height <= 0) return false;
    right = (int32_t)clip->x + clip->width - 1;
    bottom = (int32_t)clip->y + clip->height - 1;
    for (;;)
    {
        int32_t x;
        int32_t y;
        uint8_t outside;
        c1 = line_outcode(clip, *x1, *y1);
        c2 = line_outcode(clip, *x2, *y2);
        if ((c1 | c2) == 0U) return true;
        if ((c1 & c2) != 0U) return false;
        outside = c1 != 0U ? c1 : c2;
        if ((outside & FUI_CLIP_TOP) != 0U)
        {
            y = clip->y;
            x = *x1 + (*x2 - *x1) * (y - *y1) / (*y2 - *y1);
        }
        else if ((outside & FUI_CLIP_BOTTOM) != 0U)
        {
            y = bottom;
            x = *x1 + (*x2 - *x1) * (y - *y1) / (*y2 - *y1);
        }
        else if ((outside & FUI_CLIP_RIGHT) != 0U)
        {
            x = right;
            y = *y1 + (*y2 - *y1) * (x - *x1) / (*x2 - *x1);
        }
        else
        {
            x = clip->x;
            y = *y1 + (*y2 - *y1) * (x - *x1) / (*x2 - *x1);
        }
        if (outside == c1)
        {
            *x1 = x;
            *y1 = y;
        }
        else
        {
            *x2 = x;
            *y2 = y;
        }
    }
}

bool fui_rect_contains(const fui_rect_t *rect, int16_t x, int16_t y)
{
    if (rect == RT_NULL || rect->width <= 0 || rect->height <= 0)
        return false;
    return x >= rect->x && y >= rect->y &&
           x < (int32_t)rect->x + rect->width &&
           y < (int32_t)rect->y + rect->height;
}

void fui_display_list_reset(fui_display_list_t *list)
{
    if (list == RT_NULL) return;
    fui_text_cache_begin_frame();
    list->count = 0U;
    list->line_segment_count = 0U;
    list->overflow = 0U;
}

bool fui_display_list_push(fui_display_list_t *list,
                           const fui_command_t *command)
{
    if (list == RT_NULL || command == RT_NULL) return false;
    if (list->count >= FUI_DISPLAY_LIST_CAPACITY)
    {
        list->overflow++;
        return false;
    }
    list->commands[list->count++] = *command;
    return true;
}

void fui_painter_init(fui_painter_t *painter, fui_display_list_t *list,
                      uint16_t width, uint16_t height)
{
    if (painter == RT_NULL) return;
    painter->list = list;
    painter->screen = (fui_rect_t){0, 0, (int16_t)width, (int16_t)height};
    painter->clip = painter->screen;
}

void fui_painter_set_clip(fui_painter_t *painter, fui_rect_t clip)
{
    if (painter == RT_NULL) return;
    painter->clip = rect_intersect(painter->screen, clip);
}

void fui_painter_reset_clip(fui_painter_t *painter)
{
    if (painter != RT_NULL) painter->clip = painter->screen;
}

bool fui_painter_clear(fui_painter_t *painter, fui_color_t color)
{
    fui_command_t command;
    if (painter == RT_NULL) return false;
    memset(&command, 0, sizeof(command));
    command.type = FUI_CMD_CLEAR;
    command.clip = painter->screen;
    command.color = color;
    return fui_display_list_push(painter->list, &command);
}

bool fui_painter_rect(fui_painter_t *painter, fui_rect_t rect,
                      uint16_t radius, fui_color_t color)
{
    fui_command_t command;
    fui_rect_t visible;
    if (painter == RT_NULL || rect.width <= 0 || rect.height <= 0) return false;
    if ((color >> 24) == 0U) return true;
    visible = rect_intersect(rect, painter->clip);
    if (visible.width <= 0 || visible.height <= 0) return true;
    memset(&command, 0, sizeof(command));
    command.type = FUI_CMD_RECT;
    command.clip = painter->clip;
    command.color = color;
    /* FeatherUI deliberately does not change the VG-Lite scissor while a
     * frame is being encoded: on this IP/driver combination every scissor
     * change flushes the command buffer.  Clip simple geometry while building
     * the immutable display list instead, so all commands remain in one GPU
     * submission. */
    command.data.rect.rect = visible;
    if (radius > (uint16_t)(visible.width / 2))
        radius = (uint16_t)(visible.width / 2);
    if (radius > (uint16_t)(visible.height / 2))
        radius = (uint16_t)(visible.height / 2);
    command.data.rect.radius = radius;
    return fui_display_list_push(painter->list, &command);
}

bool fui_painter_line(fui_painter_t *painter, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint16_t width,
                      fui_color_t color)
{
    fui_command_t command;
    int32_t cx1 = x1;
    int32_t cy1 = y1;
    int32_t cx2 = x2;
    int32_t cy2 = y2;
    int16_t inset;
    fui_rect_t line_clip;
    if (painter == RT_NULL || width == 0U) return false;
    if ((color >> 24) == 0U) return true;
    inset = (int16_t)((width + 1U) / 2U);
    line_clip = painter->clip;
    line_clip.x += inset;
    line_clip.y += inset;
    line_clip.width -= (int16_t)(inset * 2);
    line_clip.height -= (int16_t)(inset * 2);
    if (!clip_line(&line_clip, &cx1, &cy1, &cx2, &cy2)) return true;
    memset(&command, 0, sizeof(command));
    command.type = FUI_CMD_LINE;
    command.clip = painter->clip;
    command.color = color;
    command.data.line.x1 = (int16_t)cx1;
    command.data.line.y1 = (int16_t)cy1;
    command.data.line.x2 = (int16_t)cx2;
    command.data.line.y2 = (int16_t)cy2;
    command.data.line.width = width;
    return fui_display_list_push(painter->list, &command);
}

bool fui_painter_line_batch(fui_painter_t *painter, int16_t x1, int16_t y1,
                            int16_t x2, int16_t y2, uint16_t width,
                            fui_color_t color)
{
    fui_command_t command;
    fui_command_t *last = RT_NULL;
    int32_t cx1 = x1;
    int32_t cy1 = y1;
    int32_t cx2 = x2;
    int32_t cy2 = y2;
    int16_t inset;
    fui_rect_t line_clip;
    if (painter == RT_NULL || painter->list == RT_NULL || width == 0U)
        return false;
    if ((color >> 24) == 0U) return true;
    inset = (int16_t)((width + 1U) / 2U);
    line_clip = painter->clip;
    line_clip.x += inset;
    line_clip.y += inset;
    line_clip.width -= (int16_t)(inset * 2);
    line_clip.height -= (int16_t)(inset * 2);
    if (!clip_line(&line_clip, &cx1, &cy1, &cx2, &cy2)) return true;
    if (painter->list->count > 0U)
        last = &painter->list->commands[painter->list->count - 1U];
    if (last != RT_NULL && last->type == FUI_CMD_LINE_BATCH &&
        last->color == color && last->data.line_batch.width == width &&
        last->data.line_batch.count < FUI_LINE_BATCH_CAPACITY &&
        last->clip.x == painter->clip.x && last->clip.y == painter->clip.y &&
        last->clip.width == painter->clip.width &&
        last->clip.height == painter->clip.height &&
        painter->list->line_segment_count < FUI_LINE_SEGMENT_CAPACITY)
    {
        last->data.line_batch.count++;
        painter->list->line_segments[painter->list->line_segment_count][0] =
            (int16_t)cx1;
        painter->list->line_segments[painter->list->line_segment_count][1] =
            (int16_t)cy1;
        painter->list->line_segments[painter->list->line_segment_count][2] =
            (int16_t)cx2;
        painter->list->line_segments[painter->list->line_segment_count][3] =
            (int16_t)cy2;
        painter->list->line_segment_count++;
        return true;
    }
    if (painter->list->line_segment_count >= FUI_LINE_SEGMENT_CAPACITY)
        return fui_painter_line(painter, (int16_t)cx1, (int16_t)cy1,
                                (int16_t)cx2, (int16_t)cy2, width, color);
    memset(&command, 0, sizeof(command));
    command.type = FUI_CMD_LINE_BATCH;
    command.clip = painter->clip;
    command.color = color;
    command.data.line_batch.width = width;
    command.data.line_batch.count = 1U;
    command.data.line_batch.points =
        &painter->list->line_segments[painter->list->line_segment_count];
    painter->list->line_segments[painter->list->line_segment_count][0] =
        (int16_t)cx1;
    painter->list->line_segments[painter->list->line_segment_count][1] =
        (int16_t)cy1;
    painter->list->line_segments[painter->list->line_segment_count][2] =
        (int16_t)cx2;
    painter->list->line_segments[painter->list->line_segment_count][3] =
        (int16_t)cy2;
    painter->list->line_segment_count++;
    return fui_display_list_push(painter->list, &command);
}

static uint32_t utf8_next(const char **cursor)
{
    const uint8_t *text = (const uint8_t *)*cursor;
    uint32_t codepoint;
    if (text[0] < 0x80U)
    {
        *cursor += 1;
        return text[0];
    }
    if ((text[0] & 0xe0U) == 0xc0U &&
        (text[1] & 0xc0U) == 0x80U)
    {
        codepoint = ((uint32_t)(text[0] & 0x1fU) << 6) |
                    (uint32_t)(text[1] & 0x3fU);
        *cursor += 2;
        return codepoint >= 0x80U ? codepoint : (uint32_t)'?';
    }
    if ((text[0] & 0xf0U) == 0xe0U &&
        (text[1] & 0xc0U) == 0x80U &&
        (text[2] & 0xc0U) == 0x80U)
    {
        codepoint = ((uint32_t)(text[0] & 0x0fU) << 12) |
                    ((uint32_t)(text[1] & 0x3fU) << 6) |
                    (uint32_t)(text[2] & 0x3fU);
        *cursor += 3;
        return codepoint >= 0x800U ? codepoint : (uint32_t)'?';
    }
    *cursor += 1;
    return (uint32_t)'?';
}

bool fui_painter_text(fui_painter_t *painter, int16_t x, int16_t y,
                      uint8_t scale, fui_color_t color, const char *text)
{
    fui_command_t command;
    uint8_t cache_id;
    uint8_t render_scale;
    uint16_t run_width;
    int16_t cursor = x;
    bool ok = true;
    if (painter == RT_NULL || text == RT_NULL) return false;
    if ((color >> 24) == 0U) return true;
    if (scale == 0U) scale = 1U;
    if (fui_text_cache_acquire(text, scale, &cache_id, &run_width,
                               &render_scale))
    {
        const vg_lite_buffer_t *cached = fui_text_cache_get(cache_id);
        fui_rect_t run_rect = {x, y,
                               (int16_t)fui_font_scale_dimension(run_width,
                                                                 render_scale),
                               cached != RT_NULL ?
                               (int16_t)fui_font_scale_dimension(
                                   (uint16_t)cached->height, render_scale) : 0};
        fui_rect_t visible = rect_intersect(run_rect, painter->clip);
        if (visible.width == run_rect.width && visible.height == run_rect.height)
        {
            memset(&command, 0, sizeof(command));
            command.type = FUI_CMD_TEXT_RUN;
            command.clip = painter->clip;
            command.color = color;
            command.data.text_run.x = x;
            command.data.text_run.y = y;
            command.data.text_run.scale = render_scale;
            command.data.text_run.cache_id = cache_id;
            return fui_display_list_push(painter->list, &command);
        }
    }
    while (*text != '\0')
    {
        uint32_t glyph = utf8_next(&text);
        fui_font_glyph_t product;
        if (!fui_font_glyph_get(glyph, 2U, &product))
        {
            ok = false;
            continue;
        }
        fui_rect_t glyph_rect = {cursor, y,
                                 (int16_t)fui_font_scale_dimension(product.width,
                                                                   scale),
                                 (int16_t)fui_font_scale_dimension(product.height,
                                                                   scale)};
        fui_rect_t visible = rect_intersect(glyph_rect, painter->clip);
        if (glyph < 32U || (glyph > 126U && glyph < 0x80U))
            glyph = (uint32_t)'?';
        if (glyph == (uint32_t)' ')
        {
            cursor += (int16_t)fui_font_scale_dimension(product.advance, scale);
            continue;
        }
        /* Partial glyph clipping needs a cropped atlas blit.  Until that
         * command exists, cull edge-crossing glyphs instead of allowing them
         * to bleed outside a container and silently break the frame contract
         * with a hardware scissor change. */
        if (visible.width != glyph_rect.width ||
            visible.height != glyph_rect.height)
        {
            cursor += (int16_t)fui_font_scale_dimension(product.advance, scale);
            continue;
        }
        memset(&command, 0, sizeof(command));
        command.type = FUI_CMD_GLYPH;
        command.clip = painter->clip;
        command.color = color;
        command.data.glyph.x = cursor;
        command.data.glyph.y = y;
        command.data.glyph.scale = scale;
        command.data.glyph.glyph = glyph;
        if (!fui_display_list_push(painter->list, &command)) ok = false;
        cursor += (int16_t)fui_font_scale_dimension(product.advance, scale);
    }
    return ok;
}

bool fui_painter_image_rgb565(fui_painter_t *painter, int16_t x, int16_t y,
                              const fui_image_rgb565_t *image)
{
    fui_command_t command;
    fui_rect_t destination;
    fui_rect_t visible;
    if (painter == RT_NULL || image == RT_NULL || image->pixels == RT_NULL ||
        image->width == 0U || image->height == 0U ||
        image->stride_pixels < image->width)
        return false;
    destination = (fui_rect_t){x, y, (int16_t)image->width,
                               (int16_t)image->height};
    visible = rect_intersect(destination, painter->clip);
    if (visible.width <= 0 || visible.height <= 0) return true;
    memset(&command, 0, sizeof(command));
    command.type = FUI_CMD_IMAGE_RGB565;
    command.clip = painter->clip;
    command.data.image.image = *image;
    command.data.image.source.x = (int16_t)(visible.x - x);
    command.data.image.source.y = (int16_t)(visible.y - y);
    command.data.image.source.width = visible.width;
    command.data.image.source.height = visible.height;
    command.data.image.x = x;
    command.data.image.y = y;
    return fui_display_list_push(painter->list, &command);
}
