#include <limits.h>
#include <stddef.h>
#include "feather_ui_components.h"
#include "fui_internal.h"

static int16_t minimum_i16(int16_t a, int16_t b)
{
    return a < b ? a : b;
}

static int16_t maximum_i16(int16_t a, int16_t b)
{
    return a > b ? a : b;
}

static int32_t clamp_i32(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static bool valid_bounds(const fui_rect_t *bounds)
{
    return bounds != NULL && bounds->width > 0 && bounds->height > 0;
}

static uint32_t utf8_next_component(const char **cursor)
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

int16_t fui_component_text_width(const char *text, uint8_t scale)
{
    int32_t width = 0;
    int32_t trailing = 0;
    if (text == NULL) return 0;
    if (scale == 0U) scale = 1U;
    while (*text != '\0')
    {
        uint32_t glyph = utf8_next_component(&text);
        fui_font_glyph_t product;
        if (!fui_font_glyph_get(glyph, 2U, &product)) continue;
        width += product.advance;
        trailing = 0;
        if (width + trailing >= INT16_MAX) return INT16_MAX;
    }
    return (int16_t)fui_font_scale_dimension((uint16_t)(width + trailing), scale);
}

int16_t fui_component_text_height(const char *text, uint8_t scale)
{
    int16_t height;
    if (scale == 0U) scale = 1U;
    height = fui_font_line_height(scale);
    (void)text;
    return height;
}

static bool disabled(fui_component_state_t state)
{
    return (state & FUI_COMPONENT_STATE_DISABLED) != 0U;
}

static fui_color_t surface_color(fui_component_state_t state,
                                 const fui_component_style_t *style)
{
    if (disabled(state)) return style->surface_disabled;
    if ((state & FUI_COMPONENT_STATE_PRESSED) != 0U)
        return style->surface_pressed;
    if ((state & FUI_COMPONENT_STATE_SELECTED) != 0U)
        return style->surface_selected;
    return style->surface;
}

static fui_color_t foreground_color(fui_component_state_t state,
                                    const fui_component_style_t *style)
{
    return disabled(state) ? style->foreground_disabled : style->foreground;
}

bool fui_component_list_row(fui_painter_t *painter,
                            const fui_list_row_t *row,
                            const fui_component_style_t *style)
{
    fui_color_t foreground;
    fui_color_t leading_color;
    int16_t padding;
    int16_t text_x;
    int16_t title_height;
    int16_t detail_height = 0;
    int16_t title_y;
    int16_t leading_size;
    bool has_detail;
    bool ok;
    if (painter == NULL || row == NULL || style == NULL ||
        !valid_bounds(&row->bounds) || row->title == NULL)
        return false;

    padding = maximum_i16(style->padding, 0);
    leading_size = row->leading_size > 0 ? row->leading_size : 24;
    if (leading_size > row->bounds.height - padding * 2)
        leading_size = maximum_i16((int16_t)(row->bounds.height - padding * 2), 0);
    text_x = (int16_t)(row->bounds.x + padding);
    foreground = foreground_color(row->state, style);
    leading_color = disabled(row->state) ? style->foreground_disabled : style->accent;
    has_detail = row->detail != NULL && row->detail[0] != '\0';
    title_height = fui_component_text_height(row->title,
                                              row->title_scale == 0U ? 1U :
                                                                      row->title_scale);
    if (has_detail)
        detail_height = fui_component_text_height(row->detail,
            row->detail_scale == 0U ? 1U : row->detail_scale);

    ok = fui_painter_rect(painter, row->bounds, style->radius,
                          surface_color(row->state, style));
    if (row->leading_draw != NULL && leading_size > 0)
    {
        fui_rect_t leading = {
            (int16_t)(row->bounds.x + padding),
            (int16_t)(row->bounds.y + (row->bounds.height - leading_size) / 2),
            leading_size,
            leading_size
        };
        row->leading_draw(painter, leading, leading_color,
                          row->leading_context);
        text_x = (int16_t)(leading.x + leading.width + maximum_i16(padding / 2, 4));
    }
    if (has_detail)
    {
        int16_t gap = maximum_i16(row->bounds.height / 14, 3);
        int16_t total = (int16_t)(title_height + gap + detail_height);
        title_y = (int16_t)(row->bounds.y + (row->bounds.height - total) / 2);
        ok = fui_painter_text(painter, text_x, title_y,
                              row->title_scale == 0U ? 1U : row->title_scale,
                              foreground, row->title) && ok;
        ok = fui_painter_text(painter, text_x,
                              (int16_t)(title_y + title_height + gap),
                              row->detail_scale == 0U ? 1U : row->detail_scale,
                              disabled(row->state) ? style->foreground_disabled :
                                                     style->foreground_muted,
                              row->detail) && ok;
    }
    else
    {
        title_y = (int16_t)(row->bounds.y +
                            (row->bounds.height - title_height) / 2);
        ok = fui_painter_text(painter, text_x, title_y,
                              row->title_scale == 0U ? 1U : row->title_scale,
                              foreground, row->title) && ok;
    }
    if (row->show_chevron)
    {
        int16_t size = minimum_i16(maximum_i16(row->bounds.height / 8, 6), 9);
        int16_t x = (int16_t)(row->bounds.x + row->bounds.width - padding - size);
        int16_t y = (int16_t)(row->bounds.y + row->bounds.height / 2 - size);
        fui_color_t color = disabled(row->state) ? style->foreground_disabled :
                                                   style->foreground_muted;
        ok = fui_painter_line(painter, x, y, (int16_t)(x + size),
                              (int16_t)(y + size), 2U, color) && ok;
        ok = fui_painter_line(painter, (int16_t)(x + size),
                              (int16_t)(y + size), x,
                              (int16_t)(y + size * 2), 2U, color) && ok;
    }
    return ok;
}

bool fui_component_button(fui_painter_t *painter,
                          const fui_button_t *button,
                          const fui_component_style_t *style)
{
    fui_color_t background;
    fui_color_t foreground;
    int16_t text_width;
    int16_t text_height;
    uint8_t scale;
    bool ok = true;
    if (painter == NULL || button == NULL || style == NULL ||
        !valid_bounds(&button->bounds) || button->label == NULL)
        return false;
    scale = button->text_scale == 0U ? 1U : button->text_scale;
    foreground = foreground_color(button->state, style);
    if (disabled(button->state)) background = style->surface_disabled;
    else if ((button->state & FUI_COMPONENT_STATE_PRESSED) != 0U)
        background = style->surface_pressed;
    else
    {
        switch (button->variant)
        {
        case FUI_BUTTON_PRIMARY: background = style->accent; break;
        case FUI_BUTTON_DANGER: background = style->danger; break;
        case FUI_BUTTON_GHOST: background = style->surface_alt; break;
        default: background = style->surface; break;
        }
    }
    if (button->variant != FUI_BUTTON_GHOST || (background >> 24) != 0U)
        ok = fui_painter_rect(painter, button->bounds, style->radius,
                              background);
    text_width = fui_component_text_width(button->label, scale);
    text_height = fui_component_text_height(button->label, scale);
    return fui_painter_text(painter,
        (int16_t)(button->bounds.x + (button->bounds.width - text_width) / 2),
        (int16_t)(button->bounds.y + (button->bounds.height - text_height) / 2),
        scale, foreground, button->label) && ok;
}

bool fui_component_switch(fui_painter_t *painter,
                          const fui_switch_t *control,
                          const fui_component_style_t *style)
{
    fui_rect_t knob;
    int16_t inset;
    int16_t diameter;
    fui_color_t track;
    if (painter == NULL || control == NULL || style == NULL ||
        !valid_bounds(&control->bounds))
        return false;
    inset = maximum_i16(control->bounds.height / 8, 2);
    diameter = (int16_t)(control->bounds.height - inset * 2);
    if (diameter <= 0) return false;
    track = disabled(control->state) ? style->surface_disabled :
            (control->checked ? style->accent : style->track);
    knob.x = control->checked ?
        (int16_t)(control->bounds.x + control->bounds.width - inset - diameter) :
        (int16_t)(control->bounds.x + inset);
    knob.y = (int16_t)(control->bounds.y + inset);
    knob.width = diameter;
    knob.height = diameter;
    return fui_painter_rect(painter, control->bounds,
                            (uint16_t)(control->bounds.height / 2), track) &&
           fui_painter_rect(painter, knob, (uint16_t)(diameter / 2),
                            disabled(control->state) ?
                            style->foreground_disabled : style->knob);
}

fui_rect_t fui_component_slider_track_rect(const fui_slider_t *slider)
{
    fui_rect_t empty = {0, 0, 0, 0};
    fui_rect_t track;
    int16_t thumb;
    int16_t track_height;
    if (slider == NULL || !valid_bounds(&slider->bounds)) return empty;
    thumb = minimum_i16(slider->bounds.height, 20);
    thumb = maximum_i16(thumb, 8);
    if (thumb > slider->bounds.width) thumb = slider->bounds.width;
    track_height = minimum_i16(12, maximum_i16(slider->bounds.height / 2, 4));
    track.x = (int16_t)(slider->bounds.x + thumb / 2);
    track.y = (int16_t)(slider->bounds.y +
                        (slider->bounds.height - track_height) / 2);
    track.width = (int16_t)(slider->bounds.width - thumb);
    track.height = track_height;
    if (track.width < 1) track.width = 1;
    return track;
}

int32_t fui_component_slider_value_from_x(const fui_slider_t *slider,
                                          int16_t x)
{
    fui_rect_t track;
    int32_t span;
    int32_t position;
    if (slider == NULL || slider->maximum <= slider->minimum)
        return slider != NULL ? slider->minimum : 0;
    track = fui_component_slider_track_rect(slider);
    if (track.width <= 0) return slider->minimum;
    position = clamp_i32((int32_t)x - track.x, 0, track.width);
    span = slider->maximum - slider->minimum;
    return slider->minimum + (position * span + track.width / 2) / track.width;
}

bool fui_component_slider(fui_painter_t *painter,
                          const fui_slider_t *slider,
                          const fui_component_style_t *style)
{
    fui_rect_t track;
    fui_rect_t fill;
    fui_rect_t knob;
    int32_t span;
    int32_t value;
    int16_t thumb;
    int16_t center;
    fui_color_t accent;
    bool ok;
    if (painter == NULL || slider == NULL || style == NULL ||
        !valid_bounds(&slider->bounds) || slider->maximum <= slider->minimum)
        return false;
    track = fui_component_slider_track_rect(slider);
    span = slider->maximum - slider->minimum;
    value = clamp_i32(slider->value, slider->minimum, slider->maximum) -
            slider->minimum;
    fill = track;
    fill.width = (int16_t)((value * track.width + span / 2) / span);
    thumb = (int16_t)(slider->bounds.width - track.width);
    center = (int16_t)(track.x + fill.width);
    knob = (fui_rect_t){(int16_t)(center - thumb / 2),
                        (int16_t)(slider->bounds.y +
                                  (slider->bounds.height - thumb) / 2),
                        thumb, thumb};
    accent = disabled(slider->state) ? style->foreground_disabled : style->accent;
    ok = fui_painter_rect(painter, track, (uint16_t)(track.height / 2),
                          disabled(slider->state) ? style->surface_disabled :
                                                    style->track);
    if (fill.width > 0)
        ok = fui_painter_rect(painter, fill, (uint16_t)(track.height / 2),
                              accent) && ok;
    return fui_painter_rect(painter, knob, (uint16_t)(thumb / 2),
                            disabled(slider->state) ?
                            style->foreground_disabled : style->knob) && ok;
}

bool fui_component_progress(fui_painter_t *painter,
                            const fui_progress_t *progress,
                            const fui_component_style_t *style)
{
    fui_rect_t fill;
    int32_t span;
    int32_t value;
    fui_color_t accent;
    bool ok;
    if (painter == NULL || progress == NULL || style == NULL ||
        !valid_bounds(&progress->bounds) || progress->maximum <= progress->minimum)
        return false;
    span = progress->maximum - progress->minimum;
    value = clamp_i32(progress->value, progress->minimum, progress->maximum) -
            progress->minimum;
    fill = progress->bounds;
    fill.width = (int16_t)((value * progress->bounds.width + span / 2) / span);
    accent = disabled(progress->state) ? style->foreground_disabled : style->accent;
    ok = fui_painter_rect(painter, progress->bounds,
                          (uint16_t)(progress->bounds.height / 2),
                          disabled(progress->state) ? style->surface_disabled :
                                                      style->track);
    if (fill.width > 0)
        ok = fui_painter_rect(painter, fill,
                              (uint16_t)(progress->bounds.height / 2),
                              accent) && ok;
    return ok;
}

bool fui_component_text_field(fui_painter_t *painter,
                              const fui_text_field_t *field,
                              const fui_component_style_t *style)
{
    fui_rect_t interior;
    const char *text;
    uint8_t scale;
    int16_t text_height;
    int16_t text_x;
    int16_t text_y;
    bool focused;
    bool has_text;
    bool ok;
    if (painter == NULL || field == NULL || style == NULL ||
        !valid_bounds(&field->bounds))
        return false;
    scale = field->text_scale == 0U ? 1U : field->text_scale;
    focused = (field->state & FUI_COMPONENT_STATE_FOCUSED) != 0U;
    has_text = field->text != NULL && field->text[0] != '\0';
    text = has_text ? field->text :
           (field->placeholder != NULL ? field->placeholder : "");
    if (focused)
    {
        ok = fui_painter_rect(painter, field->bounds, style->radius,
                              disabled(field->state) ?
                              style->foreground_disabled : style->accent);
        interior = (fui_rect_t){(int16_t)(field->bounds.x + 2),
                                (int16_t)(field->bounds.y + 2),
                                (int16_t)(field->bounds.width - 4),
                                (int16_t)(field->bounds.height - 4)};
        if (interior.width > 0 && interior.height > 0)
            ok = fui_painter_rect(painter, interior,
                                  style->radius > 2U ? style->radius - 2U : 0U,
                                  disabled(field->state) ? style->surface_disabled :
                                                           style->surface_alt) && ok;
    }
    else
        ok = fui_painter_rect(painter, field->bounds, style->radius,
                              disabled(field->state) ? style->surface_disabled :
                                                       style->surface_alt);
    text_height = fui_component_text_height(text, scale);
    text_x = (int16_t)(field->bounds.x + maximum_i16(style->padding, 0));
    text_y = (int16_t)(field->bounds.y +
                       (field->bounds.height - text_height) / 2);
    ok = fui_painter_text(painter, text_x, text_y, scale,
                          disabled(field->state) ? style->foreground_disabled :
                          (has_text ? style->foreground : style->foreground_muted),
                          text) && ok;
    if (focused && !disabled(field->state))
    {
        int16_t cursor_x = (int16_t)(text_x +
            fui_component_text_width(has_text ? text : "", scale) + 2);
        int16_t right = (int16_t)(field->bounds.x + field->bounds.width -
                                  maximum_i16(style->padding, 0));
        if (cursor_x > right) cursor_x = right;
        ok = fui_painter_line(painter, cursor_x, text_y, cursor_x,
                              (int16_t)(text_y + text_height), 1U,
                              style->accent) && ok;
    }
    return ok;
}

fui_rect_t fui_component_scrollbar_thumb_rect(const fui_scrollbar_t *scrollbar)
{
    fui_rect_t empty = {0, 0, 0, 0};
    fui_rect_t thumb;
    int32_t scrollable_content;
    int32_t scrollable_track;
    if (scrollbar == NULL || !valid_bounds(&scrollbar->bounds) ||
        scrollbar->viewport_extent <= 0 ||
        scrollbar->content_extent <= scrollbar->viewport_extent)
        return empty;
    thumb = scrollbar->bounds;
    thumb.height = (int16_t)(((int32_t)scrollbar->bounds.height *
                              scrollbar->viewport_extent) /
                             scrollbar->content_extent);
    if (thumb.height < scrollbar->minimum_thumb)
        thumb.height = scrollbar->minimum_thumb;
    if (thumb.height > scrollbar->bounds.height)
        thumb.height = scrollbar->bounds.height;
    scrollable_content = scrollbar->content_extent - scrollbar->viewport_extent;
    scrollable_track = scrollbar->bounds.height - thumb.height;
    thumb.y = (int16_t)(scrollbar->bounds.y +
        (clamp_i32(scrollbar->offset, 0, scrollable_content) * scrollable_track +
         scrollable_content / 2) / scrollable_content);
    return thumb;
}

bool fui_component_scrollbar(fui_painter_t *painter,
                             const fui_scrollbar_t *scrollbar,
                             const fui_component_style_t *style)
{
    fui_rect_t thumb;
    fui_color_t thumb_color;
    bool ok;
    if (painter == NULL || scrollbar == NULL || style == NULL)
        return false;
    thumb = fui_component_scrollbar_thumb_rect(scrollbar);
    if (thumb.width <= 0 || thumb.height <= 0) return true;
    ok = fui_painter_rect(painter, scrollbar->bounds,
                          (uint16_t)(scrollbar->bounds.width / 2),
                          style->track);
    thumb_color = disabled(scrollbar->state) ? style->foreground_disabled :
        ((scrollbar->state & FUI_COMPONENT_STATE_PRESSED) != 0U ?
         style->accent : style->foreground_muted);
    return fui_painter_rect(painter, thumb,
                            (uint16_t)(thumb.width / 2), thumb_color) && ok;
}

static fui_component_state_t combined_option_state(fui_component_state_t parent,
                                                    fui_component_state_t option)
{
    return (fui_component_state_t)(parent | option);
}

static bool draw_radio_indicator(fui_painter_t *painter, int16_t center_x,
                                 int16_t center_y, int16_t diameter,
                                 bool selected, fui_component_state_t state,
                                 const fui_component_style_t *style)
{
    fui_rect_t outer;
    fui_rect_t inner;
    fui_color_t ring = disabled(state) ? style->foreground_disabled :
                       (selected ? style->accent : style->foreground_muted);
    fui_color_t background = surface_color(state, style);
    bool ok;
    if (diameter < 8) diameter = 8;
    outer = (fui_rect_t){(int16_t)(center_x - diameter / 2),
                         (int16_t)(center_y - diameter / 2),
                         diameter, diameter};
    inner = (fui_rect_t){(int16_t)(outer.x + 2), (int16_t)(outer.y + 2),
                         (int16_t)(diameter - 4), (int16_t)(diameter - 4)};
    ok = fui_painter_rect(painter, outer, (uint16_t)(diameter / 2), ring);
    if (inner.width > 0 && inner.height > 0)
        ok = fui_painter_rect(painter, inner, (uint16_t)(inner.width / 2),
                              background) && ok;
    if (selected)
    {
        fui_rect_t dot = {(int16_t)(center_x - diameter / 5),
                          (int16_t)(center_y - diameter / 5),
                          (int16_t)(diameter * 2 / 5),
                          (int16_t)(diameter * 2 / 5)};
        ok = fui_painter_rect(painter, dot, (uint16_t)(dot.width / 2), ring) && ok;
    }
    return ok;
}

fui_rect_t fui_component_radio_option_rect(const fui_radio_group_t *group,
                                           uint8_t index)
{
    fui_rect_t empty = {0, 0, 0, 0};
    fui_rect_t rect;
    if (group == NULL || !valid_bounds(&group->bounds) ||
        group->option_count == 0U || index >= group->option_count)
        return empty;
    rect = group->bounds;
    if (group->vertical)
    {
        int16_t top = (int16_t)(((int32_t)group->bounds.height * index) /
                                group->option_count);
        int16_t bottom = (int16_t)(((int32_t)group->bounds.height *
                                   (index + 1U)) / group->option_count);
        rect.y = (int16_t)(group->bounds.y + top);
        rect.height = (int16_t)(bottom - top);
    }
    else
    {
        int16_t left = (int16_t)(((int32_t)group->bounds.width * index) /
                                 group->option_count);
        int16_t right = (int16_t)(((int32_t)group->bounds.width *
                                  (index + 1U)) / group->option_count);
        rect.x = (int16_t)(group->bounds.x + left);
        rect.width = (int16_t)(right - left);
    }
    return rect;
}

int fui_component_radio_index_from_point(const fui_radio_group_t *group,
                                         int16_t x, int16_t y)
{
    uint8_t i;
    if (group == NULL || !fui_rect_contains(&group->bounds, x, y)) return -1;
    for (i = 0U; i < group->option_count; i++)
    {
        fui_rect_t option = fui_component_radio_option_rect(group, i);
        if (fui_rect_contains(&option, x, y) &&
            !disabled(combined_option_state(group->state,
                                             group->options[i].state)))
            return i;
    }
    return -1;
}

bool fui_component_radio_group(fui_painter_t *painter,
                               const fui_radio_group_t *group,
                               const fui_component_style_t *style)
{
    uint8_t i;
    bool ok = true;
    if (painter == NULL || group == NULL || style == NULL ||
        group->options == NULL || group->option_count == 0U ||
        !valid_bounds(&group->bounds))
        return false;
    for (i = 0U; i < group->option_count; i++)
    {
        fui_rect_t option = fui_component_radio_option_rect(group, i);
        fui_component_state_t state = combined_option_state(group->state,
                                                             group->options[i].state);
        int16_t diameter = minimum_i16(option.height / 2, 18);
        int16_t center_x = (int16_t)(option.x + maximum_i16(style->padding, 8) +
                                     diameter / 2);
        int16_t center_y = (int16_t)(option.y + option.height / 2);
        uint8_t scale = group->text_scale == 0U ? 1U : group->text_scale;
        int16_t text_y = (int16_t)(option.y +
            (option.height - fui_component_text_height(group->options[i].label,
                                                        scale)) / 2);
        if ((state & (FUI_COMPONENT_STATE_PRESSED |
                      FUI_COMPONENT_STATE_SELECTED)) != 0U)
            ok = fui_painter_rect(painter, option, style->radius,
                                  surface_color(state, style)) && ok;
        ok = draw_radio_indicator(painter, center_x, center_y, diameter,
                                  i == group->selected_index, state, style) && ok;
        ok = fui_painter_text(painter,
            (int16_t)(center_x + diameter / 2 + maximum_i16(style->padding / 2, 5)),
            text_y, scale, foreground_color(state, style),
            group->options[i].label) && ok;
    }
    return ok;
}

fui_rect_t fui_component_segment_rect(const fui_segmented_control_t *control,
                                      uint8_t index)
{
    fui_rect_t empty = {0, 0, 0, 0};
    fui_rect_t rect;
    int16_t left;
    int16_t right;
    if (control == NULL || !valid_bounds(&control->bounds) ||
        control->option_count == 0U || index >= control->option_count)
        return empty;
    rect = control->bounds;
    left = (int16_t)(((int32_t)control->bounds.width * index) /
                     control->option_count);
    right = (int16_t)(((int32_t)control->bounds.width * (index + 1U)) /
                      control->option_count);
    rect.x = (int16_t)(control->bounds.x + left);
    rect.width = (int16_t)(right - left);
    return rect;
}

int fui_component_segment_index_from_point(const fui_segmented_control_t *control,
                                           int16_t x, int16_t y)
{
    uint8_t i;
    if (control == NULL || !fui_rect_contains(&control->bounds, x, y)) return -1;
    for (i = 0U; i < control->option_count; i++)
    {
        fui_rect_t segment = fui_component_segment_rect(control, i);
        if (fui_rect_contains(&segment, x, y) &&
            !disabled(combined_option_state(control->state,
                                             control->options[i].state)))
            return i;
    }
    return -1;
}

bool fui_component_segmented_control(fui_painter_t *painter,
                                     const fui_segmented_control_t *control,
                                     const fui_component_style_t *style)
{
    uint8_t i;
    bool ok;
    if (painter == NULL || control == NULL || style == NULL ||
        control->options == NULL || control->option_count == 0U ||
        !valid_bounds(&control->bounds))
        return false;
    ok = fui_painter_rect(painter, control->bounds, style->radius, style->track);
    for (i = 0U; i < control->option_count; i++)
    {
        fui_rect_t segment = fui_component_segment_rect(control, i);
        fui_component_state_t state = combined_option_state(control->state,
                                                             control->options[i].state);
        uint8_t scale = control->text_scale == 0U ? 1U : control->text_scale;
        int16_t text_width = fui_component_text_width(control->options[i].label, scale);
        int16_t text_height = fui_component_text_height(control->options[i].label, scale);
        fui_color_t background = i == control->selected_index ? style->accent :
                                 surface_color(state, style);
        if (i == control->selected_index)
            state = (fui_component_state_t)(state | FUI_COMPONENT_STATE_SELECTED);
        ok = fui_painter_rect(painter, segment, style->radius, background) && ok;
        ok = fui_painter_text(painter,
            (int16_t)(segment.x + (segment.width - text_width) / 2),
            (int16_t)(segment.y + (segment.height - text_height) / 2),
            scale, foreground_color(state, style), control->options[i].label) && ok;
    }
    return ok;
}

static int16_t select_header_height(const fui_select_popup_t *popup)
{
    if (popup == NULL || popup->title == NULL || popup->title[0] == '\0') return 0;
    return popup->row_height > 0 ? popup->row_height : 40;
}

fui_rect_t fui_component_select_option_rect(const fui_select_popup_t *popup,
                                            uint8_t index)
{
    fui_rect_t empty = {0, 0, 0, 0};
    fui_rect_t rect;
    int16_t header;
    int16_t row_height;
    if (popup == NULL || !valid_bounds(&popup->bounds) ||
        popup->option_count == 0U || index >= popup->option_count)
        return empty;
    header = select_header_height(popup);
    row_height = popup->row_height > 0 ? popup->row_height :
        (int16_t)((popup->bounds.height - header) / popup->option_count);
    rect = (fui_rect_t){popup->bounds.x,
                        (int16_t)(popup->bounds.y + header + row_height * index),
                        popup->bounds.width, row_height};
    return rect;
}

int fui_component_select_index_from_point(const fui_select_popup_t *popup,
                                          int16_t x, int16_t y)
{
    uint8_t i;
    if (popup == NULL || !fui_rect_contains(&popup->bounds, x, y)) return -1;
    for (i = 0U; i < popup->option_count; i++)
    {
        fui_rect_t option = fui_component_select_option_rect(popup, i);
        if (fui_rect_contains(&option, x, y) &&
            !disabled(combined_option_state(popup->state,
                                             popup->options[i].state)))
            return i;
    }
    return -1;
}

bool fui_component_select_popup(fui_painter_t *painter,
                                const fui_select_popup_t *popup,
                                const fui_component_style_t *style)
{
    fui_radio_group_t group;
    int16_t header;
    uint8_t scale;
    bool ok;
    if (painter == NULL || popup == NULL || style == NULL ||
        popup->options == NULL || popup->option_count == 0U ||
        !valid_bounds(&popup->bounds))
        return false;
    header = select_header_height(popup);
    scale = popup->text_scale == 0U ? 1U : popup->text_scale;
    ok = fui_painter_rect(painter, popup->bounds, style->radius,
                          style->surface_alt);
    if (header > 0)
    {
        fui_rect_t title = {popup->bounds.x, popup->bounds.y,
                            popup->bounds.width, header};
        int16_t title_height = fui_component_text_height(popup->title, scale);
        ok = fui_painter_rect(painter, title, style->radius,
                              style->surface_selected) && ok;
        ok = fui_painter_text(painter,
            (int16_t)(title.x + maximum_i16(style->padding, 8)),
            (int16_t)(title.y + (title.height - title_height) / 2),
            scale, style->foreground, popup->title) && ok;
    }
    group.bounds = (fui_rect_t){popup->bounds.x,
                                (int16_t)(popup->bounds.y + header),
                                popup->bounds.width,
                                (int16_t)(popup->bounds.height - header)};
    group.options = popup->options;
    group.option_count = popup->option_count;
    group.selected_index = popup->selected_index;
    group.state = popup->state;
    group.text_scale = scale;
    group.vertical = true;
    return fui_component_radio_group(painter, &group, style) && ok;
}

static int16_t context_menu_header_height(const fui_context_menu_t *menu)
{
    if (menu == NULL || menu->title == NULL || menu->title[0] == '\0') return 0;
    return menu->header_height > 0 ? menu->header_height :
           (menu->row_height > 0 ? menu->row_height : 40);
}

fui_rect_t fui_component_context_menu_item_rect(const fui_context_menu_t *menu,
                                                uint8_t index)
{
    fui_rect_t empty = {0, 0, 0, 0};
    int16_t header;
    int16_t row_height;
    if (menu == NULL || !valid_bounds(&menu->bounds) || menu->items == NULL ||
        menu->item_count == 0U || index >= menu->item_count)
        return empty;
    header = context_menu_header_height(menu);
    row_height = menu->row_height > 0 ? menu->row_height :
        (int16_t)((menu->bounds.height - header) / menu->item_count);
    return (fui_rect_t){menu->bounds.x,
                        (int16_t)(menu->bounds.y + header + row_height * index),
                        menu->bounds.width, row_height};
}

int fui_component_context_menu_index_from_point(const fui_context_menu_t *menu,
                                                int16_t x, int16_t y)
{
    uint8_t i;
    if (menu == NULL || !fui_rect_contains(&menu->bounds, x, y)) return -1;
    for (i = 0U; i < menu->item_count; i++)
    {
        fui_rect_t item = fui_component_context_menu_item_rect(menu, i);
        if (fui_rect_contains(&item, x, y) &&
            !disabled(combined_option_state(menu->state, menu->items[i].state)))
            return i;
    }
    return -1;
}

bool fui_component_context_menu(fui_painter_t *painter,
                                const fui_context_menu_t *menu,
                                const fui_component_style_t *style)
{
    uint8_t i;
    uint8_t scale;
    int16_t header;
    bool ok;
    if (painter == NULL || menu == NULL || style == NULL || menu->items == NULL ||
        menu->item_count == 0U || !valid_bounds(&menu->bounds))
        return false;
    scale = menu->text_scale == 0U ? 1U : menu->text_scale;
    header = context_menu_header_height(menu);
    ok = fui_painter_rect(painter, menu->bounds, style->radius,
                          disabled(menu->state) ? style->surface_disabled :
                                                  style->surface_alt);
    if (header > 0)
    {
        fui_rect_t title = {menu->bounds.x, menu->bounds.y,
                            menu->bounds.width, header};
        int16_t text_height = fui_component_text_height(menu->title, scale);
        ok = fui_painter_rect(painter, title, style->radius,
                              style->surface_selected) && ok;
        ok = fui_painter_text(painter,
            (int16_t)(title.x + maximum_i16(style->padding, 8)),
            (int16_t)(title.y + (title.height - text_height) / 2),
            scale, style->foreground, menu->title) && ok;
    }
    for (i = 0U; i < menu->item_count; i++)
    {
        const fui_context_menu_item_t *entry = &menu->items[i];
        fui_component_state_t state = combined_option_state(menu->state,
                                                             entry->state);
        fui_rect_t row = fui_component_context_menu_item_rect(menu, i);
        fui_color_t color = disabled(state) ? style->foreground_disabled :
            (entry->variant == FUI_BUTTON_DANGER ? style->danger :
                                                   style->foreground);
        int16_t text_x = (int16_t)(row.x + maximum_i16(style->padding, 8));
        int16_t text_y = (int16_t)(row.y +
            (row.height - fui_component_text_height(entry->label, scale)) / 2);
        if ((state & FUI_COMPONENT_STATE_PRESSED) != 0U)
            ok = fui_painter_rect(painter, row, 0U,
                                  style->surface_pressed) && ok;
        if (entry->leading_draw != NULL && menu->leading_size > 0)
        {
            fui_rect_t leading = {
                text_x,
                (int16_t)(row.y + (row.height - menu->leading_size) / 2),
                menu->leading_size,
                menu->leading_size
            };
            entry->leading_draw(painter, leading, color, entry->leading_context);
            text_x = (int16_t)(leading.x + leading.width +
                               maximum_i16(style->padding / 2, 5));
        }
        ok = fui_painter_text(painter, text_x, text_y, scale, color,
                              entry->label) && ok;
    }
    return ok;
}

fui_rect_t fui_component_dialog_button_rect(const fui_dialog_t *dialog,
                                            bool primary)
{
    fui_rect_t empty = {0, 0, 0, 0};
    fui_rect_t button;
    int16_t inset;
    int16_t gap;
    if (dialog == NULL || !valid_bounds(&dialog->bounds)) return empty;
    inset = maximum_i16(dialog->content_inset, 0);
    gap = maximum_i16(dialog->button_gap, 0);
    button.height = dialog->button_height > 0 ? dialog->button_height : 40;
    if (button.height > dialog->bounds.height - inset * 2)
        button.height = maximum_i16((int16_t)(dialog->bounds.height - inset * 2), 1);
    button.y = (int16_t)(dialog->bounds.y + dialog->bounds.height - inset -
                         button.height);
    if (!dialog->show_secondary)
    {
        button.x = (int16_t)(dialog->bounds.x + inset);
        button.width = (int16_t)(dialog->bounds.width - inset * 2);
    }
    else
    {
        button.width = (int16_t)((dialog->bounds.width - inset * 2 - gap) / 2);
        button.x = (int16_t)(dialog->bounds.x + inset +
                             (primary ? button.width + gap : 0));
    }
    return button;
}

bool fui_component_dialog(fui_painter_t *painter,
                          const fui_dialog_t *dialog,
                          const fui_component_style_t *style)
{
    fui_rect_t primary;
    int16_t inset;
    int16_t y;
    uint8_t title_scale;
    uint8_t text_scale;
    bool ok;
    if (painter == NULL || dialog == NULL || style == NULL ||
        !valid_bounds(&dialog->bounds) || dialog->title == NULL ||
        dialog->primary_label == NULL)
        return false;
    inset = maximum_i16(dialog->content_inset, 0);
    title_scale = dialog->title_scale == 0U ? 1U : dialog->title_scale;
    text_scale = dialog->text_scale == 0U ? 1U : dialog->text_scale;
    ok = fui_painter_rect(painter, dialog->bounds, style->radius,
                          disabled(dialog->state) ? style->surface_disabled :
                                                   style->surface);
    y = (int16_t)(dialog->bounds.y + inset);
    ok = fui_painter_text(painter, (int16_t)(dialog->bounds.x + inset), y,
                          title_scale, foreground_color(dialog->state, style),
                          dialog->title) && ok;
    y = (int16_t)(y + fui_component_text_height(dialog->title, title_scale) + 12);
    if (dialog->detail != NULL && dialog->detail[0] != '\0')
    {
        ok = fui_painter_text(painter, (int16_t)(dialog->bounds.x + inset), y,
                              text_scale, style->foreground_muted,
                              dialog->detail) && ok;
        y = (int16_t)(y + fui_component_text_height(dialog->detail, text_scale) + 10);
    }
    if (dialog->target != NULL && dialog->target[0] != '\0')
    {
        ok = fui_painter_text(painter, (int16_t)(dialog->bounds.x + inset), y,
                              text_scale, style->foreground,
                              dialog->target) && ok;
        y = (int16_t)(y + fui_component_text_height(dialog->target, text_scale) + 10);
    }
    if (dialog->status != NULL && dialog->status[0] != '\0')
        ok = fui_painter_text(painter, (int16_t)(dialog->bounds.x + inset), y,
                              text_scale, dialog->status_danger ? style->danger :
                                                                 style->foreground_muted,
                              dialog->status) && ok;
    if (dialog->show_secondary && dialog->secondary_label != NULL)
    {
        fui_component_style_t secondary_style = *style;
        fui_button_t secondary = {
            .bounds = fui_component_dialog_button_rect(dialog, false),
            .label = dialog->secondary_label,
            .state = dialog->state,
            .variant = FUI_BUTTON_SECONDARY,
            .text_scale = dialog->button_scale
        };
        secondary_style.surface = style->surface_alt;
        ok = fui_component_button(painter, &secondary, &secondary_style) && ok;
    }
    primary = fui_component_dialog_button_rect(dialog, true);
    {
        fui_button_t button = {
            .bounds = primary,
            .label = dialog->primary_label,
            .state = dialog->state,
            .variant = dialog->destructive ? FUI_BUTTON_DANGER :
                                             FUI_BUTTON_PRIMARY,
            .text_scale = dialog->button_scale
        };
        ok = fui_component_button(painter, &button, style) && ok;
    }
    return ok;
}

bool fui_component_spinner(fui_painter_t *painter,
                           const fui_spinner_t *spinner,
                           const fui_component_style_t *style)
{
    static const int8_t offsets[8][4] = {
        {0, -10, 0, -5}, {7, -7, 4, -4}, {10, 0, 5, 0}, {7, 7, 4, 4},
        {0, 10, 0, 5}, {-7, 7, -4, 4}, {-10, 0, -5, 0}, {-7, -7, -4, -4}
    };
    int16_t cx;
    int16_t cy;
    int16_t radius;
    uint8_t i;
    bool ok = true;
    if (painter == NULL || spinner == NULL || style == NULL ||
        !valid_bounds(&spinner->bounds))
        return false;
    cx = (int16_t)(spinner->bounds.x + spinner->bounds.width / 2);
    cy = (int16_t)(spinner->bounds.y + spinner->bounds.height / 2);
    radius = minimum_i16(spinner->bounds.width, spinner->bounds.height) / 2;
    if (radius < 5) return false;
    for (i = 0U; i < 8U; i++)
    {
        uint8_t slot = (uint8_t)((i + 8U - (spinner->phase & 7U)) & 7U);
        fui_color_t color = slot < 2U ? style->accent :
                            (disabled(spinner->state) ?
                             style->foreground_disabled : style->foreground_muted);
        ok = fui_painter_line(painter,
            (int16_t)(cx + offsets[i][0] * radius / 10),
            (int16_t)(cy + offsets[i][1] * radius / 10),
            (int16_t)(cx + offsets[i][2] * radius / 10),
            (int16_t)(cy + offsets[i][3] * radius / 10),
            2U, color) && ok;
    }
    return ok;
}

fui_rect_t fui_component_toast_action_rect(const fui_toast_t *toast,
                                           const fui_component_style_t *style)
{
    fui_rect_t empty = {0, 0, 0, 0};
    fui_rect_t action;
    uint8_t scale;
    int16_t padding;
    if (toast == NULL || style == NULL || !valid_bounds(&toast->bounds) ||
        toast->action_label == NULL || toast->action_label[0] == '\0')
        return empty;
    scale = toast->text_scale == 0U ? 1U : toast->text_scale;
    padding = maximum_i16(style->padding, 8);
    action.width = (int16_t)(fui_component_text_width(toast->action_label, scale) +
                             padding * 2);
    if (action.width > toast->bounds.width / 2)
        action.width = (int16_t)(toast->bounds.width / 2);
    action.x = (int16_t)(toast->bounds.x + toast->bounds.width - action.width);
    action.y = toast->bounds.y;
    action.height = toast->bounds.height;
    return action;
}

bool fui_component_toast(fui_painter_t *painter,
                         const fui_toast_t *toast,
                         const fui_component_style_t *style)
{
    fui_rect_t action;
    uint8_t scale;
    int16_t padding;
    int16_t text_y;
    bool ok;
    if (painter == NULL || toast == NULL || style == NULL ||
        !valid_bounds(&toast->bounds) || toast->message == NULL)
        return false;
    scale = toast->text_scale == 0U ? 1U : toast->text_scale;
    padding = maximum_i16(style->padding, 8);
    text_y = (int16_t)(toast->bounds.y +
        (toast->bounds.height - fui_component_text_height(toast->message, scale)) / 2);
    ok = fui_painter_rect(painter, toast->bounds, style->radius,
                          disabled(toast->state) ? style->surface_disabled :
                                                  style->surface_alt);
    ok = fui_painter_text(painter, (int16_t)(toast->bounds.x + padding), text_y,
                          scale, foreground_color(toast->state, style),
                          toast->message) && ok;
    action = fui_component_toast_action_rect(toast, style);
    if (action.width > 0 && action.height > 0)
    {
        int16_t action_width = fui_component_text_width(toast->action_label, scale);
        ok = fui_painter_text(painter,
            (int16_t)(action.x + (action.width - action_width) / 2), text_y,
            scale, disabled(toast->state) ? style->foreground_disabled :
                                            style->accent,
            toast->action_label) && ok;
    }
    return ok;
}
