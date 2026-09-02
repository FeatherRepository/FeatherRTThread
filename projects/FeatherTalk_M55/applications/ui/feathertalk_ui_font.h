#ifndef FEATHERTALK_UI_FONT_H
#define FEATHERTALK_UI_FONT_H

#include "lvgl.h"

/* One canonical outline table backs every requested pixel size.  The returned
 * font object is persistent for the life of the UI, so LVGL may cache it. */
const lv_font_t *ft_vector_font_get(uint16_t pixel_size);

/* Validate pixel-bound rounding for every generated glyph at every supported
 * size.  This catches baseline drift and right/bottom clipping globally. */
bool ft_vector_font_metrics_self_test(void);

#endif /* FEATHERTALK_UI_FONT_H */
