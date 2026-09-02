#include <string.h>
#include <board.h>
#include "fui_internal.h"

#define FUI_TEXT_CACHE_ENTRIES   24U
#define FUI_TEXT_CACHE_MAX_CHARS 42U
#define FUI_TEXT_CACHE_STRIDE    256U
#define FUI_TEXT_CACHE_HEIGHT    18U
#define FUI_TEXT_CACHE_SLOT_SIZE (FUI_TEXT_CACHE_STRIDE * FUI_TEXT_CACHE_HEIGHT)

typedef struct
{
    vg_lite_buffer_t image;
    char text[FUI_TEXT_CACHE_MAX_CHARS + 1U];
    uint16_t width;
    uint8_t render_scale;
    uint8_t requested_scale;
    bool unicode;
    uint32_t last_used;
    uint32_t frame_tag;
    bool valid;
} fui_text_cache_entry_t;

CY_SECTION(".cy_gpu_buf") CY_ALIGN(32)
static uint8_t s_text_cache_pixels[FUI_TEXT_CACHE_ENTRIES][FUI_TEXT_CACHE_SLOT_SIZE];
static fui_text_cache_entry_t s_entries[FUI_TEXT_CACHE_ENTRIES];
static uint32_t s_frame_tag;
static uint32_t s_use_counter;

void fui_text_cache_init(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    memset(s_text_cache_pixels, 0, sizeof(s_text_cache_pixels));
    s_frame_tag = 1U;
    s_use_counter = 0U;
}

void fui_text_cache_begin_frame(void)
{
    s_frame_tag++;
    if (s_frame_tag == 0U)
    {
        uint32_t i;
        for (i = 0U; i < FUI_TEXT_CACHE_ENTRIES; i++)
            s_entries[i].frame_tag = 0U;
        s_frame_tag = 1U;
    }
}

static bool make_key(const char *text, char *key, uint16_t *length,
                     bool *unicode)
{
    uint16_t count = 0U;
    bool has_ink = false;
    if (text == RT_NULL) return false;
    while (*text != '\0')
    {
        uint8_t glyph;
        if (count >= FUI_TEXT_CACHE_MAX_CHARS) return false;
        glyph = (uint8_t)*text++;
        if (glyph < 32U) return false;
        if (glyph > 126U) *unicode = true;
        if (glyph != (uint8_t)' ') has_ink = true;
        key[count++] = (char)glyph;
    }
    if (count == 0U || !has_ink) return false;
    key[count] = '\0';
    *length = count;
    return true;
}

static uint32_t utf8_next_cache(const char **cursor)
{
    const uint8_t *text = (const uint8_t *)*cursor;
    uint32_t codepoint;
    if (text[0] < 0x80U) { *cursor += 1; return text[0]; }
    if ((text[0] & 0xe0U) == 0xc0U && (text[1] & 0xc0U) == 0x80U)
    {
        codepoint = ((uint32_t)(text[0] & 0x1fU) << 6) | (text[1] & 0x3fU);
        *cursor += 2;
        return codepoint;
    }
    if ((text[0] & 0xf0U) == 0xe0U && (text[1] & 0xc0U) == 0x80U &&
        (text[2] & 0xc0U) == 0x80U)
    {
        codepoint = ((uint32_t)(text[0] & 0x0fU) << 12) |
                    ((uint32_t)(text[1] & 0x3fU) << 6) | (text[2] & 0x3fU);
        *cursor += 3;
        return codepoint;
    }
    *cursor += 1;
    return (uint32_t)'?';
}

static void coverage_copy_scaled(uint8_t *destination, uint16_t destination_x,
                                 const uint8_t *source, uint16_t source_stride,
                                 uint16_t source_x, uint16_t source_y,
                                 uint8_t width, uint8_t height, uint8_t scale)
{
    uint8_t row, column, sy, sx;
    for (row = 0U; row < height; row++)
        for (column = 0U; column < width; column++)
        {
            uint8_t coverage = source[(source_y + row) * source_stride +
                                      source_x + column];
            for (sy = 0U; sy < scale; sy++)
                for (sx = 0U; sx < scale; sx++)
                {
                    uint16_t dx = (uint16_t)(destination_x + column * scale + sx);
                    uint16_t dy = (uint16_t)(row * scale + sy);
                    if (dx < FUI_TEXT_CACHE_STRIDE && dy < FUI_TEXT_CACHE_HEIGHT &&
                        coverage > destination[dy * FUI_TEXT_CACHE_STRIDE + dx])
                        destination[dy * FUI_TEXT_CACHE_STRIDE + dx] = coverage;
                }
        }
}

static bool rasterize_entry(fui_text_cache_entry_t *entry, uint8_t entry_id,
                            const char *key, uint16_t length,
                            uint8_t requested_scale, bool unicode)
{
    uint8_t *pixels = s_text_cache_pixels[entry_id];
    uint16_t cursor_x = 0U;
    uint8_t line_height = fui_font_line_height(2U);
    const char *cursor = key;
    memset(pixels, 0, FUI_TEXT_CACHE_SLOT_SIZE);
    while (*cursor != '\0')
    {
        uint32_t glyph = utf8_next_cache(&cursor);
        fui_font_glyph_t product;
        if (!fui_font_glyph_get(glyph, 2U, &product)) return false;
        if (glyph != (uint32_t)' ')
            coverage_copy_scaled(pixels, cursor_x,
                (const uint8_t *)product.atlas->memory,
                (uint16_t)product.atlas->stride,
                product.source_x, product.source_y,
                product.width, product.height, 1U);
        if ((uint32_t)cursor_x + product.advance > FUI_TEXT_CACHE_STRIDE)
            return false;
        cursor_x = (uint16_t)(cursor_x + product.advance);
    }

    memset(&entry->image, 0, sizeof(entry->image));
    entry->width = cursor_x;
    if (entry->width > FUI_TEXT_CACHE_STRIDE)
        entry->width = FUI_TEXT_CACHE_STRIDE;
    entry->image.width = entry->width;
    entry->image.height = line_height;
    entry->image.stride = FUI_TEXT_CACHE_STRIDE;
    entry->image.format = VG_LITE_A8;
    entry->image.tiled = VG_LITE_LINEAR;
    entry->image.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
    entry->image.transparency_mode = VG_LITE_IMAGE_TRANSPARENT;
    entry->image.memory = pixels;
    entry->image.address = (uint32_t)(uintptr_t)pixels;
    memcpy(entry->text, key, length + 1U);
    entry->unicode = unicode;
    entry->requested_scale = requested_scale;
    entry->render_scale = requested_scale;
    entry->valid = true;
    return true;
}

bool fui_text_cache_acquire(const char *text, uint8_t requested_scale,
                            uint8_t *cache_id, uint16_t *width,
                            uint8_t *render_scale)
{
    char key[FUI_TEXT_CACHE_MAX_CHARS + 1U];
    uint16_t length;
    uint32_t i;
    uint32_t candidate;
    uint32_t free_candidate = FUI_TEXT_CACHE_ENTRIES;
    uint32_t lru_candidate = FUI_TEXT_CACHE_ENTRIES;
    uint32_t oldest = UINT32_MAX;
    bool unicode = false;
    if (cache_id == RT_NULL || width == RT_NULL || render_scale == RT_NULL ||
        !make_key(text, key, &length, &unicode) ||
        (unicode && requested_scale > 2U)) return false;

    for (i = 0U; i < FUI_TEXT_CACHE_ENTRIES; i++)
    {
        if (s_entries[i].valid && strcmp(s_entries[i].text, key) == 0)
        {
            candidate = i;
            break;
        }
        if (!s_entries[i].valid)
        {
            if (free_candidate == FUI_TEXT_CACHE_ENTRIES) free_candidate = i;
        }
        else if (s_entries[i].frame_tag != s_frame_tag &&
                 s_entries[i].last_used < oldest)
        {
            lru_candidate = i;
            oldest = s_entries[i].last_used;
        }
    }
    if (i < FUI_TEXT_CACHE_ENTRIES)
        candidate = i;
    else
        candidate = free_candidate != FUI_TEXT_CACHE_ENTRIES ?
                    free_candidate : lru_candidate;
    if (candidate == FUI_TEXT_CACHE_ENTRIES) return false;
    if (!s_entries[candidate].valid || strcmp(s_entries[candidate].text, key) != 0 ||
        s_entries[candidate].unicode != unicode)
        if (!rasterize_entry(&s_entries[candidate], (uint8_t)candidate, key,
                             length, requested_scale, unicode)) return false;
    s_entries[candidate].last_used = ++s_use_counter;
    s_entries[candidate].frame_tag = s_frame_tag;
    *cache_id = (uint8_t)candidate;
    *width = s_entries[candidate].width;
    *render_scale = requested_scale;
    return true;
}

const vg_lite_buffer_t *fui_text_cache_get(uint8_t cache_id)
{
    if (cache_id >= FUI_TEXT_CACHE_ENTRIES || !s_entries[cache_id].valid)
        return RT_NULL;
    return &s_entries[cache_id].image;
}
