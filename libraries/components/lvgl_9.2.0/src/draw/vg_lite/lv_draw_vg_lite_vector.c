/**
 * @file lv_draw_vg_lite_vector.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "../lv_image_decoder_private.h"
#include "../lv_draw_vector_private.h"
#include "lv_draw_vg_lite.h"

#if LV_USE_DRAW_VG_LITE && LV_USE_VECTOR_GRAPHIC

#include "lv_draw_vg_lite_type.h"
#include "lv_vg_lite_path.h"
#include "lv_vg_lite_pending.h"
#include "lv_vg_lite_utils.h"
#include "lv_vg_lite_grad.h"
#include "lv_vg_lite_stroke.h"
#include <float.h>
#include <math.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef void * path_drop_data_t;
typedef void (*path_drop_func_t)(struct lv_draw_vg_lite_unit_t *, path_drop_data_t);

typedef struct {
    lv_vg_lite_path_t * fill_path;
    lv_vg_lite_path_t * stroke_path;
    float fill_bounds[4];
    float stroke_bounds[4];
} persistent_path_cache_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void task_draw_cb(void * ctx, const lv_vector_path_t * path, const lv_vector_draw_dsc_t * dsc);
static void lv_path_to_vg(lv_vg_lite_path_t * dest, const lv_vector_path_t * src);
static vg_lite_path_type_t lv_path_opa_to_path_type(const lv_vector_draw_dsc_t * dsc);
static vg_lite_blend_t lv_blend_to_vg(lv_vector_blend_t blend);
static vg_lite_fill_t lv_fill_to_vg(lv_vector_fill_t fill_rule);

static void matrix_compose(vg_lite_matrix_t * dest,
                           const vg_lite_matrix_t * global,
                           const lv_matrix_t * local)
{
    vg_lite_matrix_t local_matrix;

    vg_lite_identity(dest);
    lv_vg_lite_matrix_multiply(dest, global);
    lv_vg_lite_matrix(&local_matrix, local);
    lv_vg_lite_matrix_multiply(dest, &local_matrix);
}

static lv_area_t area_transform(const lv_area_t * src,
                                const vg_lite_matrix_t * matrix)
{
    const float x[4] = {
        (float)src->x1, (float)src->x2 + 1.0f,
        (float)src->x1, (float)src->x2 + 1.0f
    };
    const float y[4] = {
        (float)src->y1, (float)src->y1,
        (float)src->y2 + 1.0f, (float)src->y2 + 1.0f
    };
    float min_x = FLT_MAX;
    float min_y = FLT_MAX;
    float max_x = -FLT_MAX;
    float max_y = -FLT_MAX;
    lv_area_t dest;
    uint32_t i;

    for(i = 0U; i < 4U; i++) {
        float tx = x[i] * matrix->m[0][0] +
                   y[i] * matrix->m[0][1] + matrix->m[0][2];
        float ty = x[i] * matrix->m[1][0] +
                   y[i] * matrix->m[1][1] + matrix->m[1][2];
        min_x = LV_MIN(min_x, tx);
        min_y = LV_MIN(min_y, ty);
        max_x = LV_MAX(max_x, tx);
        max_y = LV_MAX(max_y, ty);
    }

    dest.x1 = (int32_t)floorf(min_x);
    dest.y1 = (int32_t)floorf(min_y);
    dest.x2 = (int32_t)ceilf(max_x) - 1;
    dest.y2 = (int32_t)ceilf(max_y) - 1;
    return dest;
}

static bool area_contains(const lv_area_t * outer, const lv_area_t * inner)
{
    return inner->x1 >= outer->x1 && inner->y1 >= outer->y1 &&
           inner->x2 <= outer->x2 && inner->y2 <= outer->y2;
}

static void persistent_path_cache_free(void * data)
{
    persistent_path_cache_t * cache = data;
    if(cache == NULL) return;
    if(cache->fill_path) lv_vg_lite_path_destroy(cache->fill_path);
    if(cache->stroke_path) lv_vg_lite_path_destroy(cache->stroke_path);
    lv_free(cache);
}

static void persistent_path_drop(lv_draw_vg_lite_unit_t * u, path_drop_data_t data)
{
    LV_UNUSED(u);
    LV_UNUSED(data);
}

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_vg_lite_vector(lv_draw_unit_t * draw_unit, const lv_draw_vector_task_dsc_t * dsc)
{
    if(dsc->task_list == NULL)
        return;

    lv_layer_t * layer = dsc->base.layer;
    if(layer->draw_buf == NULL)
        return;

    LV_PROFILER_BEGIN;
    lv_vector_for_each_destroy_tasks(dsc->task_list, task_draw_cb, draw_unit);
    LV_PROFILER_END;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static vg_lite_color_t lv_color32_to_vg(lv_color32_t color, lv_opa_t opa)
{
    uint8_t a = LV_OPA_MIX2(color.alpha, opa);
    if(a < LV_OPA_COVER) {
        color.red = LV_UDIV255(color.red * a);
        color.green = LV_UDIV255(color.green * a);
        color.blue = LV_UDIV255(color.blue * a);
    }
    /* vg_lite_color_t is ARGB8888 (0xAARRGGBB).  Keep this identical to
     * lv_vg_lite_color(), which is used by fills, labels, and A8 images.
     * Packing B and R in reverse makes vector icons visibly use the wrong
     * accent color while the surrounding LVGL objects remain correct. */
    return (uint32_t)a << 24 | (uint32_t)color.red << 16 | (uint32_t)color.green << 8 | color.blue;
}

static void task_draw_cb(void * ctx, const lv_vector_path_t * path, const lv_vector_draw_dsc_t * dsc)
{
    LV_PROFILER_BEGIN;
    lv_draw_vg_lite_unit_t * u = ctx;
    bool changed_scissor = false;
    lv_area_t active_scissor = u->base_unit.target_layer->phy_clip_area;
    lv_area_t vector_scissor = area_transform(&dsc->scissor_area,
                                              &u->global_matrix);
    lv_area_move(&active_scissor,
                 -u->base_unit.target_layer->buf_area.x1,
                 -u->base_unit.target_layer->buf_area.y1);
    LV_VG_LITE_ASSERT_DEST_BUFFER(&u->target_buffer);

    /* clear area */
    if(!path) {
        /* clear color needs to ignore fill_dsc.opa */
        vg_lite_color_t c = lv_color32_to_vg(dsc->fill_dsc.color, LV_OPA_COVER);
        vg_lite_rectangle_t rect;
        lv_vg_lite_rect(&rect, &vector_scissor);
        LV_PROFILER_BEGIN_TAG("vg_lite_clear");
        LV_VG_LITE_CHECK_ERROR(vg_lite_clear(&u->target_buffer, &rect, c));
        LV_PROFILER_END_TAG("vg_lite_clear");
        LV_PROFILER_END;
        return;
    }

    /* convert color */
    vg_lite_color_t vg_color = lv_color32_to_vg(dsc->fill_dsc.color, dsc->fill_dsc.opa);

    /* convert path type before selecting the cached native representation;
     * pure strokes intentionally omit END while fill paths require it. */
    vg_lite_path_type_t path_type = lv_path_opa_to_path_type(dsc);

    /* transform matrix */
    vg_lite_matrix_t matrix;
    matrix_compose(&matrix, &u->global_matrix, &dsc->matrix);
    LV_VG_LITE_ASSERT_MATRIX(&matrix);

    /* Persistent SVG/font paths are translated and uploaded exactly once.
     * Mutable procedural paths retain the reusable per-task conversion path. */
    const bool persistent = lv_vector_path_is_immutable(path);
    const bool add_end = path_type == VG_LITE_DRAW_ZERO ||
                         path_type == VG_LITE_DRAW_FILL_PATH ||
                         path_type == VG_LITE_DRAW_FILL_STROKE_PATH;
    float persistent_bounds[4];
    lv_vg_lite_path_t * lv_vg_path;
    if(persistent) {
        lv_vg_path = lv_draw_vg_lite_vector_path_prepare(path, add_end, persistent_bounds);
        if(lv_vg_path == NULL) {
            LV_LOG_ERROR("persistent vector path preparation failed");
            LV_PROFILER_END;
            return;
        }
        lv_vg_lite_path_set_bonding_box(lv_vg_path,
                                        persistent_bounds[0], persistent_bounds[1],
                                        persistent_bounds[2], persistent_bounds[3]);
    }
    else {
        lv_vg_path = lv_vg_lite_path_get(u, VG_LITE_FP32);
        lv_path_to_vg(lv_vg_path, path);
    }

    /* get path bounds */
    float min_x, min_y, max_x, max_y;
    lv_vg_lite_path_get_bonding_box(lv_vg_path, &min_x, &min_y, &max_x, &max_y);
    float stroke_pad = dsc->stroke_dsc.opa > LV_OPA_MIN ?
                       dsc->stroke_dsc.width * 0.5f + 1.0f : 0.0f;
    lv_area_t source_path_bounds = {
        .x1 = (int32_t)floorf(min_x - stroke_pad),
        .y1 = (int32_t)floorf(min_y - stroke_pad),
        .x2 = (int32_t)ceilf(max_x + stroke_pad) - 1,
        .y2 = (int32_t)ceilf(max_y + stroke_pad) - 1
    };
    lv_area_t device_path_bounds = area_transform(&source_path_bounds, &matrix);

    /* convert blend mode and fill rule */
    vg_lite_blend_t blend = lv_blend_to_vg(dsc->blend_mode);
    vg_lite_fill_t fill = lv_fill_to_vg(dsc->fill_dsc.fill_rule);

    /* set default path drop function and data */
    path_drop_func_t path_drop_func = persistent ? persistent_path_drop :
                                                 (path_drop_func_t)lv_vg_lite_path_drop;
    path_drop_data_t path_drop_data = lv_vg_path;

    /* If it is fill mode, the end op code should be added */
    if(path_type == VG_LITE_DRAW_ZERO
       || path_type == VG_LITE_DRAW_FILL_PATH
       || path_type == VG_LITE_DRAW_FILL_STROKE_PATH) {
        if(!persistent) lv_vg_lite_path_end(lv_vg_path);
    }

    /* convert stroke style */
    if(path_type == VG_LITE_DRAW_STROKE_PATH
       || path_type == VG_LITE_DRAW_FILL_STROKE_PATH) {
        lv_cache_entry_t * stroke_cache_entey = lv_vg_lite_stroke_get(u, lv_vg_path, &dsc->stroke_dsc);

        if(!stroke_cache_entey) {
            LV_LOG_ERROR("convert stroke failed");

            /* drop original path */
            path_drop_func(u, path_drop_data);
            return;
        }

        lv_vg_lite_path_t * ori_path = lv_vg_path;
        const vg_lite_path_t * ori_vg_path = lv_vg_lite_path_get_path(ori_path);

        lv_vg_lite_path_t * stroke_path = lv_vg_lite_stroke_get_path(stroke_cache_entey);
        vg_lite_path_t * vg_path = lv_vg_lite_path_get_path(stroke_path);

        /* set stroke params */
        LV_VG_LITE_CHECK_ERROR(vg_lite_set_path_type(vg_path, path_type));
        vg_path->stroke_color = lv_color32_to_vg(dsc->stroke_dsc.color, dsc->stroke_dsc.opa);
        vg_path->quality = ori_vg_path->quality;
        /* vg_lite_update_stroke() expands the centerline into an outline, but
         * it keeps the original path bounding box.  Reusing that unexpanded
         * box clips the outer half of the stroke: circles acquire flat sides
         * and folder/image outlines look cut by a rectangle.  The path box is
         * expressed in source coordinates, so include half the source stroke
         * width plus one AA guard pixel.  The LVGL/object scissor remains the
         * authoritative logical clip after the matrix is applied. */
        vg_path->bounding_box[0] = min_x - stroke_pad;
        vg_path->bounding_box[1] = min_y - stroke_pad;
        vg_path->bounding_box[2] = max_x + stroke_pad;
        vg_path->bounding_box[3] = max_y + stroke_pad;

        /* change path to stroke path */
        LV_LOG_TRACE("change path to stroke path: %p -> %p", (void *)lv_vg_path, (void *)stroke_path);
        lv_vg_path = stroke_path;
        path_drop_func = (path_drop_func_t)lv_vg_lite_stroke_drop;
        path_drop_data = stroke_cache_entey;

        /* drop original path */
        if(!persistent) lv_vg_lite_path_drop(u, ori_path);
    }

    vg_lite_path_t * vg_path = lv_vg_lite_path_get_path(lv_vg_path);
    LV_VG_LITE_ASSERT_PATH(vg_path);

    if(vg_lite_query_feature(gcFEATURE_BIT_VG_SCISSOR)) {
        /* A path that already fits inside its logical clip cannot produce any
         * pixels outside that clip, so programming hardware scissor would be
         * redundant. This is the common SVG icon case and, importantly, keeps
         * every icon in the Tile transform layer on one command stream. Only
         * paths whose transformed fill/stroke bounds actually cross the clip
         * need a narrower hardware scissor. */
        if(!area_contains(&vector_scissor, &device_path_bounds)) {
            lv_vg_lite_set_scissor_area(&vector_scissor);
            changed_scissor = true;
        }
    }
    else {
        /* calc inverse matrix */
        vg_lite_matrix_t result;
        if(!lv_vg_lite_matrix_inverse(&result, &matrix)) {
            LV_LOG_ERROR("no inverse matrix");
            path_drop_func(u, path_drop_data);
            LV_PROFILER_END;
            return;
        }

        /* Reverse the clip area on the source */
        lv_point_precise_t p1 = { dsc->scissor_area.x1, dsc->scissor_area.y1 };
        lv_point_precise_t p1_res = lv_vg_lite_matrix_transform_point(&result, &p1);

        /* vg-lite bounding_box will crop the pixels on the edge, so +1px is needed here */
        lv_point_precise_t p2 = { dsc->scissor_area.x2 + 1, dsc->scissor_area.y2 + 1 };
        lv_point_precise_t p2_res = lv_vg_lite_matrix_transform_point(&result, &p2);

        lv_vg_lite_path_set_bonding_box(lv_vg_path, p1_res.x, p1_res.y, p2_res.x, p2_res.y);
    }

    switch(dsc->fill_dsc.style) {
        case LV_VECTOR_DRAW_STYLE_SOLID: {
                /* normal draw shape */
                LV_PROFILER_BEGIN_TAG("vg_lite_draw");
                LV_VG_LITE_CHECK_ERROR(vg_lite_draw(
                                           &u->target_buffer,
                                           vg_path,
                                           fill,
                                           &matrix,
                                           blend,
                                           vg_color));
                LV_PROFILER_END_TAG("vg_lite_draw");
            }
            break;
        case LV_VECTOR_DRAW_STYLE_PATTERN: {
                /* draw image */
                vg_lite_buffer_t image_buffer;
                lv_image_decoder_dsc_t decoder_dsc;
                if(lv_vg_lite_buffer_open_image(&image_buffer, &decoder_dsc, dsc->fill_dsc.img_dsc.src, false)) {
                    /* Calculate pattern matrix. Should start from path bond box, and also apply fill matrix. */
                    lv_matrix_t m = dsc->matrix;
                    lv_matrix_translate(&m, min_x, min_y);
                    lv_matrix_multiply(&m, &dsc->fill_dsc.matrix);

                    vg_lite_matrix_t pattern_matrix;
                    matrix_compose(&pattern_matrix, &u->global_matrix, &m);

                    vg_lite_color_t recolor = lv_vg_lite_color(dsc->fill_dsc.img_dsc.recolor, dsc->fill_dsc.img_dsc.recolor_opa, true);

                    LV_VG_LITE_ASSERT_MATRIX(&pattern_matrix);

                    LV_PROFILER_BEGIN_TAG("vg_lite_draw_pattern");
                    LV_VG_LITE_CHECK_ERROR(vg_lite_draw_pattern(
                                               &u->target_buffer,
                                               vg_path,
                                               fill,
                                               &matrix,
                                               &image_buffer,
                                               &pattern_matrix,
                                               blend,
                                               VG_LITE_PATTERN_COLOR,
                                               vg_color,
                                               recolor,
                                               VG_LITE_FILTER_BI_LINEAR));
                    LV_PROFILER_END_TAG("vg_lite_draw_pattern");

                    lv_vg_lite_pending_add(u->image_dsc_pending, &decoder_dsc);
                }
            }
            break;
        case LV_VECTOR_DRAW_STYLE_GRADIENT: {
                lv_matrix_t grad_local = dsc->matrix;
                lv_matrix_multiply(&grad_local, &dsc->fill_dsc.matrix);
                vg_lite_matrix_t grad_matrix;
                matrix_compose(&grad_matrix, &u->global_matrix, &grad_local);

                lv_vg_lite_draw_grad(
                    u,
                    &u->target_buffer,
                    vg_path,
                    &dsc->fill_dsc.gradient,
                    &grad_matrix,
                    &matrix,
                    fill,
                    blend);
            }
            break;
        default:
            LV_LOG_WARN("unknown style: %d", dsc->fill_dsc.style);
            break;
    }

    /* Flush in time to avoid accumulation of drawing commands */
    lv_vg_lite_flush(u);

    /* drop path */
    path_drop_func(u, path_drop_data);

    if(changed_scissor) {
        /* Restore the exact device-space clip installed by draw_execute(). */
        lv_vg_lite_set_scissor_area(&active_scissor);
    }

    LV_PROFILER_END;
}

static vg_lite_quality_t lv_quality_to_vg(lv_vector_path_quality_t quality)
{
    switch(quality) {
        case LV_VECTOR_PATH_QUALITY_LOW:
            return VG_LITE_LOW;
        case LV_VECTOR_PATH_QUALITY_MEDIUM:
            return VG_LITE_MEDIUM;
        case LV_VECTOR_PATH_QUALITY_HIGH:
            return VG_LITE_HIGH;
        default:
            return VG_LITE_MEDIUM;
    }
}

lv_vg_lite_path_t * lv_draw_vg_lite_vector_path_prepare(const lv_vector_path_t * path,
                                                         bool add_end, float bounds[4])
{
    persistent_path_cache_t * cache = lv_vector_path_get_backend_data(path);
    lv_vg_lite_path_t ** slot;
    float * cached_bounds;

    if(cache == NULL) {
        cache = lv_malloc_zeroed(sizeof(*cache));
        if(cache == NULL) return NULL;
        lv_vector_path_set_backend_data((lv_vector_path_t *)path, cache,
                                        persistent_path_cache_free);
    }
    slot = add_end ? &cache->fill_path : &cache->stroke_path;
    cached_bounds = add_end ? cache->fill_bounds : cache->stroke_bounds;
    if(*slot == NULL) {
        *slot = lv_vg_lite_path_create(VG_LITE_FP32);
        if(*slot == NULL) return NULL;
        lv_path_to_vg(*slot, path);
        if(add_end) lv_vg_lite_path_end(*slot);
        lv_vg_lite_path_get_bonding_box(*slot,
                                        &cached_bounds[0], &cached_bounds[1],
                                        &cached_bounds[2], &cached_bounds[3]);
        /* Runtime-converted LVGL paths are kept inline. Their lifetime and
         * command termination are more varied than the offline-generated
         * native assets, and broad upload/CALL testing exposed a GPU stall.
         * Only attach_native() below opts a path into persistent CALL. */
    }
    lv_memcpy(bounds, cached_bounds, sizeof(cache->fill_bounds));
    return *slot;
}

bool lv_draw_vg_lite_vector_path_attach_native(lv_vector_path_t * path,
                                               bool add_end,
                                               lv_vg_lite_path_t * native_path,
                                               const float bounds[4])
{
    persistent_path_cache_t * cache;
    lv_vg_lite_path_t ** slot;
    float * cached_bounds;

    if(path == NULL || native_path == NULL || bounds == NULL) return false;
    cache = lv_vector_path_get_backend_data(path);
    if(cache == NULL) {
        cache = lv_malloc_zeroed(sizeof(*cache));
        if(cache == NULL) return false;
        lv_vector_path_set_backend_data(path, cache, persistent_path_cache_free);
    }

    slot = add_end ? &cache->fill_path : &cache->stroke_path;
    cached_bounds = add_end ? cache->fill_bounds : cache->stroke_bounds;
    if(*slot != NULL) return false;
    *slot = native_path;
    lv_memcpy(cached_bounds, bounds, sizeof(cache->fill_bounds));
#if LV_VG_LITE_USE_PATH_UPLOAD
    if(add_end && lv_vg_lite_path_upload(native_path) != VG_LITE_SUCCESS) {
        /* The immutable XIP stream remains a valid fallback if GPU-addressable
         * storage is temporarily exhausted. */
        LV_LOG_WARN("native VG-Lite path upload failed; using XIP stream");
    }
#endif
    return true;
}

static void lv_path_to_vg(lv_vg_lite_path_t * dest, const lv_vector_path_t * src)
{
    LV_PROFILER_BEGIN;
    lv_vg_lite_path_set_quality(dest, lv_quality_to_vg(src->quality));

    /* init bounds */
    float min_x = FLT_MAX;
    float min_y = FLT_MAX;
    float max_x = FLT_MIN;
    float max_y = FLT_MIN;

#define CMP_BOUNDS(point)                           \
    do {                                            \
        if((point)->x < min_x) min_x = (point)->x;  \
        if((point)->y < min_y) min_y = (point)->y;  \
        if((point)->x > max_x) max_x = (point)->x;  \
        if((point)->y > max_y) max_y = (point)->y;  \
    } while(0)

    uint32_t pidx = 0;
    lv_vector_path_op_t * op = lv_array_front(&src->ops);
    uint32_t size = lv_array_size(&src->ops);
    for(uint32_t i = 0; i < size; i++) {
        switch(op[i]) {
            case LV_VECTOR_PATH_OP_MOVE_TO: {
                    const lv_fpoint_t * pt = lv_array_at(&src->points, pidx);
                    CMP_BOUNDS(pt);
                    lv_vg_lite_path_move_to(dest, pt->x, pt->y);
                    pidx += 1;
                }
                break;
            case LV_VECTOR_PATH_OP_LINE_TO: {
                    const lv_fpoint_t * pt = lv_array_at(&src->points, pidx);
                    CMP_BOUNDS(pt);
                    lv_vg_lite_path_line_to(dest, pt->x, pt->y);
                    pidx += 1;
                }
                break;
            case LV_VECTOR_PATH_OP_QUAD_TO: {
                    const lv_fpoint_t * pt1 = lv_array_at(&src->points, pidx);
                    const lv_fpoint_t * pt2 = lv_array_at(&src->points, pidx + 1);
                    CMP_BOUNDS(pt1);
                    CMP_BOUNDS(pt2);
                    lv_vg_lite_path_quad_to(dest, pt1->x, pt1->y, pt2->x, pt2->y);
                    pidx += 2;
                }
                break;
            case LV_VECTOR_PATH_OP_CUBIC_TO: {
                    const lv_fpoint_t * pt1 = lv_array_at(&src->points, pidx);
                    const lv_fpoint_t * pt2 = lv_array_at(&src->points, pidx + 1);
                    const lv_fpoint_t * pt3 = lv_array_at(&src->points, pidx + 2);
                    CMP_BOUNDS(pt1);
                    CMP_BOUNDS(pt2);
                    CMP_BOUNDS(pt3);
                    lv_vg_lite_path_cubic_to(dest, pt1->x, pt1->y, pt2->x, pt2->y, pt3->x, pt3->y);
                    pidx += 3;
                }
                break;
            case LV_VECTOR_PATH_OP_CLOSE: {
                    lv_vg_lite_path_close(dest);
                }
                break;
        }
    }

    lv_vg_lite_path_set_bonding_box(dest, min_x, min_y, max_x, max_y);
    LV_PROFILER_END;
}

static vg_lite_path_type_t lv_path_opa_to_path_type(const lv_vector_draw_dsc_t * dsc)
{
    lv_opa_t fill_opa = dsc->fill_dsc.opa;
    lv_opa_t stroke_opa = dsc->stroke_dsc.opa;

    if(fill_opa > LV_OPA_0 && stroke_opa > LV_OPA_0) {
        return VG_LITE_DRAW_FILL_STROKE_PATH;
    }

    if(fill_opa == LV_OPA_0 && stroke_opa > LV_OPA_0) {
        return VG_LITE_DRAW_STROKE_PATH;
    }

    if(fill_opa > LV_OPA_0) {
        return VG_LITE_DRAW_FILL_PATH;
    }

    return VG_LITE_DRAW_ZERO;
}

static vg_lite_blend_t lv_blend_to_vg(lv_vector_blend_t blend)
{
    switch(blend) {
        case LV_VECTOR_BLEND_SRC_OVER:
            return VG_LITE_BLEND_SRC_OVER;
        case LV_VECTOR_BLEND_SCREEN:
            return VG_LITE_BLEND_SCREEN;
        case LV_VECTOR_BLEND_MULTIPLY:
            return VG_LITE_BLEND_MULTIPLY;
        case LV_VECTOR_BLEND_NONE:
            return VG_LITE_BLEND_NONE;
        case LV_VECTOR_BLEND_ADDITIVE:
            return VG_LITE_BLEND_ADDITIVE;
        case LV_VECTOR_BLEND_SRC_IN:
            return VG_LITE_BLEND_SRC_IN;
        case LV_VECTOR_BLEND_DST_OVER:
            return VG_LITE_BLEND_DST_OVER;
        case LV_VECTOR_BLEND_DST_IN:
            return VG_LITE_BLEND_DST_IN;
        case LV_VECTOR_BLEND_SUBTRACTIVE:
            return VG_LITE_BLEND_SUBTRACT;
        default:
            return VG_LITE_BLEND_SRC_OVER;
    }
}

static vg_lite_fill_t lv_fill_to_vg(lv_vector_fill_t fill_rule)
{
    switch(fill_rule) {
        case LV_VECTOR_FILL_NONZERO:
            return VG_LITE_FILL_NON_ZERO;
        case LV_VECTOR_FILL_EVENODD:
            return VG_LITE_FILL_EVEN_ODD;
        default:
            return VG_LITE_FILL_NON_ZERO;
    }
}

#endif /*LV_USE_DRAW_VG_LITE && LV_USE_VECTOR_GRAPHIC*/
