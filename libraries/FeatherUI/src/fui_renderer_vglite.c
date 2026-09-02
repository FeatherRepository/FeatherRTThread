#include <string.h>
#include <board.h>
#include "fui_internal.h"

#define FUI_PATH_WORDS 512U
#define FUI_ROUND_MAGIC 0.55191502449351f

typedef struct
{
    uint32_t words[FUI_PATH_WORDS];
    uint16_t count;
} fui_path_data_t;

static fui_path_data_t s_path_batch;
static fui_renderer_frame_stats_t s_frame_stats;
static volatile bool s_defer_vglite_cache_maintenance;

int vg_lite_defer_cache_maintenance_hook(void)
{
    return s_defer_vglite_cache_maintenance ? 1 : 0;
}

static void path_word(fui_path_data_t *data, uint32_t value)
{
    if (data->count < FUI_PATH_WORDS) data->words[data->count++] = value;
}

static void path_float(fui_path_data_t *data, float value)
{
    uint32_t word;
    memcpy(&word, &value, sizeof(word));
    path_word(data, word);
}

static void path_point(fui_path_data_t *data, float x, float y)
{
    path_float(data, x);
    path_float(data, y);
}

static void path_move(fui_path_data_t *data, float x, float y)
{
    path_word(data, VLC_OP_MOVE);
    path_point(data, x, y);
}

static void path_line(fui_path_data_t *data, float x, float y)
{
    path_word(data, VLC_OP_LINE);
    path_point(data, x, y);
}

static void path_cubic(fui_path_data_t *data, float x1, float y1,
                       float x2, float y2, float x3, float y3)
{
    path_word(data, VLC_OP_CUBIC);
    path_point(data, x1, y1);
    path_point(data, x2, y2);
    path_point(data, x3, y3);
}

static void path_end(fui_path_data_t *data)
{
    path_word(data, VLC_OP_END);
}

static void path_rounded_rect(fui_path_data_t *data, const fui_rect_t *rect,
                              uint16_t radius)
{
    float x = rect->x;
    float y = rect->y;
    float w = rect->width;
    float h = rect->height;
    float r = radius;
    float maximum = (w < h ? w : h) * 0.5f;
    float offset;
    if (r > maximum) r = maximum;
    if (r <= 0.0f)
    {
        path_move(data, x, y);
        path_line(data, x + w, y);
        path_line(data, x + w, y + h);
        path_line(data, x, y + h);
        return;
    }
    offset = r * FUI_ROUND_MAGIC;
    path_move(data, x + r, y);
    path_line(data, x + w - r, y);
    path_cubic(data, x + w - r + offset, y,
               x + w, y + r - offset, x + w, y + r);
    path_line(data, x + w, y + h - r);
    path_cubic(data, x + w, y + h - r + offset,
               x + w - r + offset, y + h, x + w - r, y + h);
    path_line(data, x + r, y + h);
    path_cubic(data, x + r - offset, y + h,
               x, y + h - r + offset, x, y + h - r);
    path_line(data, x, y + r);
    path_cubic(data, x, y + r - offset,
               x + r - offset, y, x + r, y);
}

static vg_lite_error_t draw_path(vg_lite_buffer_t *target,
                                 fui_path_data_t *data,
                                 const fui_rect_t *bounds,
                                 fui_color_t color)
{
    vg_lite_path_t path;
    vg_lite_matrix_t matrix;
    vg_lite_error_t result;
    memset(&path, 0, sizeof(path));
    vg_lite_identity(&matrix);
    result = vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH,
                               data->count * sizeof(uint32_t), data->words,
                               bounds->x, bounds->y,
                               bounds->x + bounds->width,
                               bounds->y + bounds->height);
    if (result != VG_LITE_SUCCESS) return result;
    return vg_lite_draw(target, &path, VG_LITE_FILL_NON_ZERO, &matrix,
                        VG_LITE_BLEND_SRC_OVER, color);
}

static void path_line_command(fui_path_data_t *data,
                              const fui_command_t *command,
                              fui_rect_t *bounds)
{
    int32_t x1 = command->data.line.x1;
    int32_t y1 = command->data.line.y1;
    int32_t x2 = command->data.line.x2;
    int32_t y2 = command->data.line.y2;
    int32_t dx;
    int32_t dy;
    int32_t swap;
    /* Normalize direction so every generated quadrilateral has the same
     * winding as a rectangle. Non-zero compound fill then produces a union
     * at icon joints instead of punching even/odd holes. Filled open subpaths
     * are implicitly closed by the next MOVE or the final END; VLC_OP_CLOSE
     * is driver-internal according to the VG-Lite user guide. */
    if (x2 < x1 || (x2 == x1 && y2 < y1))
    {
        swap = x1; x1 = x2; x2 = swap;
        swap = y1; y1 = y2; y2 = swap;
    }
    dx = x2 - x1;
    dy = y2 - y1;
    const float length = (float)((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ?
                                 (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy));
    const float half = command->data.line.width * 0.5f;
    float nx;
    float ny;
    if (length < 1.0f)
    {
        memset(bounds, 0, sizeof(*bounds));
        return;
    }
    nx = -(float)dy * half / length;
    ny = (float)dx * half / length;
    path_move(data, x1 - nx, y1 - ny);
    path_line(data, x2 - nx, y2 - ny);
    path_line(data, x2 + nx, y2 + ny);
    path_line(data, x1 + nx, y1 + ny);
    bounds->x = (int16_t)(x1 - command->data.line.width);
    bounds->y = (int16_t)((y1 < y2 ? y1 : y2) - command->data.line.width);
    bounds->width = (int16_t)((dx < 0 ? -dx : dx) +
                              command->data.line.width * 2U + 1U);
    bounds->height = (int16_t)((dy < 0 ? -dy : dy) +
                               command->data.line.width * 2U + 1U);
}

static void bounds_union(fui_rect_t *bounds, const fui_rect_t *item,
                         bool *valid)
{
    int32_t right;
    int32_t bottom;
    int32_t item_right;
    int32_t item_bottom;
    if (item->width <= 0 || item->height <= 0) return;
    if (!*valid)
    {
        *bounds = *item;
        *valid = true;
        return;
    }
    right = (int32_t)bounds->x + bounds->width;
    bottom = (int32_t)bounds->y + bounds->height;
    item_right = (int32_t)item->x + item->width;
    item_bottom = (int32_t)item->y + item->height;
    if (item->x < bounds->x) bounds->x = item->x;
    if (item->y < bounds->y) bounds->y = item->y;
    if (item_right > right) right = item_right;
    if (item_bottom > bottom) bottom = item_bottom;
    bounds->width = (int16_t)(right - bounds->x);
    bounds->height = (int16_t)(bottom - bounds->y);
}

static uint16_t command_path_words(const fui_command_t *command)
{
    if (command->type == FUI_CMD_LINE) return 12U;
    if (command->type == FUI_CMD_LINE_BATCH)
        return (uint16_t)(command->data.line_batch.count * 12U);
    if (command->type == FUI_CMD_RECT)
        return command->data.rect.radius == 0U ? 12U : 43U;
    return 0U;
}

static bool command_is_opaque_path(const fui_command_t *command)
{
    return (command->type == FUI_CMD_RECT || command->type == FUI_CMD_LINE ||
            command->type == FUI_CMD_LINE_BATCH) &&
           (command->color >> 24) == 0xffU;
}

static uint16_t command_path_primitives(const fui_command_t *command)
{
    return command->type == FUI_CMD_LINE_BATCH ?
           command->data.line_batch.count : 1U;
}

static void append_path_command(fui_path_data_t *data,
                                const fui_command_t *command,
                                fui_rect_t *bounds, bool *bounds_valid)
{
    fui_rect_t item_bounds;
    if (command->type == FUI_CMD_RECT)
    {
        path_rounded_rect(data, &command->data.rect.rect,
                          command->data.rect.radius);
        item_bounds = command->data.rect.rect;
    }
    else if (command->type == FUI_CMD_LINE)
    {
        path_line_command(data, command, &item_bounds);
    }
    else
    {
        uint8_t i;
        for (i = 0U; i < command->data.line_batch.count; i++)
        {
            fui_command_t line;
            memset(&line, 0, sizeof(line));
            line.type = FUI_CMD_LINE;
            line.data.line.x1 = command->data.line_batch.points[i][0];
            line.data.line.y1 = command->data.line_batch.points[i][1];
            line.data.line.x2 = command->data.line_batch.points[i][2];
            line.data.line.y2 = command->data.line_batch.points[i][3];
            line.data.line.width = command->data.line_batch.width;
            path_line_command(data, &line, &item_bounds);
            bounds_union(bounds, &item_bounds, bounds_valid);
        }
        return;
    }
    bounds_union(bounds, &item_bounds, bounds_valid);
}

static vg_lite_error_t encode_single_path(vg_lite_buffer_t *target,
                                          const fui_command_t *command)
{
    fui_rect_t bounds = {0, 0, 0, 0};
    bool bounds_valid = false;
    s_path_batch.count = 0U;
    append_path_command(&s_path_batch, command, &bounds, &bounds_valid);
    if (!bounds_valid) return VG_LITE_SUCCESS;
    path_end(&s_path_batch);
    return draw_path(target, &s_path_batch, &bounds, command->color);
}

static vg_lite_error_t draw_glyph(vg_lite_buffer_t *target,
                                  const fui_command_t *command)
{
    uint32_t glyph = command->data.glyph.glyph;
    fui_font_glyph_t product;
    vg_lite_rectangle_t source;
    vg_lite_matrix_t matrix;
    if (!fui_font_glyph_get(glyph, 2U, &product))
        return VG_LITE_INVALID_ARGUMENT;
    source.x = product.source_x;
    source.y = product.source_y;
    source.width = product.width;
    source.height = product.height;
    vg_lite_identity(&matrix);
    vg_lite_translate(command->data.glyph.x, command->data.glyph.y, &matrix);
    if (command->data.glyph.scale >= 3U) vg_lite_scale(1.5f, 1.5f, &matrix);
    else if (command->data.glyph.scale <= 1U)
        vg_lite_scale(0.75f, 0.75f, &matrix);
    return vg_lite_blit_rect(target, (vg_lite_buffer_t *)product.atlas,
                             &source, &matrix, VG_LITE_BLEND_SRC_OVER,
                             command->color, VG_LITE_FILTER_LINEAR);
}

static vg_lite_error_t draw_text_run(vg_lite_buffer_t *target,
                                     const fui_command_t *command)
{
    const vg_lite_buffer_t *source =
        fui_text_cache_get(command->data.text_run.cache_id);
    vg_lite_matrix_t matrix;
    if (source == RT_NULL) return VG_LITE_INVALID_ARGUMENT;
    vg_lite_identity(&matrix);
    vg_lite_translate(command->data.text_run.x,
                      command->data.text_run.y, &matrix);
    if (command->data.text_run.scale >= 3U) vg_lite_scale(1.5f, 1.5f, &matrix);
    else if (command->data.text_run.scale <= 1U)
        vg_lite_scale(0.75f, 0.75f, &matrix);
    return vg_lite_blit(target, (vg_lite_buffer_t *)source, &matrix,
                        VG_LITE_BLEND_SRC_OVER, command->color,
                        VG_LITE_FILTER_LINEAR);
}

static vg_lite_error_t draw_image_rgb565(vg_lite_buffer_t *target,
                                         const fui_command_t *command)
{
    const fui_image_rgb565_t *image = &command->data.image.image;
    const fui_rect_t *crop = &command->data.image.source;
    vg_lite_buffer_t source;
    vg_lite_rectangle_t source_rect;
    vg_lite_matrix_t matrix;
    memset(&source, 0, sizeof(source));
    source.width = image->width;
    source.height = image->height;
    source.stride = image->stride_pixels * sizeof(uint16_t);
    source.format = VG_LITE_RGB565;
    source.tiled = VG_LITE_LINEAR;
    source.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    source.transparency_mode = VG_LITE_IMAGE_OPAQUE;
    source.memory = (void *)image->pixels;
    source.address = (uint32_t)(uintptr_t)image->pixels;
    source_rect.x = crop->x;
    source_rect.y = crop->y;
    source_rect.width = crop->width;
    source_rect.height = crop->height;
    vg_lite_identity(&matrix);
    vg_lite_translate(command->data.image.x, command->data.image.y, &matrix);
    return vg_lite_blit_rect(target, &source, &source_rect, &matrix,
                             VG_LITE_BLEND_NONE, 0xffffffffU,
                             VG_LITE_FILTER_POINT);
}

void fui_renderer_get_frame_stats(fui_renderer_frame_stats_t *stats)
{
    if (stats != RT_NULL) *stats = s_frame_stats;
}

int fui_renderer_init(void)
{
    vg_lite_error_t result;
    fui_font_glyph_t bootstrap;
    (void)fui_font_glyph_get((uint32_t)'?', 1U, &bootstrap);
    fui_text_cache_init();
    SCB_CleanDCache();
    /* vg_lite_init() leaves an initialization stream pending on this driver.
     * Drain it before the first UI frame, then start frame accounting from a
     * clean command buffer.  Initialization is not part of a rendered frame. */
    result = vg_lite_finish();
    if (result != VG_LITE_SUCCESS) return -(int)result;
    g_fui_gpu_submit_count = 0U;
    g_fui_gpu_submit_bytes = 0U;
    g_fui_gpu_completed_jobs = 0U;
    g_fui_gpu_busy_cycles = 0U;
    g_fui_gpu_busy_last_cycles = 0U;
    return 0;
}

int fui_renderer_render(const fui_display_list_t *list, void *framebuffer,
                        uint16_t width, uint16_t height,
                        uint16_t stride_pixels)
{
    vg_lite_buffer_t target;
    vg_lite_error_t result = VG_LITE_SUCCESS;
    uint16_t i;
    uint32_t encode_start;
    if (list == RT_NULL || framebuffer == RT_NULL) return -RT_EINVAL;
    memset(&target, 0, sizeof(target));
    target.width = width;
    target.height = height;
    target.stride = stride_pixels * sizeof(uint16_t);
    target.format = VG_LITE_RGB565;
    target.tiled = VG_LITE_LINEAR;
    target.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    target.transparency_mode = VG_LITE_IMAGE_OPAQUE;
    target.memory = framebuffer;
    target.address = (uint32_t)(uintptr_t)framebuffer;

    /* Do not call vg_lite_set_scissor() in this loop.  The official driver
     * treats a dirty scissor as a render-target transition and flushes the
     * command buffer from set_render_target().  Geometry is clipped while the
     * display list is collected, which preserves the strict one-submit frame
     * contract. */
    memset(&s_frame_stats, 0, sizeof(s_frame_stats));
    s_defer_vglite_cache_maintenance = true;
    encode_start = fui_clock_cycles();
    for (i = 0U; i < list->count;)
    {
        const fui_command_t *command = &list->commands[i];
        uint32_t call_start;
        switch (command->type)
        {
        case FUI_CMD_CLEAR:
            call_start = fui_clock_cycles();
            result = vg_lite_clear(&target, RT_NULL, command->color);
            s_frame_stats.clear_encode_cycles += fui_clock_cycles() - call_start;
            s_frame_stats.clear_calls++;
            i++;
            break;
        case FUI_CMD_RECT:
        case FUI_CMD_LINE:
        case FUI_CMD_LINE_BATCH:
            if (command_is_opaque_path(command))
            {
                fui_rect_t bounds = {0, 0, 0, 0};
                fui_color_t batch_color = command->color;
                uint16_t batch_count = 0U;
                uint16_t primitive_count = 0U;
                bool bounds_valid = false;
                s_path_batch.count = 0U;
                while (i < list->count &&
                       command_is_opaque_path(&list->commands[i]) &&
                       list->commands[i].color == batch_color)
                {
                    uint16_t required = command_path_words(&list->commands[i]);
                    if ((uint32_t)s_path_batch.count + required + 1U > FUI_PATH_WORDS)
                        break;
                    append_path_command(&s_path_batch, &list->commands[i],
                                        &bounds, &bounds_valid);
                    primitive_count += command_path_primitives(&list->commands[i]);
                    batch_count++;
                    i++;
                }
                if (!bounds_valid)
                {
                    result = VG_LITE_SUCCESS;
                    break;
                }
                if (batch_count == 1U && command->type == FUI_CMD_RECT &&
                    command->data.rect.radius == 0U)
                {
                    vg_lite_rectangle_t area = {
                        command->data.rect.rect.x, command->data.rect.rect.y,
                        command->data.rect.rect.width, command->data.rect.rect.height};
                    call_start = fui_clock_cycles();
                    result = vg_lite_clear(&target, &area, command->color);
                    s_frame_stats.clear_encode_cycles += fui_clock_cycles() - call_start;
                    s_frame_stats.clear_calls++;
                }
                else
                {
                    path_end(&s_path_batch);
                    call_start = fui_clock_cycles();
                    result = draw_path(&target, &s_path_batch, &bounds, batch_color);
                    s_frame_stats.path_encode_cycles += fui_clock_cycles() - call_start;
                    s_frame_stats.path_calls++;
                    s_frame_stats.path_primitives += primitive_count;
                    if (primitive_count > s_frame_stats.path_batch_peak)
                        s_frame_stats.path_batch_peak = primitive_count;
                }
            }
            else
            {
                call_start = fui_clock_cycles();
                result = encode_single_path(&target, command);
                s_frame_stats.path_encode_cycles += fui_clock_cycles() - call_start;
                s_frame_stats.path_calls++;
                s_frame_stats.path_primitives += command_path_primitives(command);
                if (s_frame_stats.path_batch_peak == 0U)
                    s_frame_stats.path_batch_peak = 1U;
                i++;
            }
            break;
        case FUI_CMD_GLYPH:
            call_start = fui_clock_cycles();
            result = draw_glyph(&target, command);
            s_frame_stats.blit_encode_cycles += fui_clock_cycles() - call_start;
            s_frame_stats.blit_calls++;
            i++;
            break;
        case FUI_CMD_TEXT_RUN:
            call_start = fui_clock_cycles();
            result = draw_text_run(&target, command);
            s_frame_stats.blit_encode_cycles += fui_clock_cycles() - call_start;
            s_frame_stats.blit_calls++;
            i++;
            break;
        case FUI_CMD_IMAGE_RGB565:
            call_start = fui_clock_cycles();
            result = draw_image_rgb565(&target, command);
            s_frame_stats.blit_encode_cycles += fui_clock_cycles() - call_start;
            s_frame_stats.blit_calls++;
            i++;
            break;
        default:
            result = VG_LITE_INVALID_ARGUMENT;
            i++;
            break;
        }
        if (result != VG_LITE_SUCCESS)
        {
            s_defer_vglite_cache_maintenance = false;
            return -(int)result;
        }
    }
    /* All CPU-written source/cache and command data becomes visible once,
     * immediately before the one frame submission. Per-blit whole-cache
     * clean+invalidate is redundant inside this transaction and dominates
     * text encoding time on Cortex-M55. */
    SCB_CleanDCache();
    s_frame_stats.encode_cycles = fui_clock_cycles() - encode_start;

    /* This is the sole submission point for a normal frame. VG-Lite has
     * collected every primitive above in one command buffer. finish() appends
     * END, submits once, and waits for that one frame fence. */
    result = vg_lite_finish();
    s_defer_vglite_cache_maintenance = false;
    return result == VG_LITE_SUCCESS ? 0 : -(int)result;
}
