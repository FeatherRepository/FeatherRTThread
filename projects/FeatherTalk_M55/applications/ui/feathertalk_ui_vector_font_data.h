#ifndef FEATHERTALK_UI_VECTOR_FONT_DATA_H
#define FEATHERTALK_UI_VECTOR_FONT_DATA_H

#include <stdint.h>

typedef struct
{
    uint32_t codepoint;
    uint32_t path_offset;
    uint16_t path_word_count;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    uint16_t advance;
} ft_vector_font_glyph_asset_t;

extern const uint16_t ft_vector_font_units_per_em;
extern const int16_t ft_vector_font_typo_ascender;
extern const int16_t ft_vector_font_typo_descender;
extern const uint32_t ft_vector_font_glyph_count;
extern const ft_vector_font_glyph_asset_t ft_vector_font_glyphs[];
extern const int16_t ft_vector_font_path_data[];

#endif
