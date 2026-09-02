/**
 * @file lv_font.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_font.h"
#include "../misc/lv_text_private.h"
#include "../misc/lv_utils.h"
#include "../misc/lv_log.h"
#include "../misc/lv_assert.h"
#include "../stdlib/lv_string.h"
#include "lv_font_fmt_txt.h"
#include "lv_gpu_batch.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

#ifdef FEATHERTALK_USING_LVGL_GPU_BATCH
#define LV_FONT_DSC_CACHE_SLOTS 1024U
#define LV_FONT_DSC_CACHE_PROBES 8U
typedef struct
{
    const lv_font_t *font;
    uint32_t letter;
    uint32_t letter_next;
    lv_font_glyph_dsc_t dsc;
    bool found;
    bool valid;
} lv_font_dsc_cache_entry_t;

static lv_font_dsc_cache_entry_t s_font_dsc_cache[LV_FONT_DSC_CACHE_SLOTS];

static bool font_chain_has_stable_descriptors(const lv_font_t *font)
{
    while(font != NULL) {
        if(font->get_glyph_dsc != lv_font_get_glyph_dsc_fmt_txt &&
           !font->glyph_dsc_cacheable) return false;
        font = font->fallback;
    }
    return true;
}

static uint32_t font_dsc_cache_slot(const lv_font_t *font, uint32_t letter, uint32_t letter_next)
{
    uintptr_t key = (uintptr_t)font;
    return (uint32_t)(((key >> 4U) ^ (key >> 13U) ^
                       ((uintptr_t)letter * 2654435761UL) ^
                       ((uintptr_t)letter_next * 2246822519UL)) &
                      (LV_FONT_DSC_CACHE_SLOTS - 1U));
}
#endif

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const void * lv_font_get_glyph_bitmap(lv_font_glyph_dsc_t * g_dsc, lv_draw_buf_t * draw_buf)
{
    const lv_font_t * font_p = g_dsc->resolved_font;
    LV_ASSERT_NULL(font_p);
    return font_p->get_glyph_bitmap(g_dsc, draw_buf);
}

void lv_font_glyph_release_draw_data(lv_font_glyph_dsc_t * g_dsc)
{
    const lv_font_t * font = g_dsc->resolved_font;

    if(font != NULL && font->release_glyph) {
        font->release_glyph(font, g_dsc);
    }
}

bool lv_font_get_glyph_dsc(const lv_font_t * font_p, lv_font_glyph_dsc_t * dsc_out, uint32_t letter,
                           uint32_t letter_next)
{

    LV_ASSERT_NULL(font_p);
    LV_ASSERT_NULL(dsc_out);

#ifdef FEATHERTALK_USING_LVGL_GPU_BATCH
    bool cacheable = font_chain_has_stable_descriptors(font_p);
    uint32_t cache_slot = 0U;
    uint32_t cache_probe;
    lv_font_dsc_cache_entry_t *cache_entry = NULL;
    if(cacheable) {
        cache_slot = font_dsc_cache_slot(font_p, letter, letter_next);
        for(cache_probe = 0U; cache_probe < LV_FONT_DSC_CACHE_PROBES; cache_probe++) {
            lv_font_dsc_cache_entry_t *candidate =
                &s_font_dsc_cache[(cache_slot + cache_probe) & (LV_FONT_DSC_CACHE_SLOTS - 1U)];
            if(candidate->valid && candidate->font == font_p &&
               candidate->letter == letter && candidate->letter_next == letter_next) {
                *dsc_out = candidate->dsc;
                lv_gpu_batch_note_font_descriptor_cache(true);
                return candidate->found;
            }
            if(!candidate->valid) {
                cache_entry = candidate;
                break;
            }
        }
        if(cache_entry == NULL) cache_entry = &s_font_dsc_cache[cache_slot];
        lv_gpu_batch_note_font_descriptor_cache(false);
    }
#endif

#if LV_USE_FONT_PLACEHOLDER
    const lv_font_t * placeholder_font = NULL;
#endif

    const lv_font_t * f = font_p;

    dsc_out->resolved_font = NULL;

    while(f) {
        bool found = f->get_glyph_dsc(f, dsc_out, letter, f->kerning == LV_FONT_KERNING_NONE ? 0 : letter_next);
        if(found) {
            if(!dsc_out->is_placeholder) {
                dsc_out->resolved_font = f;
#ifdef FEATHERTALK_USING_LVGL_GPU_BATCH
                if(cacheable) {
                    cache_entry->font = font_p;
                    cache_entry->letter = letter;
                    cache_entry->letter_next = letter_next;
                    cache_entry->dsc = *dsc_out;
                    cache_entry->found = true;
                    cache_entry->valid = true;
                }
#endif
                return true;
            }
#if LV_USE_FONT_PLACEHOLDER
            else if(placeholder_font == NULL) {
                placeholder_font = f;
            }
#endif
        }
        f = f->fallback;
    }

#if LV_USE_FONT_PLACEHOLDER
    if(placeholder_font != NULL) {
        placeholder_font->get_glyph_dsc(placeholder_font, dsc_out, letter,
                                        placeholder_font->kerning == LV_FONT_KERNING_NONE ? 0 : letter_next);
        dsc_out->resolved_font = placeholder_font;
#ifdef FEATHERTALK_USING_LVGL_GPU_BATCH
        if(cacheable) {
            cache_entry->font = font_p;
            cache_entry->letter = letter;
            cache_entry->letter_next = letter_next;
            cache_entry->dsc = *dsc_out;
            cache_entry->found = true;
            cache_entry->valid = true;
        }
#endif
        return true;
    }
#endif

#if LV_USE_FONT_PLACEHOLDER
    dsc_out->box_w = font_p->line_height / 2;
    dsc_out->adv_w = dsc_out->box_w + 2;
#else
    dsc_out->box_w = 0;
    dsc_out->adv_w = 0;
#endif

    dsc_out->resolved_font = NULL;
    dsc_out->box_h = font_p->line_height;
    dsc_out->ofs_x = 0;
    dsc_out->ofs_y = 0;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A1;
    dsc_out->is_placeholder = true;

#ifdef FEATHERTALK_USING_LVGL_GPU_BATCH
    if(cacheable) {
        cache_entry->font = font_p;
        cache_entry->letter = letter;
        cache_entry->letter_next = letter_next;
        cache_entry->dsc = *dsc_out;
        cache_entry->found = false;
        cache_entry->valid = true;
    }
#endif

    return false;
}

uint16_t lv_font_get_glyph_width(const lv_font_t * font, uint32_t letter, uint32_t letter_next)
{
    LV_ASSERT_NULL(font);
    lv_font_glyph_dsc_t g;

    /*Return zero if letter is marker*/
    if(lv_text_is_marker(letter)) return 0;

    lv_font_get_glyph_dsc(font, &g, letter, letter_next);
    return g.adv_w;
}

void lv_font_set_kerning(lv_font_t * font, lv_font_kerning_t kerning)
{
    LV_ASSERT_NULL(font);
    font->kerning = kerning;
}

int32_t lv_font_get_line_height(const lv_font_t * font)
{
    return font->line_height;
}

const lv_font_t * lv_font_default(void)
{
    return LV_FONT_DEFAULT;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
