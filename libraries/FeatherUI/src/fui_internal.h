#ifndef FUI_INTERNAL_H
#define FUI_INTERNAL_H

#include <rtthread.h>
#include "feather_ui.h"
#include "vg_lite.h"

typedef enum
{
    FUI_CMD_CLEAR = 0,
    FUI_CMD_RECT,
    FUI_CMD_LINE,
    FUI_CMD_LINE_BATCH,
    FUI_CMD_GLYPH,
    FUI_CMD_TEXT_RUN,
    FUI_CMD_IMAGE_RGB565
} fui_command_type_t;

typedef struct
{
    fui_command_type_t type;
    fui_rect_t clip;
    fui_color_t color;
    union
    {
        struct
        {
            fui_rect_t rect;
            uint16_t radius;
        } rect;
        struct
        {
            int16_t x1;
            int16_t y1;
            int16_t x2;
            int16_t y2;
            uint16_t width;
        } line;
        struct
        {
            const int16_t (*points)[4];
            uint16_t width;
            uint8_t count;
        } line_batch;
        struct
        {
            int16_t x;
            int16_t y;
            uint8_t scale;
            uint32_t glyph;
        } glyph;
        struct
        {
            int16_t x;
            int16_t y;
            uint8_t scale;
            uint8_t cache_id;
        } text_run;
        struct
        {
            fui_image_rgb565_t image;
            fui_rect_t source;
            int16_t x;
            int16_t y;
        } image;
    } data;
} fui_command_t;

typedef struct
{
    fui_command_t commands[FUI_DISPLAY_LIST_CAPACITY];
    int16_t line_segments[FUI_LINE_SEGMENT_CAPACITY][4];
    uint16_t count;
    uint16_t line_segment_count;
    uint16_t overflow;
} fui_display_list_t;

struct fui_painter
{
    fui_display_list_t *list;
    fui_rect_t clip;
    fui_rect_t screen;
};

typedef struct
{
    vg_lite_buffer_t atlas;
    uint8_t *pixels;
    uint16_t width;
    uint16_t height;
    uint8_t cell_width;
    uint8_t cell_height;
    bool ready;
} fui_font_atlas_t;

typedef struct
{
    const vg_lite_buffer_t *atlas;
    uint16_t source_x;
    uint16_t source_y;
    uint8_t width;
    uint8_t height;
    uint8_t advance;
} fui_cjk_glyph_t;

typedef struct
{
    const vg_lite_buffer_t *atlas;
    uint16_t source_x;
    uint16_t source_y;
    uint8_t width;
    uint8_t height;
    uint8_t advance;
} fui_font_glyph_t;

typedef struct
{
    uint32_t encode_cycles;
    uint32_t clear_encode_cycles;
    uint32_t path_encode_cycles;
    uint32_t blit_encode_cycles;
    uint16_t clear_calls;
    uint16_t path_calls;
    uint16_t blit_calls;
    uint16_t path_primitives;
    uint16_t path_batch_peak;
} fui_renderer_frame_stats_t;

void fui_display_list_reset(fui_display_list_t *list);
bool fui_display_list_push(fui_display_list_t *list,
                           const fui_command_t *command);
void fui_painter_init(fui_painter_t *painter, fui_display_list_t *list,
                      uint16_t width, uint16_t height);

int fui_renderer_init(void);
int fui_renderer_render(const fui_display_list_t *list, void *framebuffer,
                        uint16_t width, uint16_t height,
                        uint16_t stride_pixels);
void fui_renderer_get_frame_stats(fui_renderer_frame_stats_t *stats);
const fui_font_atlas_t *fui_font_atlas_get(void);
bool fui_cjk_glyph_get(uint32_t codepoint, fui_cjk_glyph_t *glyph);
bool fui_font_glyph_get(uint32_t codepoint, uint8_t text_scale,
                        fui_font_glyph_t *glyph);
uint8_t fui_font_line_height(uint8_t text_scale);
uint16_t fui_font_scale_dimension(uint16_t base, uint8_t text_scale);
void fui_text_cache_init(void);
void fui_text_cache_begin_frame(void);
bool fui_text_cache_acquire(const char *text, uint8_t requested_scale,
                            uint8_t *cache_id, uint16_t *width,
                            uint8_t *render_scale);
const vg_lite_buffer_t *fui_text_cache_get(uint8_t cache_id);

uint32_t fui_clock_cycles(void);
uint32_t fui_cycles_to_us(uint32_t cycles);

extern volatile uint32_t g_fui_gpu_submit_count;
extern volatile uint32_t g_fui_gpu_submit_bytes;
extern volatile uint32_t g_fui_gpu_completed_jobs;
extern volatile uint32_t g_fui_gpu_busy_cycles;
extern volatile uint32_t g_fui_gpu_busy_last_cycles;

#endif /* FUI_INTERNAL_H */
