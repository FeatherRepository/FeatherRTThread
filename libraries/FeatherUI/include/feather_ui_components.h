#ifndef FEATHER_UI_COMPONENTS_H
#define FEATHER_UI_COMPONENTS_H

#include "feather_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t fui_component_state_t;

enum
{
    FUI_COMPONENT_STATE_DEFAULT  = 0U,
    FUI_COMPONENT_STATE_DISABLED = 1U << 0,
    FUI_COMPONENT_STATE_PRESSED  = 1U << 1,
    FUI_COMPONENT_STATE_SELECTED = 1U << 2,
    FUI_COMPONENT_STATE_FOCUSED  = 1U << 3
};

typedef enum
{
    FUI_BUTTON_SECONDARY = 0,
    FUI_BUTTON_PRIMARY,
    FUI_BUTTON_DANGER,
    FUI_BUTTON_GHOST
} fui_button_variant_t;

typedef struct
{
    fui_color_t surface;
    fui_color_t surface_alt;
    fui_color_t surface_pressed;
    fui_color_t surface_selected;
    fui_color_t surface_disabled;
    fui_color_t foreground;
    fui_color_t foreground_muted;
    fui_color_t foreground_disabled;
    fui_color_t accent;
    fui_color_t danger;
    fui_color_t track;
    fui_color_t knob;
    fui_color_t outline;
    int16_t padding;
    uint16_t radius;
} fui_component_style_t;

typedef void (*fui_component_leading_draw_cb_t)(fui_painter_t *painter,
                                                 fui_rect_t bounds,
                                                 fui_color_t color,
                                                 void *context);

typedef struct
{
    fui_rect_t bounds;
    const char *title;
    const char *detail;
    fui_component_state_t state;
    uint8_t title_scale;
    uint8_t detail_scale;
    bool show_chevron;
    int16_t leading_size;
    fui_component_leading_draw_cb_t leading_draw;
    void *leading_context;
} fui_list_row_t;

typedef struct
{
    fui_rect_t bounds;
    const char *label;
    fui_component_state_t state;
    fui_button_variant_t variant;
    uint8_t text_scale;
} fui_button_t;

typedef struct
{
    fui_rect_t bounds;
    fui_component_state_t state;
    bool checked;
} fui_switch_t;

typedef struct
{
    fui_rect_t bounds;
    fui_component_state_t state;
    int32_t minimum;
    int32_t maximum;
    int32_t value;
} fui_slider_t;

typedef struct
{
    fui_rect_t bounds;
    fui_component_state_t state;
    int32_t minimum;
    int32_t maximum;
    int32_t value;
} fui_progress_t;

typedef struct
{
    fui_rect_t bounds;
    const char *text;
    const char *placeholder;
    fui_component_state_t state;
    uint8_t text_scale;
} fui_text_field_t;

typedef struct
{
    fui_rect_t bounds;
    int32_t viewport_extent;
    int32_t content_extent;
    int32_t offset;
    int16_t minimum_thumb;
    fui_component_state_t state;
} fui_scrollbar_t;

typedef struct
{
    const char *label;
    fui_component_state_t state;
} fui_option_t;

typedef struct
{
    fui_rect_t bounds;
    const fui_option_t *options;
    uint8_t option_count;
    uint8_t selected_index;
    fui_component_state_t state;
    uint8_t text_scale;
    bool vertical;
} fui_radio_group_t;

typedef struct
{
    fui_rect_t bounds;
    const fui_option_t *options;
    uint8_t option_count;
    uint8_t selected_index;
    fui_component_state_t state;
    uint8_t text_scale;
} fui_segmented_control_t;

typedef struct
{
    fui_rect_t bounds;
    const char *title;
    const fui_option_t *options;
    uint8_t option_count;
    uint8_t selected_index;
    fui_component_state_t state;
    uint8_t text_scale;
    int16_t row_height;
} fui_select_popup_t;

typedef struct
{
    const char *label;
    fui_component_state_t state;
    fui_button_variant_t variant;
    fui_component_leading_draw_cb_t leading_draw;
    void *leading_context;
} fui_context_menu_item_t;

typedef struct
{
    fui_rect_t bounds;
    const char *title;
    const fui_context_menu_item_t *items;
    uint8_t item_count;
    fui_component_state_t state;
    uint8_t text_scale;
    int16_t header_height;
    int16_t row_height;
    int16_t leading_size;
} fui_context_menu_t;

typedef struct
{
    fui_rect_t bounds;
    const char *title;
    const char *detail;
    const char *target;
    const char *status;
    const char *primary_label;
    const char *secondary_label;
    fui_component_state_t state;
    uint8_t title_scale;
    uint8_t text_scale;
    uint8_t button_scale;
    int16_t content_inset;
    int16_t button_height;
    int16_t button_gap;
    bool show_secondary;
    bool destructive;
    bool status_danger;
} fui_dialog_t;

typedef struct
{
    fui_rect_t bounds;
    fui_component_state_t state;
    uint8_t phase;
} fui_spinner_t;

typedef struct
{
    fui_rect_t bounds;
    const char *message;
    const char *action_label;
    fui_component_state_t state;
    uint8_t text_scale;
} fui_toast_t;

int16_t fui_component_text_width(const char *text, uint8_t scale);
int16_t fui_component_text_height(const char *text, uint8_t scale);

bool fui_component_list_row(fui_painter_t *painter,
                            const fui_list_row_t *row,
                            const fui_component_style_t *style);
bool fui_component_button(fui_painter_t *painter,
                          const fui_button_t *button,
                          const fui_component_style_t *style);
bool fui_component_switch(fui_painter_t *painter,
                          const fui_switch_t *control,
                          const fui_component_style_t *style);
bool fui_component_slider(fui_painter_t *painter,
                          const fui_slider_t *slider,
                          const fui_component_style_t *style);
bool fui_component_progress(fui_painter_t *painter,
                            const fui_progress_t *progress,
                            const fui_component_style_t *style);
bool fui_component_text_field(fui_painter_t *painter,
                              const fui_text_field_t *field,
                              const fui_component_style_t *style);
bool fui_component_scrollbar(fui_painter_t *painter,
                             const fui_scrollbar_t *scrollbar,
                             const fui_component_style_t *style);
bool fui_component_radio_group(fui_painter_t *painter,
                               const fui_radio_group_t *group,
                               const fui_component_style_t *style);
bool fui_component_segmented_control(fui_painter_t *painter,
                                     const fui_segmented_control_t *control,
                                     const fui_component_style_t *style);
bool fui_component_select_popup(fui_painter_t *painter,
                                const fui_select_popup_t *popup,
                                const fui_component_style_t *style);
bool fui_component_context_menu(fui_painter_t *painter,
                                const fui_context_menu_t *menu,
                                const fui_component_style_t *style);
bool fui_component_dialog(fui_painter_t *painter,
                          const fui_dialog_t *dialog,
                          const fui_component_style_t *style);
bool fui_component_spinner(fui_painter_t *painter,
                           const fui_spinner_t *spinner,
                           const fui_component_style_t *style);
bool fui_component_toast(fui_painter_t *painter,
                         const fui_toast_t *toast,
                         const fui_component_style_t *style);

fui_rect_t fui_component_slider_track_rect(const fui_slider_t *slider);
int32_t fui_component_slider_value_from_x(const fui_slider_t *slider,
                                          int16_t x);
fui_rect_t fui_component_scrollbar_thumb_rect(const fui_scrollbar_t *scrollbar);
fui_rect_t fui_component_radio_option_rect(const fui_radio_group_t *group,
                                           uint8_t index);
int fui_component_radio_index_from_point(const fui_radio_group_t *group,
                                         int16_t x, int16_t y);
fui_rect_t fui_component_segment_rect(const fui_segmented_control_t *control,
                                      uint8_t index);
int fui_component_segment_index_from_point(const fui_segmented_control_t *control,
                                           int16_t x, int16_t y);
fui_rect_t fui_component_select_option_rect(const fui_select_popup_t *popup,
                                            uint8_t index);
int fui_component_select_index_from_point(const fui_select_popup_t *popup,
                                          int16_t x, int16_t y);
fui_rect_t fui_component_context_menu_item_rect(const fui_context_menu_t *menu,
                                                uint8_t index);
int fui_component_context_menu_index_from_point(const fui_context_menu_t *menu,
                                                int16_t x, int16_t y);
fui_rect_t fui_component_dialog_button_rect(const fui_dialog_t *dialog,
                                            bool primary);
fui_rect_t fui_component_toast_action_rect(const fui_toast_t *toast,
                                           const fui_component_style_t *style);

#ifdef __cplusplus
}
#endif

#endif /* FEATHER_UI_COMPONENTS_H */
