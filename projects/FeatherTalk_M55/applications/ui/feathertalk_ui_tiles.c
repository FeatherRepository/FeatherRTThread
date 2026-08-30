#include <limits.h>
#include <rtthread.h>
#include <stdint.h>
#include <string.h>
#include "feathertalk_ui_internal.h"

#define FT_TILE_MAX_APPS          16U
#define FT_TILE_NAME_CAPACITY     32U
#define FT_TILE_MAX_ROW_SPAN       3U
#define FT_TILE_LIVE_TICK_MS      200U
#define FT_TILE_EDIT_SCALE        248
#define FT_TILE_HANDLE_SIZE        35
#define FT_TILE_HANDLE_HIT_PAD      8
#define FT_TILE_SEAM_REPAIR_MARGIN 32
#define FT_TILE_EDIT_BORDER         2
#define FT_TILE_GRID_ROWS          (FT_TILE_MAX_APPS * FT_TILE_MAX_ROW_SPAN + 1U)
#define FT_TILE_DESKTOP_MIN_ROWS    12U
#define FT_TILE_DESKTOP_TAIL_ROWS    3U
#define FT_TILE_AUTO_SCROLL_EDGE    48
#define FT_TILE_AUTO_SCROLL_STEP    14
#define FT_TILE_SNAP_DURATION_MS   180U

typedef enum
{
    FT_TILE_HANDLE_RESIZE_TL = 0,
    FT_TILE_HANDLE_RESIZE_TR,
    FT_TILE_HANDLE_RESIZE_BL,
    FT_TILE_HANDLE_RESIZE_BR,
    FT_TILE_HANDLE_COUNT
} ft_tile_handle_id_t;

typedef enum
{
    FT_TILE_INTERACTION_NONE = 0,
    FT_TILE_INTERACTION_MOVE,
    FT_TILE_INTERACTION_RESIZE
} ft_tile_interaction_t;

typedef struct ft_start_tile_runtime ft_start_tile_runtime_t;

typedef struct
{
    ft_start_tile_runtime_t *tile;
    ft_tile_handle_id_t id;
} ft_tile_handle_context_t;

struct ft_start_tile_runtime
{
    const ft_app_descriptor_t *descriptor;
    lv_obj_t *object;
    lv_obj_t *body;
    lv_obj_t *label;
    lv_obj_t *pattern;
    lv_obj_t *live_host;
    lv_obj_t *live_label;
    lv_obj_t *handles[FT_TILE_HANDLE_COUNT];
    ft_tile_handle_context_t handle_context[FT_TILE_HANDLE_COUNT];
    char name[FT_TILE_NAME_CAPACITY];
    uint8_t column_span;
    uint8_t row_span;
    uint8_t grid_column;
    uint8_t grid_row;
    uint8_t opacity;
    ft_icon_id_t pattern_icon;
    bool live_enabled;
    uint32_t live_frame;
    uint32_t live_elapsed_ms;
};

static lv_obj_t *s_container;
static lv_obj_t *s_tileview;
static lv_obj_t *s_scroll_extent;
static bool s_container_was_scrollable;
static bool s_tileview_was_scrollable;
static lv_scrollbar_mode_t s_container_scrollbar_mode;
static bool s_container_scrollbar_saved;
static uint32_t s_scale_redraw_count;
static ft_start_tile_runtime_t s_tiles[FT_TILE_MAX_APPS];
static size_t s_tile_count;
static ft_start_tile_runtime_t *s_selected;
static lv_obj_t *s_placeholder;
static lv_timer_t *s_live_timer;
static ft_tile_interaction_t s_interaction;
static ft_tile_handle_id_t s_resize_handle;
static lv_point_t s_press_point;
static int32_t s_press_x;
static int32_t s_press_y;
static int32_t s_press_width;
static int32_t s_press_height;
static int32_t s_resize_max_width;
static int32_t s_resize_max_height;
static uint8_t s_resize_max_columns;
static uint8_t s_resize_max_rows;
static uint8_t s_resize_start_column;
static uint8_t s_resize_start_row;
static uint8_t s_resize_start_columns;
static uint8_t s_resize_start_rows;
static uint8_t s_resize_base_column[FT_TILE_MAX_APPS];
static uint8_t s_resize_base_row[FT_TILE_MAX_APPS];
static size_t s_last_resize_displaced;
static int32_t s_resize_last_width;
static int32_t s_resize_last_height;
static uint8_t s_resize_reserve_column;
static uint8_t s_resize_reserve_row;
static uint8_t s_resize_reserve_columns;
static uint8_t s_resize_reserve_rows;
static bool s_resize_reservation_valid;
static ft_start_tile_runtime_t *s_resize_settle_tile;
static int32_t s_resize_settle_from_x;
static int32_t s_resize_settle_from_y;
static int32_t s_resize_settle_from_width;
static int32_t s_resize_settle_from_height;
static int32_t s_resize_settle_to_x;
static int32_t s_resize_settle_to_y;
static int32_t s_resize_settle_to_width;
static int32_t s_resize_settle_to_height;
static uint8_t s_move_base_column[FT_TILE_MAX_APPS];
static uint8_t s_move_base_row[FT_TILE_MAX_APPS];
static lv_point_t s_move_desired_center;
static bool s_move_base_valid;
static bool s_move_snap_confirmed;
static uint8_t s_desktop_rows;
static int32_t s_interaction_start_scroll_y;
static lv_area_t s_interaction_visual_area;
static bool s_interaction_visual_valid;

static uint8_t clamp_u8(uint8_t value, uint8_t minimum, uint8_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int32_t tile_width(uint8_t columns)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    columns = clamp_u8(columns, 1U, layout->tile_columns);
    return (int32_t)columns * layout->tile_column_width +
           ((int32_t)columns - 1) * layout->tile_gap;
}

static int32_t tile_height(uint8_t rows)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    rows = clamp_u8(rows, 1U, FT_TILE_MAX_ROW_SPAN);
    return (int32_t)rows * layout->tile_height +
           ((int32_t)rows - 1) * layout->tile_gap;
}

static uint8_t span_from_pixels(int32_t pixels, int32_t unit, int32_t gap,
                                 uint8_t maximum)
{
    int32_t span;
    if (pixels <= unit) return 1U;
    span = (pixels + gap + (unit + gap) / 2) / (unit + gap);
    if (span < 1) span = 1;
    if (span > maximum) span = maximum;
    return (uint8_t)span;
}

static uint8_t span_covering_pixels(int32_t pixels, int32_t unit, int32_t gap,
                                    uint8_t maximum)
{
    int32_t pitch = unit + gap;
    int32_t span;
    if (pitch <= 0) return 1U;
    /* A neighbour yields as soon as the dragged edge enters its grid cell;
     * final size rounding still uses span_from_pixels() on release. */
    span = (pixels + pitch - 1) / pitch;
    if (span < 1) span = 1;
    if (span > maximum) span = maximum;
    return (uint8_t)span;
}

static uint8_t span_that_fits(int32_t pixels, int32_t unit, int32_t gap,
                              uint8_t maximum)
{
    uint8_t span = 1U;
    while (span < maximum)
    {
        int32_t next = (int32_t)(span + 1U) * unit + (int32_t)span * gap;
        if (next > pixels) break;
        span++;
    }
    return span;
}

static bool object_valid(lv_obj_t *object)
{
    return object != RT_NULL && lv_obj_is_valid(object);
}

static void tile_scale_anim_cb(void *object, int32_t value)
{
    lv_obj_t *body = (lv_obj_t *)object;
    if (!object_valid(body)) return;
    /* The complete visual Tile body breathes inside the fixed outer hit box.
     * Corner Chevrons are siblings of this body, so they never transform. */
    lv_obj_set_style_transform_scale(body, value, LV_PART_MAIN);
    s_scale_redraw_count++;
}

static void tile_translate_x_anim_cb(void *object, int32_t value)
{
    if (object_valid((lv_obj_t *)object))
        lv_obj_set_style_translate_x((lv_obj_t *)object, value, LV_PART_MAIN);
}

static void tile_translate_y_anim_cb(void *object, int32_t value)
{
    if (object_valid((lv_obj_t *)object))
        lv_obj_set_style_translate_y((lv_obj_t *)object, value, LV_PART_MAIN);
}

static int32_t grid_x(uint8_t column)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    return (int32_t)column * (layout->tile_column_width + layout->tile_gap);
}

static int32_t grid_y(uint8_t row)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    return (int32_t)row * (layout->tile_height + layout->tile_gap);
}

static int32_t grid_object_x(const ft_start_tile_runtime_t *tile, uint8_t column)
{
    int32_t x = grid_x(column);
    /* LVGL FLOATING children do not inherit their parent's scroll offset when
     * positioned.  Keep a foreground edit Tile on the same screen-space pit
     * as its normal scrolling placeholder. */
    if (tile != RT_NULL && object_valid(tile->object) && object_valid(s_container) &&
        lv_obj_has_flag(tile->object, LV_OBJ_FLAG_FLOATING))
        x -= lv_obj_get_scroll_x(s_container);
    return x;
}

static int32_t grid_object_y(const ft_start_tile_runtime_t *tile, uint8_t row)
{
    int32_t y = grid_y(row);
    if (tile != RT_NULL && object_valid(tile->object) && object_valid(s_container) &&
        lv_obj_has_flag(tile->object, LV_OBJ_FLAG_FLOATING))
        y -= lv_obj_get_scroll_y(s_container);
    return y;
}

static uint8_t desktop_row_limit(void)
{
    return s_desktop_rows > 0U ? s_desktop_rows : FT_TILE_GRID_ROWS;
}

static uint8_t desktop_content_rows(void)
{
    uint8_t rows = 1U;
    size_t i;
    for (i = 0U; i < s_tile_count; i++)
    {
        uint16_t bottom = (uint16_t)s_tiles[i].grid_row + s_tiles[i].row_span;
        if (bottom > rows) rows = (uint8_t)bottom;
    }
    return rows;
}

static void tile_repair_seam(void)
{
    lv_obj_t *screen;
    lv_area_t area;
    lv_area_t screen_area;
    int32_t repair_height;
    if (!object_valid(s_container)) return;
    screen = lv_obj_get_screen(s_container);
    lv_obj_get_content_coords(s_container, &area);
    repair_height = ft_layout_px(FT_TILE_SEAM_REPAIR_MARGIN * 2);
    if (repair_height < 32) repair_height = 32;
    if (area.y2 - repair_height + 1 > area.y1)
        area.y1 = area.y2 - repair_height + 1;
    /* Redraw both owners of the hard boundary.  Invalidating only the page
     * cannot overwrite a pixel that a formerly floating Tile left in the
     * navigation bar's scan lines.  The screen redraw restores child order:
     * content first, navigation last. */
    area.y2 += repair_height;
    if (object_valid(screen))
    {
        lv_obj_get_coords(screen, &screen_area);
        if (area.x1 < screen_area.x1) area.x1 = screen_area.x1;
        if (area.x2 > screen_area.x2) area.x2 = screen_area.x2;
        if (area.y2 > screen_area.y2) area.y2 = screen_area.y2;
        lv_obj_invalidate_area(screen, &area);
    }
    else
        lv_obj_invalidate_area(s_container, &area);
}

static void tile_repair_interaction_transition(const lv_area_t *next)
{
    lv_obj_t *screen;
    lv_area_t viewport;
    lv_area_t screen_area;
    lv_area_t dirty;
    int32_t extent;
    if (next == RT_NULL || !object_valid(s_container)) return;
    if (!s_interaction_visual_valid)
    {
        s_interaction_visual_area = *next;
        s_interaction_visual_valid = true;
        return;
    }
    lv_obj_get_content_coords(s_container, &viewport);
    extent = ft_layout_px(FT_TILE_SEAM_REPAIR_MARGIN);
    dirty.x1 = LV_MIN(s_interaction_visual_area.x1, next->x1) - extent;
    dirty.y1 = LV_MIN(s_interaction_visual_area.y1, next->y1) - extent;
    dirty.x2 = LV_MAX(s_interaction_visual_area.x2, next->x2) + extent;
    dirty.y2 = LV_MAX(s_interaction_visual_area.y2, next->y2) + extent;
    s_interaction_visual_area = *next;

    /* Normal LVGL invalidation is sufficient away from the hard page clip.
     * At the content/navigation seam, explicitly redraw only the narrow union
     * crossed by the floating Tile body; the inset edit chrome itself can no
     * longer escape the Tile. */
    if (dirty.y2 < viewport.y2 - extent * 2) return;
    screen = lv_obj_get_screen(s_container);
    if (object_valid(screen)) lv_obj_get_coords(screen, &screen_area);
    else screen_area = viewport;
    if (dirty.x1 < screen_area.x1) dirty.x1 = screen_area.x1;
    if (dirty.x2 > screen_area.x2) dirty.x2 = screen_area.x2;
    if (dirty.y1 < viewport.y2 - extent * 2)
        dirty.y1 = viewport.y2 - extent * 2;
    if (dirty.y2 > viewport.y2 + extent)
        dirty.y2 = viewport.y2 + extent;
    if (dirty.y2 > screen_area.y2) dirty.y2 = screen_area.y2;
    if (dirty.x1 > dirty.x2 || dirty.y1 > dirty.y2) return;
    lv_obj_invalidate_area(object_valid(screen) ? screen : s_container, &dirty);
}

static void desktop_extent_set_rows(uint8_t rows, bool edit_clearance)
{
    lv_area_t content_area;
    int32_t bottom;
    int32_t maximum_scroll;
    int32_t current_scroll;
    int32_t clearance = 0;
    if (!object_valid(s_container)) return;
    if (rows < 1U) rows = 1U;
    if (rows > FT_TILE_GRID_ROWS) rows = FT_TILE_GRID_ROWS;
    if (!object_valid(s_scroll_extent))
    {
        s_scroll_extent = lv_obj_create(s_container);
        lv_obj_set_size(s_scroll_extent, 1, 1);
        lv_obj_set_style_bg_opa(s_scroll_extent, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_scroll_extent, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_scroll_extent, 0, LV_PART_MAIN);
        lv_obj_remove_flag(s_scroll_extent,
                           LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    if (edit_clearance)
    {
        clearance = ft_layout_px(FT_TILE_HANDLE_SIZE) / 2 +
                    ft_layout_px(FT_TILE_EDIT_BORDER) + 4;
        if (clearance < 16) clearance = 16;
    }
    bottom = grid_y(rows - 1U) + tile_height(1U) - 1 + clearance;
    lv_obj_set_pos(s_scroll_extent, 0, bottom);
    lv_obj_move_background(s_scroll_extent);
    lv_obj_update_layout(s_container);

    /* Shrinking the temporary editing workspace must also bring an old deep
     * scroll position back inside the real content extent. */
    lv_obj_get_content_coords(s_container, &content_area);
    maximum_scroll = bottom + 1 - lv_area_get_height(&content_area);
    if (maximum_scroll < 0) maximum_scroll = 0;
    current_scroll = lv_obj_get_scroll_y(s_container);
    if (current_scroll > maximum_scroll)
        lv_obj_scroll_to_y(s_container, maximum_scroll, LV_ANIM_OFF);
}

static void desktop_extent_refresh(uint8_t content_rows)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_area_t content_area;
    uint8_t visible_rows = 1U;
    uint8_t rows = FT_TILE_DESKTOP_MIN_ROWS;
    if (!object_valid(s_container)) return;
    lv_obj_get_content_coords(s_container, &content_area);
    if (layout->tile_height + layout->tile_gap > 0)
        visible_rows = span_that_fits(lv_area_get_height(&content_area),
                                      layout->tile_height, layout->tile_gap,
                                      FT_TILE_GRID_ROWS);
    if ((uint16_t)visible_rows + FT_TILE_DESKTOP_TAIL_ROWS > rows)
        rows = (uint8_t)((uint16_t)visible_rows + FT_TILE_DESKTOP_TAIL_ROWS);
    if ((uint16_t)content_rows + FT_TILE_DESKTOP_TAIL_ROWS > rows)
        rows = (uint8_t)((uint16_t)content_rows + FT_TILE_DESKTOP_TAIL_ROWS);
    if (rows > FT_TILE_GRID_ROWS) rows = FT_TILE_GRID_ROWS;
    s_desktop_rows = rows;
    /* Outside edit mode the page scrolls only as far as actual Tiles.  The
     * larger logical workspace becomes physical only while editing. */
    desktop_extent_set_rows(content_rows, false);
}

static void tile_scroll_handles_into_view(const ft_start_tile_runtime_t *tile)
{
    lv_area_t viewport;
    lv_area_t tile_area;
    int32_t clearance;
    int32_t target;
    if (tile == RT_NULL || !object_valid(tile->object) ||
        !object_valid(s_container)) return;
    lv_obj_update_layout(s_container);
    lv_obj_get_content_coords(s_container, &viewport);
    lv_obj_get_coords(tile->object, &tile_area);
    clearance = ft_layout_px(FT_TILE_HANDLE_SIZE) / 2 +
                ft_layout_px(FT_TILE_EDIT_BORDER) + 4;
    if (clearance < 16) clearance = 16;
    target = lv_obj_get_scroll_y(s_container);
    if (tile_area.y2 + clearance > viewport.y2)
        target += tile_area.y2 + clearance - viewport.y2;
    else if (tile_area.y1 - clearance < viewport.y1)
        target -= viewport.y1 - (tile_area.y1 - clearance);
    if (target < 0) target = 0;
    if (target != lv_obj_get_scroll_y(s_container))
    {
        lv_obj_scroll_to_y(s_container, target, LV_ANIM_OFF);
        lv_obj_update_layout(s_container);
    }
}

static int32_t tile_auto_scroll(int32_t pointer_y)
{
    lv_area_t viewport;
    int32_t edge;
    int32_t step;
    int32_t before;
    int32_t target;
    if (!object_valid(s_container)) return 0;
    lv_obj_get_content_coords(s_container, &viewport);
    edge = ft_layout_px(FT_TILE_AUTO_SCROLL_EDGE);
    step = ft_layout_px(FT_TILE_AUTO_SCROLL_STEP);
    if (edge < 28) edge = 28;
    if (edge > lv_area_get_height(&viewport) / 3)
        edge = lv_area_get_height(&viewport) / 3;
    if (step < 8) step = 8;
    before = lv_obj_get_scroll_y(s_container);
    target = before;
    if (pointer_y <= viewport.y1 + edge && before > 0)
        target = before > step ? before - step : 0;
    else if (pointer_y >= viewport.y2 - edge &&
             lv_obj_get_scroll_bottom(s_container) > 0)
        target = before + step;
    if (target == before) return 0;
    lv_obj_scroll_to_y(s_container, target, LV_ANIM_OFF);
    return lv_obj_get_scroll_y(s_container) - before;
}

static void tile_repair_viewport(void)
{
    lv_obj_t *page;
    if (!object_valid(s_container)) return;
    page = lv_obj_get_parent(s_container);
    /* One parent redraw after a transaction clears pixels exposed at the hard
     * content/navigation clip.  This is deliberately not run per pointer or
     * animation frame. */
    lv_obj_invalidate(object_valid(page) ? page : s_container);
    tile_repair_seam();
}

static void tile_snap_anim_completed_cb(lv_anim_t *animation)
{
    LV_UNUSED(animation);
    tile_repair_seam();
}

static bool grid_rects_overlap(uint8_t column_a, uint8_t row_a,
                               uint8_t columns_a, uint8_t rows_a,
                               uint8_t column_b, uint8_t row_b,
                               uint8_t columns_b, uint8_t rows_b)
{
    return column_a < column_b + columns_b && column_b < column_a + columns_a &&
           row_a < row_b + rows_b && row_b < row_a + rows_a;
}

static bool grid_region_free(const uint32_t *occupied, uint8_t column, uint8_t row,
                             uint8_t columns, uint8_t rows)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    uint32_t mask;
    uint8_t y;
    if (occupied == RT_NULL || columns == 0U || rows == 0U ||
        column + columns > layout->tile_columns ||
        row + rows > desktop_row_limit()) return false;
    mask = ((1UL << columns) - 1UL) << column;
    for (y = row; y < row + rows; y++)
        if ((occupied[y] & mask) != 0U) return false;
    return true;
}

static void grid_region_use(uint32_t *occupied, uint8_t column, uint8_t row,
                            uint8_t columns, uint8_t rows)
{
    uint32_t mask = ((1UL << columns) - 1UL) << column;
    uint8_t y;
    for (y = row; y < row + rows && y < FT_TILE_GRID_ROWS; y++)
        occupied[y] |= mask;
}

static bool grid_find_nearest_free(const uint32_t *occupied,
                                   uint8_t preferred_column, uint8_t preferred_row,
                                   uint8_t columns, uint8_t rows,
                                   uint8_t *result_column, uint8_t *result_row)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    int64_t best_distance = INT64_MAX;
    uint8_t row;
    uint8_t column;
    bool found = false;
    if (result_column == RT_NULL || result_row == RT_NULL) return false;
    for (row = 0U; row + rows <= desktop_row_limit(); row++)
    {
        for (column = 0U; column + columns <= layout->tile_columns; column++)
        {
            int64_t dx;
            int64_t dy;
            int64_t distance;
            if (!grid_region_free(occupied, column, row, columns, rows)) continue;
            dx = (int64_t)grid_x(column) - grid_x(preferred_column);
            dy = (int64_t)grid_y(row) - grid_y(preferred_row);
            distance = dx * dx + dy * dy;
            if (distance < best_distance)
            {
                best_distance = distance;
                *result_column = column;
                *result_row = row;
                found = true;
            }
        }
    }
    return found;
}

static void tile_snap_to_grid(ft_start_tile_runtime_t *tile,
                              uint8_t column, uint8_t row, bool animate)
{
    lv_anim_t animation;
    int32_t old_visual_x;
    int32_t old_visual_y;
    int32_t new_x;
    int32_t new_y;
    int32_t translate_x;
    int32_t translate_y;
    if (tile == RT_NULL || !object_valid(tile->object)) return;
    /* grid_column/grid_row are the animation destination.  Reapplying the
     * same destination on every pointer sample used to delete and restart the
     * two translate animations, producing both visible stalls and excessive
     * dirty rectangles.  Let an existing snap finish untouched. */
    if (animate && tile->grid_column == column && tile->grid_row == row) return;
    /* Object coordinates already include style translation.  Counting it a
     * second time makes a retargeted snap jump twice as far. */
    old_visual_x = lv_obj_get_style_x(tile->object, LV_PART_MAIN) +
                   lv_obj_get_style_translate_x(tile->object, LV_PART_MAIN);
    old_visual_y = lv_obj_get_style_y(tile->object, LV_PART_MAIN) +
                   lv_obj_get_style_translate_y(tile->object, LV_PART_MAIN);
    lv_anim_delete(tile->object, tile_translate_x_anim_cb);
    lv_anim_delete(tile->object, tile_translate_y_anim_cb);
    new_x = grid_object_x(tile, column);
    new_y = grid_object_y(tile, row);
    tile->grid_column = column;
    tile->grid_row = row;
    lv_obj_set_pos(tile->object, new_x, new_y);
    translate_x = old_visual_x - new_x;
    translate_y = old_visual_y - new_y;
    if (!animate || (translate_x == 0 && translate_y == 0))
    {
        lv_obj_set_style_translate_x(tile->object, 0, LV_PART_MAIN);
        lv_obj_set_style_translate_y(tile->object, 0, LV_PART_MAIN);
        return;
    }
    lv_obj_set_style_translate_x(tile->object, translate_x, LV_PART_MAIN);
    lv_obj_set_style_translate_y(tile->object, translate_y, LV_PART_MAIN);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, tile->object);
    lv_anim_set_exec_cb(&animation, tile_translate_x_anim_cb);
    lv_anim_set_values(&animation, translate_x, 0);
    lv_anim_set_duration(&animation, FT_TILE_SNAP_DURATION_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    (void)lv_anim_start(&animation);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, tile->object);
    lv_anim_set_exec_cb(&animation, tile_translate_y_anim_cb);
    lv_anim_set_values(&animation, translate_y, 0);
    lv_anim_set_duration(&animation, FT_TILE_SNAP_DURATION_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, tile_snap_anim_completed_cb);
    (void)lv_anim_start(&animation);
}

static void tile_animation_stop(ft_start_tile_runtime_t *tile)
{
    if (tile == RT_NULL || !object_valid(tile->body)) return;
    lv_anim_delete(tile->body, tile_scale_anim_cb);
    tile_scale_anim_cb(tile->body, 256);
}

static void tile_animation_start(ft_start_tile_runtime_t *tile)
{
    lv_anim_t animation;
    if (tile == RT_NULL || !object_valid(tile->object) ||
        !object_valid(tile->body)) return;
    tile_animation_stop(tile);
    lv_obj_update_layout(tile->object);
    lv_obj_set_style_transform_pivot_x(tile->body,
                                       lv_obj_get_width(tile->body) / 2,
                                       LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(tile->body,
                                       lv_obj_get_height(tile->body) / 2,
                                       LV_PART_MAIN);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, tile->body);
    lv_anim_set_exec_cb(&animation, tile_scale_anim_cb);
    lv_anim_set_values(&animation, FT_TILE_EDIT_SCALE, 256);
    lv_anim_set_duration(&animation, 360U);
    lv_anim_set_playback_duration(&animation, 360U);
    lv_anim_set_repeat_delay(&animation, 90U);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    (void)lv_anim_start(&animation);
}

static void tile_handles_set_visible(ft_start_tile_runtime_t *tile, bool visible)
{
    size_t i;
    if (tile == RT_NULL) return;
    if (object_valid(tile->object)) lv_obj_invalidate(tile->object);
    if (object_valid(tile->body)) lv_obj_move_to_index(tile->body, 0);
    for (i = 0U; i < FT_TILE_HANDLE_COUNT; i++)
    {
        if (!object_valid(tile->handles[i])) continue;
        if (visible)
        {
            lv_obj_remove_flag(tile->handles[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_to_index(tile->handles[i], -1);
        }
        else lv_obj_add_flag(tile->handles[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (object_valid(tile->object)) lv_obj_invalidate(tile->object);
}

static void update_pattern(ft_start_tile_runtime_t *tile)
{
    if (tile == RT_NULL || !object_valid(tile->body)) return;
    if (tile->pattern_icon >= FT_ICON_COUNT)
    {
        if (object_valid(tile->pattern)) lv_obj_delete(tile->pattern);
        tile->pattern = RT_NULL;
        return;
    }
    if (!object_valid(tile->pattern))
    {
        tile->pattern = ft_icon_create(tile->body, tile->pattern_icon,
                                       ft_layout_icon_size(48U), false);
        lv_obj_set_style_image_opa(tile->pattern, LV_OPA_20, LV_PART_MAIN);
    }
    else
    {
        ft_icon_set(tile->pattern, tile->pattern_icon, ft_layout_icon_size(48U));
        lv_obj_remove_flag(tile->pattern, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(tile->pattern, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_move_background(tile->pattern);
}

static void update_geometry(ft_start_tile_runtime_t *tile,
                            uint8_t columns, uint8_t rows)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    int32_t width;
    int32_t height;
    if (tile == RT_NULL || !object_valid(tile->object)) return;
    tile->column_span = clamp_u8(columns, 1U, layout->tile_columns);
    tile->row_span = clamp_u8(rows, 1U, FT_TILE_MAX_ROW_SPAN);
    width = tile_width(tile->column_span);
    height = tile_height(tile->row_span);
    lv_obj_set_size(tile->object, width, height);
    lv_obj_set_style_transform_pivot_x(tile->object, width / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(tile->object, height / 2, LV_PART_MAIN);
    if (object_valid(tile->body))
    {
        lv_obj_set_size(tile->body, width, height);
        lv_obj_align(tile->body, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_transform_pivot_x(tile->body, width / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(tile->body, height / 2, LV_PART_MAIN);
    }
    if (object_valid(tile->live_host))
    {
        int32_t host_height = height - ft_layout_px(46);
        if (host_height < ft_layout_px(20)) host_height = ft_layout_px(20);
        lv_obj_set_size(tile->live_host, width - ft_layout_px(24), host_height);
    }
    update_pattern(tile);
    if (object_valid(s_container)) lv_obj_update_layout(s_container);
}

static void tile_visual_rect_apply(ft_start_tile_runtime_t *tile,
                                   int32_t x, int32_t y,
                                   int32_t width, int32_t height)
{
    if (tile == RT_NULL || !object_valid(tile->object) ||
        !object_valid(tile->body)) return;
    lv_obj_set_size(tile->object, width, height);
    lv_obj_set_pos(tile->object, x, y);
    lv_obj_set_style_transform_pivot_x(tile->object, width / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(tile->object, height / 2, LV_PART_MAIN);
    lv_obj_set_size(tile->body, width, height);
    lv_obj_align(tile->body, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_transform_pivot_x(tile->body, width / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(tile->body, height / 2, LV_PART_MAIN);
    if (object_valid(tile->live_host))
    {
        int32_t host_height = height - ft_layout_px(46);
        if (host_height < ft_layout_px(20)) host_height = ft_layout_px(20);
        lv_obj_set_size(tile->live_host, width - ft_layout_px(24), host_height);
    }
}

static int32_t settle_lerp(int32_t from, int32_t to, int32_t progress)
{
    return from + (int32_t)(((int64_t)(to - from) * progress + 128) / 256);
}

static void resize_settle_anim_cb(void *object, int32_t progress)
{
    ft_start_tile_runtime_t *tile = s_resize_settle_tile;
    if (tile == RT_NULL || !object_valid(tile->object) ||
        tile->object != (lv_obj_t *)object) return;
    tile_visual_rect_apply(tile,
        settle_lerp(s_resize_settle_from_x, s_resize_settle_to_x, progress),
        settle_lerp(s_resize_settle_from_y, s_resize_settle_to_y, progress),
        settle_lerp(s_resize_settle_from_width, s_resize_settle_to_width, progress),
        settle_lerp(s_resize_settle_from_height, s_resize_settle_to_height, progress));
}

static void resize_settle_completed_cb(lv_anim_t *animation)
{
    ft_start_tile_runtime_t *tile = s_resize_settle_tile;
    LV_UNUSED(animation);
    if (tile == RT_NULL) return;
    tile_visual_rect_apply(tile, s_resize_settle_to_x, s_resize_settle_to_y,
                           s_resize_settle_to_width, s_resize_settle_to_height);
    s_resize_settle_tile = RT_NULL;
    update_pattern(tile);
    if (tile == s_selected && s_interaction == FT_TILE_INTERACTION_NONE)
        tile_animation_start(tile);
    tile_repair_viewport();
}

static void resize_settle_complete_now(ft_start_tile_runtime_t *tile)
{
    if (tile == RT_NULL || s_resize_settle_tile != tile ||
        !object_valid(tile->object)) return;
    lv_anim_delete(tile->object, resize_settle_anim_cb);
    tile_visual_rect_apply(tile, s_resize_settle_to_x, s_resize_settle_to_y,
                           s_resize_settle_to_width, s_resize_settle_to_height);
    s_resize_settle_tile = RT_NULL;
    update_pattern(tile);
}

static void resize_settle_start(ft_start_tile_runtime_t *tile)
{
    lv_anim_t animation;
    if (tile == RT_NULL || !object_valid(tile->object) ||
        !object_valid(s_placeholder)) return;
    resize_settle_complete_now(tile);
    s_resize_settle_from_x = lv_obj_get_style_x(tile->object, LV_PART_MAIN);
    s_resize_settle_from_y = lv_obj_get_style_y(tile->object, LV_PART_MAIN);
    s_resize_settle_from_width = lv_obj_get_width(tile->object);
    s_resize_settle_from_height = lv_obj_get_height(tile->object);
    s_resize_settle_to_x = grid_object_x(tile, tile->grid_column);
    s_resize_settle_to_y = grid_object_y(tile, tile->grid_row);
    s_resize_settle_to_width = tile_width(tile->column_span);
    s_resize_settle_to_height = tile_height(tile->row_span);
    lv_obj_set_size(s_placeholder, s_resize_settle_to_width,
                    s_resize_settle_to_height);
    lv_obj_set_pos(s_placeholder, grid_x(tile->grid_column),
                   grid_y(tile->grid_row));
    lv_obj_add_flag(tile->object, LV_OBJ_FLAG_FLOATING);
    lv_obj_move_to_index(tile->object, -1);
    if (s_resize_settle_from_x == s_resize_settle_to_x &&
        s_resize_settle_from_y == s_resize_settle_to_y &&
        s_resize_settle_from_width == s_resize_settle_to_width &&
        s_resize_settle_from_height == s_resize_settle_to_height)
    {
        tile_visual_rect_apply(tile, s_resize_settle_to_x, s_resize_settle_to_y,
                               s_resize_settle_to_width, s_resize_settle_to_height);
        update_pattern(tile);
        tile_animation_start(tile);
        tile_repair_viewport();
        return;
    }
    s_resize_settle_tile = tile;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, tile->object);
    lv_anim_set_exec_cb(&animation, resize_settle_anim_cb);
    lv_anim_set_values(&animation, 0, 256);
    lv_anim_set_duration(&animation, FT_TILE_SNAP_DURATION_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, resize_settle_completed_cb);
    (void)lv_anim_start(&animation);
}

static uint8_t effective_opacity(const ft_start_tile_runtime_t *tile,
                                 uint8_t global_opacity)
{
    uint32_t product;
    if (tile == RT_NULL) return global_opacity;
    product = (uint32_t)tile->opacity * (uint32_t)global_opacity;
    return (uint8_t)((product + 127U) / 255U);
}

void ft_tiles_apply_opacity(uint8_t global_opacity)
{
    size_t i;
    for (i = 0U; i < s_tile_count; i++)
        if (object_valid(s_tiles[i].body))
            lv_obj_set_style_bg_opa(s_tiles[i].body,
                                    effective_opacity(&s_tiles[i], global_opacity),
                                    LV_PART_MAIN);
}

static void tile_raise_for_edit(ft_start_tile_runtime_t *tile)
{
    if (tile == RT_NULL || !object_valid(tile->object) || !object_valid(s_container))
        return;
    if (object_valid(s_placeholder))
    {
        lv_obj_add_flag(tile->object, LV_OBJ_FLAG_FLOATING);
        lv_obj_move_to_index(tile->object, -1);
        return;
    }

    lv_obj_update_layout(s_container);
    s_placeholder = lv_obj_create(s_container);
    lv_obj_set_size(s_placeholder, lv_obj_get_width(tile->object),
                    lv_obj_get_height(tile->object));
    lv_obj_set_style_bg_color(s_placeholder, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_placeholder, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_placeholder, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_placeholder, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_placeholder, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_placeholder, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_placeholder, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_placeholder, grid_x(tile->grid_column), grid_y(tile->grid_row));

    /* The explicit slot model keeps the placeholder at the selected Tile's
     * grid rectangle while the floating last child remains above siblings. */
    lv_anim_delete(tile->object, tile_translate_x_anim_cb);
    lv_anim_delete(tile->object, tile_translate_y_anim_cb);
    lv_obj_set_style_translate_x(tile->object, 0, LV_PART_MAIN);
    lv_obj_set_style_translate_y(tile->object, 0, LV_PART_MAIN);
    lv_obj_add_flag(tile->object, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(tile->object, grid_object_x(tile, tile->grid_column),
                   grid_object_y(tile, tile->grid_row));
    lv_obj_move_to_index(tile->object, -1);
    lv_obj_update_layout(s_container);
}

static void tile_edit_leave(void)
{
    if (s_selected != RT_NULL)
    {
        resize_settle_complete_now(s_selected);
        tile_animation_stop(s_selected);
        tile_handles_set_visible(s_selected, false);
        if (object_valid(s_selected->body))
            lv_obj_set_style_border_width(s_selected->body, 0, LV_PART_MAIN);
        if (object_valid(s_placeholder)) lv_obj_delete(s_placeholder);
        s_placeholder = RT_NULL;
        if (object_valid(s_selected->object))
        {
            lv_obj_remove_flag(s_selected->object, LV_OBJ_FLAG_FLOATING);
            tile_snap_to_grid(s_selected, s_selected->grid_column,
                              s_selected->grid_row, false);
        }
        if (object_valid(s_container))
        {
            desktop_extent_set_rows(desktop_content_rows(), false);
            lv_obj_update_layout(s_container);
        }
        tile_repair_viewport();
    }
    if (object_valid(s_container) && s_container_was_scrollable)
        lv_obj_add_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    if (object_valid(s_tileview) && s_tileview_was_scrollable)
        lv_obj_add_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE);
    if (object_valid(s_container) && s_container_scrollbar_saved)
        lv_obj_set_scrollbar_mode(s_container, s_container_scrollbar_mode);
    s_container_scrollbar_saved = false;
    s_selected = RT_NULL;
    s_interaction = FT_TILE_INTERACTION_NONE;
    s_interaction_visual_valid = false;
    s_move_base_valid = false;
    s_move_snap_confirmed = false;
}

void ft_tiles_exit_edit(void)
{
    tile_edit_leave();
}

static void tile_edit_enter(ft_start_tile_runtime_t *tile)
{
    if (tile == RT_NULL || !object_valid(tile->object)) return;
    if (s_selected == tile) return;
    tile_edit_leave();
    s_selected = tile;
    s_container_was_scrollable = object_valid(s_container) &&
                                 lv_obj_has_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    s_container_scrollbar_saved = false;
    if (object_valid(s_container))
    {
        /* The selected Tile's right handles share the desktop edge with its
         * scrollbar.  Keep the lane single-owner while editing; programmatic
         * edge auto-scroll remains available with the bar hidden. */
        s_container_scrollbar_mode = lv_obj_get_scrollbar_mode(s_container);
        s_container_scrollbar_saved = true;
        lv_obj_set_scrollbar_mode(s_container, LV_SCROLLBAR_MODE_OFF);
        /* Editing exposes a larger logical desktop and reserves enough tail
         * space for a complete bottom handle.  This makes the viewport edge
         * an auto-scroll trigger instead of a resize/move dead end. */
        desktop_extent_set_rows(desktop_row_limit(), true);
        tile_scroll_handles_into_view(tile);
        lv_obj_remove_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    }
    s_tileview = object_valid(s_container) ?
                 lv_obj_get_parent(lv_obj_get_parent(s_container)) : RT_NULL;
    s_tileview_was_scrollable = object_valid(s_tileview) &&
                                lv_obj_has_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE);
    if (object_valid(s_tileview)) lv_obj_remove_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(tile->body, ft_layout_px(FT_TILE_EDIT_BORDER),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_color(tile->body, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(tile->body, LV_OPA_80, LV_PART_MAIN);
    tile_raise_for_edit(tile);
    tile_handles_set_visible(tile, true);
    s_scale_redraw_count = 0U;
    tile_animation_start(tile);
}

static size_t grid_order_of_tile(const ft_start_tile_runtime_t *tile)
{
    size_t order = 0U;
    size_t i;
    if (tile == RT_NULL) return SIZE_MAX;
    for (i = 0U; i < s_tile_count; i++)
    {
        if (&s_tiles[i] == tile) continue;
        if (s_tiles[i].grid_row < tile->grid_row ||
            (s_tiles[i].grid_row == tile->grid_row &&
             s_tiles[i].grid_column < tile->grid_column)) order++;
    }
    return order;
}

static size_t grid_resolve_reservation(uint8_t reserve_column, uint8_t reserve_row,
                                       uint8_t reserve_columns, uint8_t reserve_rows,
                                       const uint8_t *preferred_column,
                                       const uint8_t *preferred_row, bool animate)
{
    uint32_t occupied[FT_TILE_GRID_ROWS] = {0U};
    bool displaced[FT_TILE_MAX_APPS] = {false};
    size_t displaced_count = 0U;
    size_t i;
    if (s_selected == RT_NULL || !object_valid(s_placeholder) ||
        !grid_region_free(occupied, reserve_column, reserve_row,
                          reserve_columns, reserve_rows)) return SIZE_MAX;
    grid_region_use(occupied, reserve_column, reserve_row,
                    reserve_columns, reserve_rows);

    /* Tiles whose preferred cells remain free are immovable obstacles.  Only
     * a Tile actually covered by the selected footprint enters relocation. */
    for (i = 0U; i < s_tile_count; i++)
    {
        uint8_t column;
        uint8_t row;
        if (&s_tiles[i] == s_selected) continue;
        column = preferred_column != RT_NULL ? preferred_column[i] : s_tiles[i].grid_column;
        row = preferred_row != RT_NULL ? preferred_row[i] : s_tiles[i].grid_row;
        if (grid_region_free(occupied, column, row,
                             s_tiles[i].column_span, s_tiles[i].row_span))
            grid_region_use(occupied, column, row,
                            s_tiles[i].column_span, s_tiles[i].row_span);
        else
        {
            displaced[i] = true;
            displaced_count++;
        }
    }

    for (i = 0U; i < s_tile_count; i++)
    {
        uint8_t preferred_x;
        uint8_t preferred_y;
        uint8_t target_x;
        uint8_t target_y;
        if (&s_tiles[i] == s_selected || !displaced[i]) continue;
        preferred_x = preferred_column != RT_NULL ?
                      preferred_column[i] : s_tiles[i].grid_column;
        preferred_y = preferred_row != RT_NULL ?
                      preferred_row[i] : s_tiles[i].grid_row;
        if (!grid_find_nearest_free(occupied, preferred_x, preferred_y,
                                    s_tiles[i].column_span, s_tiles[i].row_span,
                                    &target_x, &target_y)) return SIZE_MAX;
        grid_region_use(occupied, target_x, target_y,
                        s_tiles[i].column_span, s_tiles[i].row_span);
        tile_snap_to_grid(&s_tiles[i], target_x, target_y, animate);
    }
    for (i = 0U; i < s_tile_count; i++)
    {
        uint8_t column;
        uint8_t row;
        if (&s_tiles[i] == s_selected || displaced[i]) continue;
        column = preferred_column != RT_NULL ? preferred_column[i] : s_tiles[i].grid_column;
        row = preferred_row != RT_NULL ? preferred_row[i] : s_tiles[i].grid_row;
        tile_snap_to_grid(&s_tiles[i], column, row, animate);
    }
    lv_obj_set_pos(s_placeholder, grid_x(reserve_column), grid_y(reserve_row));
    lv_obj_set_size(s_placeholder, tile_width(reserve_columns), tile_height(reserve_rows));
    return displaced_count;
}

static void move_restore_siblings(bool animate)
{
    size_t i;
    if (!s_move_base_valid || s_selected == RT_NULL) return;
    for (i = 0U; i < s_tile_count; i++)
        if (&s_tiles[i] != s_selected)
            tile_snap_to_grid(&s_tiles[i], s_move_base_column[i],
                              s_move_base_row[i], animate);
}

static size_t placeholder_snap_nearest(const lv_point_t *desired_center, bool force)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_area_t content_area;
    uint8_t maximum_row;
    uint8_t row;
    uint8_t column;
    uint8_t best_row = s_selected != RT_NULL ? s_selected->grid_row : 0U;
    uint8_t best_column = s_selected != RT_NULL ? s_selected->grid_column : 0U;
    int64_t best_distance = INT64_MAX;
    int32_t origin_x;
    int32_t origin_y;
    int32_t best_dx = INT32_MAX;
    int32_t best_dy = INT32_MAX;
    bool clamped_vertical_edge;
    if (desired_center == RT_NULL || !object_valid(s_placeholder) ||
        s_selected == RT_NULL || !object_valid(s_container)) return SIZE_MAX;
    lv_obj_get_content_coords(s_container, &content_area);
    origin_x = content_area.x1 - lv_obj_get_scroll_x(s_container);
    origin_y = content_area.y1 - lv_obj_get_scroll_y(s_container);
    maximum_row = desktop_row_limit() > s_selected->row_span ?
                  desktop_row_limit() - s_selected->row_span : 0U;

    for (row = 0U; row <= maximum_row; row++)
    {
        for (column = 0U;
             column + s_selected->column_span <= layout->tile_columns;
             column++)
        {
            int64_t dx = (int64_t)desired_center->x -
                         (origin_x + grid_x(column) +
                          tile_width(s_selected->column_span) / 2);
            int64_t dy = (int64_t)desired_center->y -
                         (origin_y + grid_y(row) +
                          tile_height(s_selected->row_span) / 2);
            int64_t distance = dx * dx + dy * dy;
            if (distance < best_distance)
            {
                best_distance = distance;
                best_column = column;
                best_row = row;
                best_dx = (int32_t)(dx < 0 ? -dx : dx);
                best_dy = (int32_t)(dy < 0 ? -dy : dy);
            }
        }
    }
    if (best_distance == INT64_MAX) return SIZE_MAX;
    clamped_vertical_edge =
        (desired_center->y >= content_area.y2 - ft_layout_px(FT_TILE_AUTO_SCROLL_EDGE) &&
         lv_obj_get_scroll_bottom(s_container) <= 0) ||
        (desired_center->y <= content_area.y1 + ft_layout_px(FT_TILE_AUTO_SCROLL_EDGE) &&
         lv_obj_get_scroll_y(s_container) <= 0);
    if (!force &&
        (best_dx > (layout->tile_column_width + layout->tile_gap) / 3 ||
         (!clamped_vertical_edge &&
          best_dy > (layout->tile_height + layout->tile_gap) / 3)))
    {
        size_t selected_index = (size_t)(s_selected - s_tiles);
        if (s_move_snap_confirmed) move_restore_siblings(true);
        if (s_move_base_valid && selected_index < s_tile_count)
        {
            s_selected->grid_column = s_move_base_column[selected_index];
            s_selected->grid_row = s_move_base_row[selected_index];
        }
        s_move_snap_confirmed = false;
        lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
        return SIZE_MAX;
    }
    /* The pointer can generate many PRESSING events while it remains inside
     * one confirmed pit.  Geometry and sibling destinations are already
     * committed in that case, so there is no grid transaction to repeat. */
    if (s_move_snap_confirmed && s_selected->grid_column == best_column &&
        s_selected->grid_row == best_row)
    {
        lv_obj_remove_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
        return grid_order_of_tile(s_selected);
    }
    if (grid_resolve_reservation(best_column, best_row,
                                 s_selected->column_span, s_selected->row_span,
                                 s_move_base_valid ? s_move_base_column : RT_NULL,
                                 s_move_base_valid ? s_move_base_row : RT_NULL,
                                 true) == SIZE_MAX) return SIZE_MAX;
    lv_obj_remove_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    s_selected->grid_column = best_column;
    s_selected->grid_row = best_row;
    s_move_snap_confirmed = true;
    return grid_order_of_tile(s_selected);
}

static bool tile_settle_layout_for_edit(ft_start_tile_runtime_t *tile)
{
    if (tile == RT_NULL || tile != s_selected || !object_valid(tile->object) ||
        !object_valid(s_container) || !object_valid(s_placeholder)) return false;

    /* Every sibling already owns a committed grid slot.  Snap the selected
     * Tile and its reservation to the final slot, then retain the foreground
     * edit layer and breathing animation without compacting unrelated Tiles. */
    tile_animation_stop(tile);
    lv_obj_set_size(s_placeholder, tile_width(tile->column_span),
                    tile_height(tile->row_span));
    lv_obj_set_pos(s_placeholder, grid_x(tile->grid_column), grid_y(tile->grid_row));
    tile_visual_rect_apply(tile,
                           grid_object_x(tile, tile->grid_column),
                           grid_object_y(tile, tile->grid_row),
                           tile_width(tile->column_span),
                           tile_height(tile->row_span));
    lv_obj_add_flag(tile->object, LV_OBJ_FLAG_FLOATING);
    lv_obj_move_to_index(tile->object, -1);
    lv_obj_update_layout(s_container);
    tile_repair_viewport();
    return true;
}

static void move_begin(ft_start_tile_runtime_t *tile, const lv_point_t *point)
{
    lv_area_t tile_area;
    size_t i;
    if (tile == RT_NULL || !object_valid(tile->object) || !object_valid(s_container)) return;
    resize_settle_complete_now(tile);
    tile_animation_stop(tile);
    tile_raise_for_edit(tile);
    lv_obj_update_layout(s_container);
    /* The selected Tile is FLOATING. lv_obj_get_x/y() still adds the parent's
     * scroll offset, while lv_obj_set_pos() deliberately does not for a
     * FLOATING child.  Reading the local style position keeps both sides in
     * the same coordinate system and prevents the initial drag jump. */
    s_press_x = lv_obj_get_style_x(tile->object, LV_PART_MAIN);
    s_press_y = lv_obj_get_style_y(tile->object, LV_PART_MAIN);
    lv_obj_get_coords(tile->object, &tile_area);
    s_interaction_visual_area = tile_area;
    s_interaction_visual_valid = true;
    s_interaction_start_scroll_y = lv_obj_get_scroll_y(s_container);
    s_move_desired_center.x = (tile_area.x1 + tile_area.x2) / 2;
    s_move_desired_center.y = (tile_area.y1 + tile_area.y2) / 2;
    if (point != RT_NULL) s_press_point = *point;
    else
    {
        s_press_point.x = 0;
        s_press_point.y = 0;
    }
    for (i = 0U; i < s_tile_count; i++)
    {
        s_move_base_column[i] = s_tiles[i].grid_column;
        s_move_base_row[i] = s_tiles[i].grid_row;
    }
    s_move_base_valid = true;
    s_move_snap_confirmed = false;
    if (object_valid(s_placeholder)) lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    s_interaction = FT_TILE_INTERACTION_MOVE;
}

static void move_target_nearest_slot(const lv_point_t *desired_center, bool force)
{
    if (s_selected == RT_NULL || !object_valid(s_selected->object) ||
        !object_valid(s_placeholder) || desired_center == RT_NULL) return;
    /* Candidate selection uses the pointer-derived screen center directly.
     * It must not force a full LVGL layout merely to read back coordinates
     * that were written a few instructions earlier. */
    (void)placeholder_snap_nearest(desired_center, force);
}

static void move_update(const lv_point_t *point)
{
    lv_area_t content_area;
    lv_area_t next_area;
    lv_point_t desired_center;
    int32_t dx;
    int32_t dy;
    if (s_interaction != FT_TILE_INTERACTION_MOVE || s_selected == RT_NULL ||
        point == RT_NULL) return;
    (void)tile_auto_scroll(point->y);
    dx = point->x - s_press_point.x;
    dy = point->y - s_press_point.y;
    desired_center.x = s_move_desired_center.x + dx;
    desired_center.y = s_move_desired_center.y + dy;
    lv_obj_set_pos(s_selected->object,
                   s_press_x + dx, s_press_y + dy);
    lv_obj_get_content_coords(s_container, &content_area);
    next_area.x1 = content_area.x1 + s_press_x + dx;
    next_area.y1 = content_area.y1 + s_press_y + dy;
    next_area.x2 = next_area.x1 + lv_obj_get_width(s_selected->object) - 1;
    next_area.y2 = next_area.y1 + lv_obj_get_height(s_selected->object) - 1;
    tile_repair_interaction_transition(&next_area);
    move_target_nearest_slot(&desired_center, false);
}

static void move_finish(void)
{
    lv_area_t dragged_area;
    lv_point_t desired_center;
    if (s_interaction != FT_TILE_INTERACTION_MOVE || s_selected == RT_NULL) return;
    /* RELEASED does not carry a new coordinate on every input driver.  Use
     * the object's last committed visual center after one final layout only;
     * the high-frequency PRESSING path above remains layout-free. */
    lv_obj_update_layout(s_container);
    lv_obj_get_coords(s_selected->object, &dragged_area);
    desired_center.x = (dragged_area.x1 + dragged_area.x2) / 2;
    desired_center.y = (dragged_area.y1 + dragged_area.y2) / 2;
    move_target_nearest_slot(&desired_center, true);
    s_interaction = FT_TILE_INTERACTION_NONE;
    s_interaction_visual_valid = false;
    s_move_base_valid = false;
    s_move_snap_confirmed = false;
    (void)tile_settle_layout_for_edit(s_selected);
    tile_animation_start(s_selected);
}

static void resize_begin(ft_start_tile_runtime_t *tile,
                         ft_tile_handle_id_t handle, const lv_point_t *point)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_area_t tile_area;
    int32_t fixed_right;
    int32_t fixed_bottom;
    size_t i;
    if (tile == RT_NULL || point == RT_NULL || !object_valid(tile->object) ||
        !object_valid(s_container)) return;
    resize_settle_complete_now(tile);
    tile_animation_stop(tile);
    lv_obj_update_layout(s_container);
    lv_obj_get_coords(tile->object, &tile_area);
    fixed_right = (int32_t)tile->grid_column + tile->column_span;
    fixed_bottom = (int32_t)tile->grid_row + tile->row_span;
    /* The desktop is scrollable.  Resize limits belong to its logical grid,
     * never to the currently visible window.  Each inset corner Chevron owns
     * only its two adjacent edges. */
    s_resize_max_columns =
                           (handle == FT_TILE_HANDLE_RESIZE_TL ||
                            handle == FT_TILE_HANDLE_RESIZE_BL) ?
                           (uint8_t)fixed_right :
                           (uint8_t)(layout->tile_columns - tile->grid_column);
    s_resize_max_rows =
                        (handle == FT_TILE_HANDLE_RESIZE_TL ||
                         handle == FT_TILE_HANDLE_RESIZE_TR) ?
                        (uint8_t)fixed_bottom :
                        (uint8_t)(desktop_row_limit() - tile->grid_row);
    if (s_resize_max_rows > FT_TILE_MAX_ROW_SPAN)
        s_resize_max_rows = FT_TILE_MAX_ROW_SPAN;
    if (s_resize_max_columns < tile->column_span)
        s_resize_max_columns = tile->column_span;
    if (s_resize_max_rows < tile->row_span)
        s_resize_max_rows = tile->row_span;
    s_resize_max_width = tile_width(s_resize_max_columns);
    s_resize_max_height = tile_height(s_resize_max_rows);
    s_press_point = *point;
    s_press_x = lv_obj_get_style_x(tile->object, LV_PART_MAIN);
    s_press_y = lv_obj_get_style_y(tile->object, LV_PART_MAIN);
    s_press_width = lv_obj_get_width(tile->object);
    s_press_height = lv_obj_get_height(tile->object);
    s_interaction_start_scroll_y = lv_obj_get_scroll_y(s_container);
    s_interaction_visual_area = tile_area;
    s_interaction_visual_valid = true;
    s_resize_start_column = tile->grid_column;
    s_resize_start_row = tile->grid_row;
    s_resize_start_columns = tile->column_span;
    s_resize_start_rows = tile->row_span;
    for (i = 0U; i < s_tile_count; i++)
    {
        s_resize_base_column[i] = s_tiles[i].grid_column;
        s_resize_base_row[i] = s_tiles[i].grid_row;
    }
    s_last_resize_displaced = 0U;
    s_resize_last_width = s_press_width;
    s_resize_last_height = s_press_height;
    s_resize_reservation_valid = false;
    s_resize_handle = handle;
    s_interaction = FT_TILE_INTERACTION_RESIZE;
}

static void resize_apply_size(int32_t width, int32_t height)
{
    int32_t x = s_press_x;
    int32_t scroll_delta = object_valid(s_container) ?
                           lv_obj_get_scroll_y(s_container) -
                           s_interaction_start_scroll_y : 0;
    int32_t y = s_press_y - scroll_delta;
    if (s_selected == RT_NULL || !object_valid(s_selected->object)) return;

    /* TL fixes right+bottom, TR left+bottom, BL right+top, BR left+top. */
    if (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL ||
        s_resize_handle == FT_TILE_HANDLE_RESIZE_TR)
        y += s_press_height - height;
    if (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL ||
        s_resize_handle == FT_TILE_HANDLE_RESIZE_BL)
        x = s_press_x + s_press_width - width;

    tile_visual_rect_apply(s_selected, x, y, width, height);
}

static size_t resize_resolve_if_changed(uint8_t reserve_column,
                                        uint8_t reserve_row,
                                        uint8_t reserve_columns,
                                        uint8_t reserve_rows)
{
    size_t displaced;
    if (s_resize_reservation_valid &&
        s_resize_reserve_column == reserve_column &&
        s_resize_reserve_row == reserve_row &&
        s_resize_reserve_columns == reserve_columns &&
        s_resize_reserve_rows == reserve_rows)
        return s_last_resize_displaced;

    displaced = grid_resolve_reservation(reserve_column, reserve_row,
                                         reserve_columns, reserve_rows,
                                         s_resize_base_column,
                                         s_resize_base_row, true);
    if (displaced == SIZE_MAX) return displaced;
    s_resize_reserve_column = reserve_column;
    s_resize_reserve_row = reserve_row;
    s_resize_reserve_columns = reserve_columns;
    s_resize_reserve_rows = reserve_rows;
    s_resize_reservation_valid = true;
    s_last_resize_displaced = displaced;
    return displaced;
}

static void resize_update(const lv_point_t *point)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_area_t content_area;
    lv_area_t next_area;
    int32_t scroll_delta;
    int32_t dx;
    int32_t dy;
    int32_t width;
    int32_t height;
    uint8_t columns;
    uint8_t rows;
    uint8_t cover_columns;
    uint8_t cover_rows;
    int32_t reserve_column;
    int32_t reserve_row;
    int32_t final_column;
    int32_t final_row;
    int32_t fixed_right;
    int32_t fixed_bottom;
    if (s_interaction != FT_TILE_INTERACTION_RESIZE || s_selected == RT_NULL ||
        point == RT_NULL) return;
    (void)tile_auto_scroll(point->y);
    scroll_delta = lv_obj_get_scroll_y(s_container) - s_interaction_start_scroll_y;
    dx = point->x - s_press_point.x;
    dy = point->y - s_press_point.y + scroll_delta;
    width = s_press_width;
    height = s_press_height;
    if (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL)
    {
        width -= dx;
        height -= dy;
    }
    else if (s_resize_handle == FT_TILE_HANDLE_RESIZE_TR)
    {
        width += dx;
        height -= dy;
    }
    else if (s_resize_handle == FT_TILE_HANDLE_RESIZE_BL)
    {
        width -= dx;
        height += dy;
    }
    else
    {
        width += dx;
        height += dy;
    }
    if (width < tile_width(1U)) width = tile_width(1U);
    if (width > s_resize_max_width) width = s_resize_max_width;
    if (height < tile_height(1U)) height = tile_height(1U);
    if (height > s_resize_max_height) height = s_resize_max_height;
    /* Stationary input frames are common on this touch controller.  A frame
     * that does not change either pixel dimension has no work to perform. */
    if (width == s_resize_last_width && height == s_resize_last_height) return;
    s_resize_last_width = width;
    s_resize_last_height = height;
    columns = span_from_pixels(width, layout->tile_column_width,
                               layout->tile_gap, s_resize_max_columns);
    rows = span_from_pixels(height, layout->tile_height,
                            layout->tile_gap, s_resize_max_rows);
    cover_columns = span_covering_pixels(width, layout->tile_column_width,
                                         layout->tile_gap, s_resize_max_columns);
    cover_rows = span_covering_pixels(height, layout->tile_height,
                                      layout->tile_gap, s_resize_max_rows);
    fixed_right = (int32_t)s_resize_start_column + s_resize_start_columns;
    fixed_bottom = (int32_t)s_resize_start_row + s_resize_start_rows;
    reserve_column =
                     (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL ||
                      s_resize_handle == FT_TILE_HANDLE_RESIZE_BL) ?
                     fixed_right - cover_columns : s_resize_start_column;
    reserve_row =
                  (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL ||
                   s_resize_handle == FT_TILE_HANDLE_RESIZE_TR) ?
                   fixed_bottom - cover_rows : s_resize_start_row;
    final_column =
                   (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL ||
                    s_resize_handle == FT_TILE_HANDLE_RESIZE_BL) ?
                    fixed_right - columns : s_resize_start_column;
    final_row =
                (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL ||
                 s_resize_handle == FT_TILE_HANDLE_RESIZE_TR) ?
                 fixed_bottom - rows : s_resize_start_row;
    if (reserve_column < 0) reserve_column = 0;
    if (reserve_row < 0) reserve_row = 0;
    if (final_column < 0) final_column = 0;
    if (final_row < 0) final_row = 0;
    s_selected->column_span = columns;
    s_selected->row_span = rows;
    s_selected->grid_column = (uint8_t)final_column;
    s_selected->grid_row = (uint8_t)final_row;
    s_last_resize_displaced =
        resize_resolve_if_changed((uint8_t)reserve_column,
                                  (uint8_t)reserve_row,
                                  cover_columns, cover_rows);
    resize_apply_size(width, height);
    lv_obj_get_content_coords(s_container, &content_area);
    next_area.x1 = content_area.x1 +
                   lv_obj_get_style_x(s_selected->object, LV_PART_MAIN);
    next_area.y1 = content_area.y1 +
                   lv_obj_get_style_y(s_selected->object, LV_PART_MAIN);
    next_area.x2 = next_area.x1 + width - 1;
    next_area.y2 = next_area.y1 + height - 1;
    tile_repair_interaction_transition(&next_area);
}

static void resize_finish(void)
{
    int32_t final_column;
    int32_t final_row;
    int32_t fixed_right;
    int32_t fixed_bottom;
    if (s_interaction != FT_TILE_INTERACTION_RESIZE || s_selected == RT_NULL) return;
    fixed_right = (int32_t)s_resize_start_column + s_resize_start_columns;
    fixed_bottom = (int32_t)s_resize_start_row + s_resize_start_rows;
    final_column =
                   (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL ||
                    s_resize_handle == FT_TILE_HANDLE_RESIZE_BL) ?
                    fixed_right - s_selected->column_span : s_resize_start_column;
    final_row =
                (s_resize_handle == FT_TILE_HANDLE_RESIZE_TL ||
                 s_resize_handle == FT_TILE_HANDLE_RESIZE_TR) ?
                 fixed_bottom - s_selected->row_span : s_resize_start_row;
    if (final_column < 0) final_column = 0;
    if (final_row < 0) final_row = 0;
    s_selected->grid_column = (uint8_t)final_column;
    s_selected->grid_row = (uint8_t)final_row;
    s_last_resize_displaced =
        resize_resolve_if_changed(s_selected->grid_column,
                                  s_selected->grid_row,
                                  s_selected->column_span,
                                  s_selected->row_span);
    s_interaction = FT_TILE_INTERACTION_NONE;
    s_interaction_visual_valid = false;
    resize_settle_start(s_selected);
}

static bool event_point(lv_event_t *event, lv_point_t *point)
{
    lv_indev_t *indev;
    if (point == RT_NULL) return false;
    indev = lv_event_get_indev(event);
    if (indev == RT_NULL) indev = lv_indev_active();
    if (indev == RT_NULL) return false;
    lv_indev_get_point(indev, point);
    return true;
}

static void handle_event_cb(lv_event_t *event)
{
    ft_tile_handle_context_t *context = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;
    if (context == RT_NULL || context->tile == RT_NULL) return;
    if (code == LV_EVENT_PRESSED)
    {
        if (s_selected != context->tile) tile_edit_enter(context->tile);
        if (!event_point(event, &point)) return;
        resize_begin(context->tile, context->id, &point);
    }
    else if (code == LV_EVENT_PRESSING)
    {
        if (!event_point(event, &point)) return;
        if (s_interaction == FT_TILE_INTERACTION_RESIZE) resize_update(&point);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        if (s_interaction == FT_TILE_INTERACTION_RESIZE) resize_finish();
    }
}

static void draw_handle_segment(lv_layer_t *layer, lv_draw_line_dsc_t *dsc,
                                int32_t center_x, int32_t center_y,
                                int32_t x1, int32_t y1,
                                int32_t x2, int32_t y2)
{
    dsc->p1.x = center_x + x1;
    dsc->p1.y = center_y + y1;
    dsc->p2.x = center_x + x2;
    dsc->p2.y = center_y + y2;
    lv_draw_line(layer, dsc);
}

static void handle_draw_event_cb(lv_event_t *event)
{
    ft_tile_handle_context_t *context = lv_event_get_user_data(event);
    lv_obj_t *handle = lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_draw_line_dsc_t line;
    lv_area_t area;
    int32_t center_x;
    int32_t center_y;
    int32_t extent;
    int32_t arm;
    int32_t direction_x;
    int32_t direction_y;
    int32_t tip_x;
    int32_t tip_y;
    if (context == RT_NULL || !object_valid(handle) || layer == RT_NULL) return;
    lv_obj_get_coords(handle, &area);
    center_x = (area.x1 + area.x2) / 2;
    center_y = (area.y1 + area.y2) / 2;
    extent = lv_area_get_width(&area) / 4;
    if (extent < 6) extent = 6;
    arm = extent + ft_layout_px(2);
    if (arm < 8) arm = 8;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_white();
    line.opa = LV_OPA_COVER;
    line.width = ft_layout_px(2);
    if (line.width < 2) line.width = 2;
    line.round_start = 1;
    line.round_end = 1;

    direction_x = (context->id == FT_TILE_HANDLE_RESIZE_TL ||
                   context->id == FT_TILE_HANDLE_RESIZE_BL) ? -1 : 1;
    direction_y = (context->id == FT_TILE_HANDLE_RESIZE_TL ||
                   context->id == FT_TILE_HANDLE_RESIZE_TR) ? -1 : 1;
    tip_x = direction_x * extent;
    tip_y = direction_y * extent;

    /* Two perpendicular legs form an exact 90-degree corner Chevron. */
    draw_handle_segment(layer, &line, center_x, center_y,
                        tip_x - direction_x * arm, tip_y,
                        tip_x, tip_y);
    draw_handle_segment(layer, &line, center_x, center_y,
                        tip_x, tip_y,
                        tip_x, tip_y - direction_y * arm);
}

static lv_obj_t *create_handle(ft_start_tile_runtime_t *tile,
                               ft_tile_handle_id_t id)
{
    int32_t handle_size = ft_layout_px(FT_TILE_HANDLE_SIZE);
    lv_obj_t *handle;
    if (handle_size < 25) handle_size = 25;
    if (handle_size > 35) handle_size = 35;
    if ((handle_size & 1) == 0) handle_size++;
    handle = lv_obj_create(tile->object);
    lv_obj_set_size(handle, handle_size, handle_size);
    lv_obj_set_style_radius(handle, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(handle, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(handle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(handle, 0, LV_PART_MAIN);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(handle, ft_layout_px(FT_TILE_HANDLE_HIT_PAD));
    tile->handle_context[id].tile = tile;
    tile->handle_context[id].id = id;
    lv_obj_add_event_cb(handle, handle_event_cb, LV_EVENT_PRESSED,
                        &tile->handle_context[id]);
    lv_obj_add_event_cb(handle, handle_event_cb, LV_EVENT_PRESSING,
                        &tile->handle_context[id]);
    lv_obj_add_event_cb(handle, handle_event_cb, LV_EVENT_RELEASED,
                        &tile->handle_context[id]);
    lv_obj_add_event_cb(handle, handle_event_cb, LV_EVENT_PRESS_LOST,
                        &tile->handle_context[id]);
    lv_obj_add_event_cb(handle, handle_draw_event_cb, LV_EVENT_DRAW_MAIN_END,
                        &tile->handle_context[id]);
    if (id == FT_TILE_HANDLE_RESIZE_TL)
        lv_obj_align(handle, LV_ALIGN_TOP_LEFT, 0, 0);
    else if (id == FT_TILE_HANDLE_RESIZE_TR)
        lv_obj_align(handle, LV_ALIGN_TOP_RIGHT, 0, 0);
    else if (id == FT_TILE_HANDLE_RESIZE_BL)
        lv_obj_align(handle, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    else
        lv_obj_align(handle, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_HIDDEN);
    return handle;
}

static void tile_event_cb(lv_event_t *event)
{
    ft_start_tile_runtime_t *tile = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;
    if (tile == RT_NULL) return;
    if (code == LV_EVENT_LONG_PRESSED)
    {
        tile_edit_enter(tile);
        /* Movement belongs to the Tile body: keep holding after the long-press
         * threshold and drag directly.  Corner Chevrons are resize-only. */
        if (s_selected == tile && event_point(event, &point))
            move_begin(tile, &point);
    }
    else if (code == LV_EVENT_PRESSING)
    {
        if (s_interaction == FT_TILE_INTERACTION_MOVE &&
            s_selected == tile && event_point(event, &point))
            move_update(&point);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        if (s_interaction == FT_TILE_INTERACTION_MOVE && s_selected == tile)
            move_finish();
    }
    else if (code == LV_EVENT_SHORT_CLICKED)
    {
        if (s_selected != RT_NULL)
        {
            /* A short tap can leave edit mode, but only a long press can enter it. */
            tile_edit_leave();
            return;
        }
        (void)ft_router_push(tile->descriptor->page_id);
    }
}

static void container_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCROLL_END)
        tile_repair_seam();
    else if (code == LV_EVENT_CLICKED && s_selected != RT_NULL)
        tile_edit_leave();
    else if (code == LV_EVENT_DELETE)
    {
        if (s_live_timer != RT_NULL) lv_timer_delete(s_live_timer);
        s_live_timer = RT_NULL;
        s_container = RT_NULL;
        s_tileview = RT_NULL;
        s_scroll_extent = RT_NULL;
        s_selected = RT_NULL;
        s_placeholder = RT_NULL;
        s_resize_settle_tile = RT_NULL;
        s_tile_count = 0U;
        s_interaction = FT_TILE_INTERACTION_NONE;
        s_interaction_visual_valid = false;
        s_container_scrollbar_saved = false;
        s_scale_redraw_count = 0U;
    }
}

static void live_content_refresh(ft_start_tile_runtime_t *tile, bool advance)
{
    lv_obj_t *first_child;
    if (tile == RT_NULL || !object_valid(tile->live_host) ||
        tile->descriptor == RT_NULL || tile->descriptor->app.live_content == RT_NULL)
        return;
    if (advance) tile->live_frame++;
    tile->descriptor->app.live_content(tile->live_host, tile->live_frame,
                                       tile->descriptor->app.live_context);
    first_child = lv_obj_get_child_count(tile->live_host) > 0U ?
                  lv_obj_get_child(tile->live_host, 0) : RT_NULL;
    tile->live_label = first_child != RT_NULL &&
                       lv_obj_check_type(first_child, &lv_label_class) ?
                       first_child : RT_NULL;
}

static void live_timer_cb(lv_timer_t *timer)
{
    size_t i;
    LV_UNUSED(timer);
    for (i = 0U; i < s_tile_count; i++)
    {
        uint32_t period;
        if (!s_tiles[i].live_enabled || s_tiles[i].descriptor == RT_NULL ||
            s_tiles[i].descriptor->app.live_content == RT_NULL) continue;
        period = s_tiles[i].descriptor->app.loop_period_ms;
        if (period < FT_TILE_LIVE_TICK_MS) period = FT_TILE_LIVE_TICK_MS;
        s_tiles[i].live_elapsed_ms += FT_TILE_LIVE_TICK_MS;
        if (s_tiles[i].live_elapsed_ms >= period)
        {
            s_tiles[i].live_elapsed_ms %= period;
            live_content_refresh(&s_tiles[i], true);
        }
    }
}

static lv_obj_t *create_tile_object(lv_obj_t *parent,
                                    ft_start_tile_runtime_t *runtime)
{
    const ft_app_descriptor_t *descriptor = runtime->descriptor;
    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_t *body;
    lv_obj_t *icon;
    size_t i;
    runtime->object = tile;
    lv_obj_remove_style_all(tile);
    lv_obj_set_style_radius(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    /* CLICKED is also emitted after a long press.  SHORT_CLICKED is the LVGL
     * event that guarantees application launch and edit entry are exclusive. */
    lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_SHORT_CLICKED, runtime);
    lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_LONG_PRESSED, runtime);
    lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_PRESSING, runtime);
    lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_RELEASED, runtime);
    lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_PRESS_LOST, runtime);
    body = lv_obj_create(tile);
    runtime->body = body;
    lv_obj_remove_style_all(body);
    lv_obj_set_style_radius(body, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(body, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body,
                            effective_opacity(runtime,
                                              ft_preferences_get()->tile_opa),
                            LV_PART_MAIN);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE |
                       LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    ft_ui_register_accent(body, FT_ACCENT_BACKGROUND);
    icon = ft_icon_create(body, descriptor->app.app_icon,
                          ft_layout_icon_size(48U), false);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    runtime->label = lv_label_create(body);
    lv_label_set_text(runtime->label, runtime->name);
    lv_obj_set_style_text_font(runtime->label, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_align(runtime->label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (descriptor->app.live_content != RT_NULL || descriptor->page_id == FT_PAGE_SYSTEM)
    {
        runtime->live_host = lv_obj_create(body);
        lv_obj_set_style_bg_opa(runtime->live_host, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(runtime->live_host, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(runtime->live_host, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(runtime->live_host, 0, LV_PART_MAIN);
        lv_obj_remove_flag(runtime->live_host,
                           LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(runtime->live_host, LV_ALIGN_TOP_LEFT, 0, 0);
        if (descriptor->page_id == FT_PAGE_SYSTEM)
        {
            runtime->live_label = lv_label_create(runtime->live_host);
            lv_label_set_long_mode(runtime->live_label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(runtime->live_label, lv_pct(100));
            lv_obj_set_style_text_align(runtime->live_label,
                                        LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_obj_set_style_text_font(runtime->live_label,
                                       ft_layout_font(12), LV_PART_MAIN);
            lv_obj_align(runtime->live_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
            lv_label_set_text(runtime->live_label, "M33 waiting");
        }
        else
            live_content_refresh(runtime, false);
    }
    for (i = 0U; i < FT_TILE_HANDLE_COUNT; i++)
        runtime->handles[i] = create_handle(runtime, (ft_tile_handle_id_t)i);
    update_geometry(runtime, runtime->column_span, runtime->row_span);
    return tile;
}

int ft_tiles_create(lv_obj_t *container, const ft_app_descriptor_t *apps, size_t count)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    uint8_t column = 0U;
    uint8_t row = 0U;
    uint8_t line_rows = 1U;
    size_t i;
    if (container == RT_NULL || apps == RT_NULL || count == 0U ||
        count > FT_TILE_MAX_APPS) return -RT_EINVAL;
    if (s_container != RT_NULL && s_container != container) tile_edit_leave();
    memset(s_tiles, 0, sizeof(s_tiles));
    s_container = container;
    s_scroll_extent = RT_NULL;
    s_tile_count = count;
    s_selected = RT_NULL;
    s_placeholder = RT_NULL;
    s_resize_settle_tile = RT_NULL;
    s_interaction = FT_TILE_INTERACTION_NONE;
    s_move_base_valid = false;
    s_move_snap_confirmed = false;
    s_desktop_rows = FT_TILE_DESKTOP_MIN_ROWS;
    s_interaction_visual_valid = false;
    s_container_scrollbar_saved = false;
    s_scale_redraw_count = 0U;
    /* Explicit cells prevent LVGL Flex from compacting every sibling while
     * one Tile is resized.  Conflict resolution below now owns all movement. */
    lv_obj_set_layout(container, LV_LAYOUT_NONE);
    /* Tile edit chrome is fully inset and the desktop remains a hard page
     * clip.  Neither the floating body nor a child may write into navigation. */
    lv_obj_remove_flag(container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(container, container_event_cb, LV_EVENT_ALL, RT_NULL);
    for (i = 0U; i < count; i++)
    {
        s_tiles[i].descriptor = &apps[i];
        rt_strncpy(s_tiles[i].name, apps[i].tile.name, sizeof(s_tiles[i].name) - 1U);
        s_tiles[i].name[sizeof(s_tiles[i].name) - 1U] = '\0';
        s_tiles[i].column_span = clamp_u8(apps[i].tile.column_span, 1U,
                                          ft_layout_get()->tile_columns);
        s_tiles[i].row_span = clamp_u8(apps[i].tile.row_span, 1U,
                                       FT_TILE_MAX_ROW_SPAN);
        s_tiles[i].opacity = apps[i].tile.opacity;
        s_tiles[i].pattern_icon = apps[i].tile.pattern_icon;
        s_tiles[i].live_enabled = apps[i].app.loop_enabled;
        (void)create_tile_object(container, &s_tiles[i]);
        if (column + s_tiles[i].column_span > layout->tile_columns)
        {
            row += line_rows;
            column = 0U;
            line_rows = 1U;
        }
        s_tiles[i].grid_column = column;
        s_tiles[i].grid_row = row;
        tile_snap_to_grid(&s_tiles[i], column, row, false);
        column += s_tiles[i].column_span;
        if (s_tiles[i].row_span > line_rows) line_rows = s_tiles[i].row_span;
    }
    desktop_extent_refresh((uint8_t)(row + line_rows));
    lv_obj_update_layout(container);
    if (s_live_timer != RT_NULL) lv_timer_delete(s_live_timer);
    s_live_timer = lv_timer_create(live_timer_cb, FT_TILE_LIVE_TICK_MS, RT_NULL);
    return RT_EOK;
}

void ft_tiles_set_external_text(ft_page_id_t page_id, const char *text)
{
    size_t i;
    for (i = 0U; i < s_tile_count; i++)
        if (s_tiles[i].descriptor != RT_NULL &&
            s_tiles[i].descriptor->page_id == page_id &&
            object_valid(s_tiles[i].live_label))
        {
            const char *next = text != RT_NULL ? text : "";
            const char *current = lv_label_get_text(s_tiles[i].live_label);
            if (current == RT_NULL || strcmp(current, next) != 0)
                lv_label_set_text(s_tiles[i].live_label, next);
        }
}

void ft_tiles_set_live_loop(ft_page_id_t page_id, bool enabled)
{
    size_t i;
    for (i = 0U; i < s_tile_count; i++)
        if (s_tiles[i].descriptor != RT_NULL && s_tiles[i].descriptor->page_id == page_id)
        {
            s_tiles[i].live_enabled = enabled;
            s_tiles[i].live_elapsed_ms = 0U;
            live_content_refresh(&s_tiles[i], false);
    }
}

void ft_tiles_set_localized_name(ft_page_id_t page_id,
                                 const char *english_name,
                                 const char *chinese_name,
                                 const char *display_name)
{
    size_t i;
    if (english_name == RT_NULL || chinese_name == RT_NULL || display_name == RT_NULL)
        return;
    for (i = 0U; i < s_tile_count; i++)
    {
        if (s_tiles[i].descriptor == RT_NULL ||
            s_tiles[i].descriptor->page_id != page_id) continue;
        if (strcmp(s_tiles[i].name, english_name) != 0 &&
            strcmp(s_tiles[i].name, chinese_name) != 0) continue;
        rt_strncpy(s_tiles[i].name, display_name, sizeof(s_tiles[i].name) - 1U);
        s_tiles[i].name[sizeof(s_tiles[i].name) - 1U] = '\0';
        if (object_valid(s_tiles[i].label))
            lv_label_set_text(s_tiles[i].label, s_tiles[i].name);
    }
}

bool ft_tiles_editing(void) { return s_selected != RT_NULL; }

size_t ft_tiles_selected(void)
{
    return s_selected != RT_NULL ? (size_t)(s_selected - s_tiles) : SIZE_MAX;
}

#ifdef FEATHERTALK_UI_TEST_MODE
lv_obj_t *ft_tiles_test_get_object(size_t index)
{
    return index < s_tile_count ? s_tiles[index].object : RT_NULL;
}

bool ft_tiles_test_editing(void) { return ft_tiles_editing(); }

size_t ft_tiles_test_selected(void)
{
    return ft_tiles_selected();
}

size_t ft_tiles_test_handle_count(void)
{
    size_t count = 0U;
    size_t i;
    if (s_selected == RT_NULL) return 0U;
    for (i = 0U; i < FT_TILE_HANDLE_COUNT; i++)
        if (object_valid(s_selected->handles[i]) &&
            !lv_obj_has_flag(s_selected->handles[i], LV_OBJ_FLAG_HIDDEN)) count++;
    return count;
}

bool ft_tiles_test_handle_geometry(void)
{
    lv_area_t tile_area;
    lv_area_t body_area;
    uint32_t child_count;
    int32_t tile_center_x;
    int32_t tile_center_y;
    size_t i;
    if (s_selected == RT_NULL || !object_valid(s_selected->object)) return false;
    child_count = lv_obj_get_child_count(s_container);
    if (!object_valid(s_placeholder) ||
        !lv_obj_has_flag(s_selected->object, LV_OBJ_FLAG_FLOATING) ||
        child_count == 0U ||
        lv_obj_get_index(s_selected->object) != (int32_t)child_count - 1)
        return false;
    if (!s_container_scrollbar_saved || s_scale_redraw_count == 0U ||
        lv_obj_get_scrollbar_mode(s_container) != LV_SCROLLBAR_MODE_OFF) return false;
    if (!object_valid(s_selected->body) ||
        lv_anim_get(s_selected->body, tile_scale_anim_cb) == RT_NULL ||
        lv_obj_has_flag(s_selected->object, LV_OBJ_FLAG_OVERFLOW_VISIBLE) ||
        lv_obj_get_style_transform_scale_x(s_selected->object, LV_PART_MAIN) != 256 ||
        lv_obj_get_style_transform_scale_y(s_selected->object, LV_PART_MAIN) != 256)
        return false;
    lv_obj_update_layout(s_container);
    lv_obj_get_coords(s_selected->object, &tile_area);
    lv_obj_get_coords(s_selected->body, &body_area);
    if (body_area.x1 < tile_area.x1 || body_area.y1 < tile_area.y1 ||
        body_area.x2 > tile_area.x2 || body_area.y2 > tile_area.y2 ||
        lv_obj_get_index(s_selected->body) != 0) return false;
    tile_center_x = (tile_area.x1 + tile_area.x2) / 2;
    tile_center_y = (tile_area.y1 + tile_area.y2) / 2;
    for (i = 0U; i < FT_TILE_HANDLE_COUNT; i++)
    {
        lv_area_t handle_area;
        int32_t center_x;
        int32_t center_y;
        if (!object_valid(s_selected->handles[i])) return false;
        lv_obj_get_coords(s_selected->handles[i], &handle_area);
        if (lv_area_get_width(&handle_area) < 25 ||
            lv_area_get_width(&handle_area) > 35 ||
            lv_area_get_width(&handle_area) != lv_area_get_height(&handle_area) ||
            (lv_area_get_width(&handle_area) & 1) == 0 ||
            lv_obj_get_child_count(s_selected->handles[i]) != 0U ||
            lv_obj_get_style_bg_opa(s_selected->handles[i], LV_PART_MAIN) !=
                LV_OPA_TRANSP ||
            lv_obj_get_style_border_width(s_selected->handles[i], LV_PART_MAIN) != 0 ||
            handle_area.x1 < tile_area.x1 || handle_area.y1 < tile_area.y1 ||
            handle_area.x2 > tile_area.x2 || handle_area.y2 > tile_area.y2)
            return false;
        center_x = (handle_area.x1 + handle_area.x2) / 2;
        center_y = (handle_area.y1 + handle_area.y2) / 2;
        if ((i == FT_TILE_HANDLE_RESIZE_TL || i == FT_TILE_HANDLE_RESIZE_BL) ?
            center_x >= tile_center_x : center_x <= tile_center_x) return false;
        if ((i == FT_TILE_HANDLE_RESIZE_TL || i == FT_TILE_HANDLE_RESIZE_TR) ?
            center_y >= tile_center_y : center_y <= tile_center_y) return false;
    }
    return true;
}

bool ft_tiles_test_move(size_t app_index, size_t target_index)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_point_t start = {40, 40};
    lv_point_t moved = {116, 102};
    lv_point_t desired;
    lv_area_t content_area;
    int32_t initial_x;
    int32_t initial_y;
    uint8_t saved_column[FT_TILE_MAX_APPS];
    uint8_t saved_row[FT_TILE_MAX_APPS];
    uint8_t target_column = 0U;
    uint8_t target_row = 0U;
    uint8_t line_rows = 1U;
    size_t i;
    bool followed;
    bool pending_stationary = true;
    bool confirmed_reflow = true;
    size_t collided = 0U;
    size_t untouched = 0U;
    if (app_index >= s_tile_count || target_index >= s_tile_count) return false;
    tile_edit_enter(&s_tiles[app_index]);
    for (i = 0U; i < s_tile_count; i++)
    {
        saved_column[i] = s_tiles[i].grid_column;
        saved_row[i] = s_tiles[i].grid_row;
    }
    for (i = 0U; i <= target_index; i++)
    {
        if (target_column + s_tiles[i].column_span > layout->tile_columns)
        {
            target_row += line_rows;
            target_column = 0U;
            line_rows = 1U;
        }
        if (i == target_index) break;
        target_column += s_tiles[i].column_span;
        if (s_tiles[i].row_span > line_rows) line_rows = s_tiles[i].row_span;
    }
    initial_x = lv_obj_get_x(s_tiles[app_index].object);
    initial_y = lv_obj_get_y(s_tiles[app_index].object);
    move_begin(&s_tiles[app_index], &start);
    move_update(&moved);
    followed = lv_obj_get_style_x(s_tiles[app_index].object, LV_PART_MAIN) ==
                   initial_x + moved.x - start.x &&
               lv_obj_get_style_y(s_tiles[app_index].object, LV_PART_MAIN) ==
                   initial_y + moved.y - start.y;
    for (i = 0U; i < s_tile_count; i++)
        if (&s_tiles[i] != s_selected &&
            (s_tiles[i].grid_column != saved_column[i] ||
             s_tiles[i].grid_row != saved_row[i])) pending_stationary = false;
    if (!lv_obj_has_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN)) pending_stationary = false;
    lv_obj_get_content_coords(s_container, &content_area);
    desired.x = content_area.x1 - lv_obj_get_scroll_x(s_container) +
                grid_x(target_column) + tile_width(s_selected->column_span) / 2;
    desired.y = content_area.y1 - lv_obj_get_scroll_y(s_container) +
                grid_y(target_row) + tile_height(s_selected->row_span) / 2;
    if (placeholder_snap_nearest(&desired, true) == SIZE_MAX) return false;
    lv_obj_set_pos(s_selected->object, grid_x(target_column), grid_y(target_row));
    move_finish();
    for (i = 0U; i < s_tile_count; i++)
    {
        bool covered;
        bool changed;
        if (&s_tiles[i] == s_selected) continue;
        covered = grid_rects_overlap(target_column, target_row,
                                     s_selected->column_span, s_selected->row_span,
                                     saved_column[i], saved_row[i],
                                     s_tiles[i].column_span, s_tiles[i].row_span);
        changed = s_tiles[i].grid_column != saved_column[i] ||
                  s_tiles[i].grid_row != saved_row[i];
        if (covered)
        {
            collided++;
            if (!changed) confirmed_reflow = false;
        }
        else
        {
            untouched++;
            if (changed) confirmed_reflow = false;
        }
    }
    return followed &&
           pending_stationary && confirmed_reflow && collided > 0U && untouched > 0U &&
           object_valid(s_placeholder) &&
           !lv_obj_has_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN) &&
           s_selected->grid_column == target_column &&
           s_selected->grid_row == target_row &&
           lv_obj_get_style_x(s_placeholder, LV_PART_MAIN) == grid_x(target_column) &&
           lv_obj_get_style_y(s_placeholder, LV_PART_MAIN) == grid_y(target_row) &&
           lv_obj_get_index(s_tiles[app_index].object) ==
           (int32_t)lv_obj_get_child_count(s_container) - 1;
}

bool ft_tiles_test_move_nearest(size_t app_index)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    uint8_t saved_column[FT_TILE_MAX_APPS];
    uint8_t saved_row[FT_TILE_MAX_APPS];
    lv_area_t content_area;
    lv_point_t desired_pit;
    uint8_t visible_rows;
    uint8_t target_column[3] = {0U};
    uint8_t target_row[3] = {0U};
    size_t i;
    size_t candidate;
    bool valid = true;
    if (app_index >= s_tile_count || s_tile_count < 2U) return false;
    tile_edit_enter(&s_tiles[app_index]);
    if (!object_valid(s_placeholder)) return false;
    for (i = 0U; i < s_tile_count; i++)
    {
        saved_column[i] = s_tiles[i].grid_column;
        saved_row[i] = s_tiles[i].grid_row;
        s_move_base_column[i] = saved_column[i];
        s_move_base_row[i] = saved_row[i];
    }
    s_move_base_valid = true;

    /* Exercise unrelated legal pits, including a middle cell and the lowest
     * visible empty row.  This guards against falling back to insertion-only
     * upper/lower choices. */
    lv_obj_get_content_coords(s_container, &content_area);
    visible_rows = span_that_fits(lv_area_get_height(&content_area),
                                  layout->tile_height, layout->tile_gap,
                                  FT_TILE_GRID_ROWS);
    if (visible_rows < 2U) return false;
    target_column[0] = layout->tile_columns - s_selected->column_span;
    target_row[0] = 1U;
    target_column[1] = 0U;
    target_row[1] = visible_rows - s_selected->row_span;
    target_column[2] = layout->tile_columns > s_selected->column_span ? 1U : 0U;
    target_row[2] = visible_rows > s_selected->row_span + 1U ? 2U : 0U;

    for (candidate = 0U; candidate < 3U; candidate++)
    {
        for (i = 0U; i < s_tile_count; i++)
            tile_snap_to_grid(&s_tiles[i], saved_column[i], saved_row[i], false);
        s_selected->grid_column = saved_column[app_index];
        s_selected->grid_row = saved_row[app_index];
        lv_obj_set_pos(s_placeholder, grid_x(saved_column[app_index]),
                       grid_y(saved_row[app_index]));
        desired_pit.x = content_area.x1 - lv_obj_get_scroll_x(s_container) +
                        grid_x(target_column[candidate]) +
                        tile_width(s_selected->column_span) / 2;
        desired_pit.y = content_area.y1 - lv_obj_get_scroll_y(s_container) +
                        grid_y(target_row[candidate]) +
                        tile_height(s_selected->row_span) / 2;
        if (placeholder_snap_nearest(&desired_pit, true) == SIZE_MAX ||
            s_selected->grid_column != target_column[candidate] ||
            s_selected->grid_row != target_row[candidate] ||
            lv_obj_get_style_x(s_placeholder, LV_PART_MAIN) !=
                grid_x(target_column[candidate]) ||
            lv_obj_get_style_y(s_placeholder, LV_PART_MAIN) !=
                grid_y(target_row[candidate]))
            valid = false;
    }

    for (i = 0U; i < s_tile_count; i++)
        tile_snap_to_grid(&s_tiles[i], saved_column[i], saved_row[i], false);
    lv_obj_set_pos(s_placeholder, grid_x(saved_column[app_index]),
                   grid_y(saved_row[app_index]));
    lv_obj_set_size(s_placeholder, tile_width(s_selected->column_span),
                    tile_height(s_selected->row_span));
    lv_obj_remove_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(s_selected->object, -1);
    s_move_base_valid = false;
    s_move_snap_confirmed = false;
    return valid;
}

bool ft_tiles_test_move_scrolled(size_t app_index)
{
    const int32_t dx = 19;
    const int32_t dy = 13;
    lv_area_t after_raise;
    lv_area_t after_move;
    lv_point_t start;
    lv_point_t moved;
    uint8_t saved_column;
    uint8_t saved_row;
    uint8_t test_row = 8U;
    int32_t applied_scroll = 0;
    bool was_scrollable;
    bool valid = false;
    if (app_index >= s_tile_count || !object_valid(s_container) ||
        !object_valid(s_tiles[app_index].object) ||
        test_row + s_tiles[app_index].row_span > desktop_row_limit()) return false;

    tile_edit_leave();
    saved_column = s_tiles[app_index].grid_column;
    saved_row = s_tiles[app_index].grid_row;
    was_scrollable = lv_obj_has_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    tile_snap_to_grid(&s_tiles[app_index], 0U, test_row, false);
    lv_obj_update_layout(s_container);
    lv_obj_scroll_to_y(s_container, grid_y(test_row) - tile_height(1U), LV_ANIM_OFF);
    lv_obj_update_layout(s_container);
    applied_scroll = lv_obj_get_scroll_y(s_container);
    if (applied_scroll != 0)
    {
        tile_edit_enter(&s_tiles[app_index]);
        lv_obj_update_layout(s_container);
        lv_obj_get_coords(s_tiles[app_index].object, &after_raise);
        start.x = (after_raise.x1 + after_raise.x2) / 2;
        start.y = (after_raise.y1 + after_raise.y2) / 2;
        moved.x = start.x + dx;
        moved.y = start.y + dy;
        move_begin(&s_tiles[app_index], &start);
        move_update(&moved);
        /* Production refresh applies the style-position update at the end of
         * the current LVGL frame.  The synchronous test reads immediately, so
         * complete that single test frame before checking screen coordinates. */
        lv_obj_update_layout(s_container);
        lv_obj_get_coords(s_tiles[app_index].object, &after_move);
        /* Entering edit may intentionally adjust the scroll just enough to
         * reveal a complete corner handle.  From that settled position the
         * floating Tile must still track the pointer without any jump. */
        valid = lv_obj_get_scroll_y(s_container) != 0 &&
                after_move.x1 == after_raise.x1 + dx &&
                after_move.y1 == after_raise.y1 + dy;
        move_finish();
    }

    tile_edit_leave();
    tile_snap_to_grid(&s_tiles[app_index], saved_column, saved_row, false);
    desktop_extent_set_rows(desktop_content_rows(), false);
    lv_obj_scroll_to_y(s_container, 0, LV_ANIM_OFF);
    if (!was_scrollable) lv_obj_remove_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(s_container);
    tile_repair_viewport();
    return valid && s_tiles[app_index].grid_column == saved_column &&
           s_tiles[app_index].grid_row == saved_row;
}

bool ft_tiles_test_move_edge_autoscroll(size_t app_index)
{
    uint8_t saved_column[FT_TILE_MAX_APPS];
    uint8_t saved_row[FT_TILE_MAX_APPS];
    lv_area_t tile_area;
    lv_area_t content_area;
    lv_point_t start;
    lv_point_t edge;
    int32_t scroll_before;
    int32_t scroll_after;
    size_t i;
    uint8_t confirmed_row = 0U;
    bool confirmed_once = false;
    bool snapped;
    if (app_index >= s_tile_count || !object_valid(s_container)) return false;
    tile_edit_leave();
    lv_obj_scroll_to_y(s_container, 0, LV_ANIM_OFF);
    for (i = 0U; i < s_tile_count; i++)
    {
        saved_column[i] = s_tiles[i].grid_column;
        saved_row[i] = s_tiles[i].grid_row;
    }
    tile_edit_enter(&s_tiles[app_index]);
    lv_obj_update_layout(s_container);
    lv_obj_get_coords(s_tiles[app_index].object, &tile_area);
    lv_obj_get_content_coords(s_container, &content_area);
    start.x = tile_area.x1;
    start.y = tile_area.y1;
    edge.x = start.x;
    edge.y = content_area.y2;
    scroll_before = lv_obj_get_scroll_y(s_container);
    move_begin(&s_tiles[app_index], &start);
    for (i = 0U; i < 24U; i++)
    {
        move_update(&edge);
        if (s_move_snap_confirmed && object_valid(s_placeholder) &&
            !lv_obj_has_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN))
        {
            confirmed_once = true;
            confirmed_row = s_selected->grid_row;
        }
    }
    scroll_after = lv_obj_get_scroll_y(s_container);
    snapped = scroll_after > scroll_before && confirmed_once &&
              confirmed_row > saved_row[app_index] &&
              confirmed_row + s_selected->row_span <= desktop_row_limit();
    move_finish();
    tile_edit_leave();
    for (i = 0U; i < s_tile_count; i++)
        tile_snap_to_grid(&s_tiles[i], saved_column[i], saved_row[i], false);
    lv_obj_scroll_to_y(s_container, 0, LV_ANIM_OFF);
    lv_obj_update_layout(s_container);
    tile_repair_viewport();
    return snapped;
}

bool ft_tiles_test_resize_edge_autoscroll(size_t app_index)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    uint8_t saved_column[FT_TILE_MAX_APPS];
    uint8_t saved_row[FT_TILE_MAX_APPS];
    uint8_t saved_columns;
    uint8_t saved_rows;
    uint8_t visible_rows;
    uint8_t target_row;
    lv_area_t tile_area;
    lv_area_t content_area;
    lv_point_t start;
    lv_point_t edge;
    int32_t target_scroll;
    int32_t scroll_before;
    size_t i;
    bool valid;
    if (app_index >= s_tile_count || !object_valid(s_container) ||
        desktop_row_limit() < FT_TILE_MAX_ROW_SPAN + 1U) return false;
    tile_edit_leave();
    lv_obj_scroll_to_y(s_container, 0, LV_ANIM_OFF);
    for (i = 0U; i < s_tile_count; i++)
    {
        saved_column[i] = s_tiles[i].grid_column;
        saved_row[i] = s_tiles[i].grid_row;
    }
    saved_columns = s_tiles[app_index].column_span;
    saved_rows = s_tiles[app_index].row_span;
    lv_obj_get_content_coords(s_container, &content_area);
    visible_rows = span_that_fits(lv_area_get_height(&content_area),
                                  layout->tile_height, layout->tile_gap,
                                  desktop_row_limit());
    target_row = visible_rows > 1U ? visible_rows - 1U : 1U;
    if (target_row + FT_TILE_MAX_ROW_SPAN > desktop_row_limit())
        target_row = desktop_row_limit() - FT_TILE_MAX_ROW_SPAN;
    update_geometry(&s_tiles[app_index], saved_columns, 1U);
    tile_snap_to_grid(&s_tiles[app_index], 0U, target_row, false);
    lv_obj_update_layout(s_container);
    target_scroll = grid_y(target_row) + tile_height(1U) -
                    lv_area_get_height(&content_area);
    if (target_scroll < 0) target_scroll = 0;
    lv_obj_scroll_to_y(s_container, target_scroll, LV_ANIM_OFF);
    tile_edit_enter(&s_tiles[app_index]);
    lv_obj_update_layout(s_container);
    lv_obj_get_coords(s_tiles[app_index].object, &tile_area);
    lv_obj_get_content_coords(s_container, &content_area);
    start.x = tile_area.x2;
    start.y = tile_area.y2;
    edge.x = start.x;
    edge.y = content_area.y2;
    scroll_before = lv_obj_get_scroll_y(s_container);
    resize_begin(&s_tiles[app_index], FT_TILE_HANDLE_RESIZE_BR, &start);
    for (i = 0U; i < 24U; i++) resize_update(&edge);
    valid = lv_obj_get_scroll_y(s_container) > scroll_before &&
            s_tiles[app_index].row_span == FT_TILE_MAX_ROW_SPAN &&
            s_tiles[app_index].grid_row == target_row &&
            s_tiles[app_index].grid_row + s_tiles[app_index].row_span <=
                desktop_row_limit();
    resize_finish();
    tile_edit_leave();
    update_geometry(&s_tiles[app_index], saved_columns, saved_rows);
    for (i = 0U; i < s_tile_count; i++)
        tile_snap_to_grid(&s_tiles[i], saved_column[i], saved_row[i], false);
    lv_obj_scroll_to_y(s_container, 0, LV_ANIM_OFF);
    lv_obj_update_layout(s_container);
    tile_repair_viewport();
    return valid;
}

bool ft_tiles_test_layout_settled(void)
{
    int32_t x[FT_TILE_MAX_APPS];
    int32_t y[FT_TILE_MAX_APPS];
    size_t i;
    uint32_t child_count;
    if (s_interaction != FT_TILE_INTERACTION_NONE || s_selected == RT_NULL ||
        !object_valid(s_selected->object) || !object_valid(s_placeholder) ||
        !object_valid(s_container)) return false;
    child_count = lv_obj_get_child_count(s_container);
    if (!lv_obj_has_flag(s_selected->object, LV_OBJ_FLAG_FLOATING) ||
        child_count == 0U ||
        lv_obj_get_index(s_selected->object) != (int32_t)child_count - 1 ||
        lv_obj_get_x(s_selected->object) != lv_obj_get_x(s_placeholder) ||
        lv_obj_get_y(s_selected->object) != lv_obj_get_y(s_placeholder) ||
        lv_obj_get_width(s_selected->object) != lv_obj_get_width(s_placeholder) ||
        lv_obj_get_height(s_selected->object) != lv_obj_get_height(s_placeholder) ||
        !object_valid(s_selected->body) ||
        lv_anim_get(s_selected->body, tile_scale_anim_cb) == RT_NULL) return false;

    for (i = 0U; i < s_tile_count; i++)
    {
        if (!object_valid(s_tiles[i].object)) return false;
        x[i] = lv_obj_get_x(s_tiles[i].object);
        y[i] = lv_obj_get_y(s_tiles[i].object);
    }
    lv_obj_mark_layout_as_dirty(s_container);
    lv_obj_update_layout(s_container);
    for (i = 0U; i < s_tile_count; i++)
    {
        if (lv_obj_get_x(s_tiles[i].object) != x[i] ||
            lv_obj_get_y(s_tiles[i].object) != y[i]) return false;
    }
    return lv_obj_get_x(s_selected->object) == lv_obj_get_x(s_placeholder) &&
           lv_obj_get_y(s_selected->object) == lv_obj_get_y(s_placeholder);
}

bool ft_tiles_test_resize(size_t app_index, uint8_t columns, uint8_t rows)
{
    lv_point_t start = {0, 0};
    lv_point_t moved;
    ft_tile_handle_id_t handle;
    int32_t initial_width;
    int32_t initial_height;
    int32_t desired_width;
    int32_t desired_height;
    bool body_followed;
    bool settle_expected;
    bool settle_started;
    if (app_index >= s_tile_count || columns == 0U ||
        columns > ft_layout_get()->tile_columns || rows == 0U ||
        rows > FT_TILE_MAX_ROW_SPAN) return false;
    /* Movement tests intentionally leave a rearranged desktop.  Reset this
     * independent resize scenario so the chosen footprint has deterministic
     * collision neighbours on every responsive column profile. */
    (void)ft_tiles_test_restore_layout();
    tile_edit_enter(&s_tiles[app_index]);
    initial_width = lv_obj_get_width(s_tiles[app_index].object);
    initial_height = lv_obj_get_height(s_tiles[app_index].object);
    handle = s_tiles[app_index].grid_column + columns <=
             ft_layout_get()->tile_columns ?
             FT_TILE_HANDLE_RESIZE_BR : FT_TILE_HANDLE_RESIZE_BL;
    desired_width = tile_width(columns) - (columns > 1U ? 8 : 0);
    desired_height = tile_height(rows) - (rows > 1U ? 8 : 0);
    moved.x = handle == FT_TILE_HANDLE_RESIZE_BL ?
               initial_width - desired_width : desired_width - initial_width;
    moved.y = desired_height - initial_height;
    resize_begin(&s_tiles[app_index], handle, &start);
    resize_update(&moved);
    body_followed = object_valid(s_tiles[app_index].body) &&
                    lv_obj_get_width(s_tiles[app_index].body) ==
                        lv_obj_get_width(s_tiles[app_index].object) &&
                    lv_obj_get_height(s_tiles[app_index].body) ==
                        lv_obj_get_height(s_tiles[app_index].object);
    settle_expected = desired_width != tile_width(columns) ||
                      desired_height != tile_height(rows);
    resize_finish();
    settle_started = !settle_expected ||
                     (s_resize_settle_tile == &s_tiles[app_index] &&
                      lv_anim_get(s_tiles[app_index].object,
                                  resize_settle_anim_cb) != RT_NULL);
    resize_settle_complete_now(&s_tiles[app_index]);
    tile_animation_start(&s_tiles[app_index]);
    return body_followed && settle_started &&
           s_tiles[app_index].column_span == columns &&
           s_tiles[app_index].row_span == rows &&
           lv_obj_get_width(s_tiles[app_index].object) == tile_width(columns) &&
           lv_obj_get_height(s_tiles[app_index].object) == tile_height(rows) &&
           lv_obj_get_width(s_tiles[app_index].body) == tile_width(columns) &&
           lv_obj_get_height(s_tiles[app_index].body) == tile_height(rows);
}

bool ft_tiles_test_resize_boundary(void)
{
    ft_start_tile_runtime_t *rightmost = RT_NULL;
    lv_area_t container_area;
    lv_area_t before;
    lv_area_t after;
    lv_point_t start = {0, 0};
    lv_point_t beyond = {10000, 0};
    int32_t rightmost_x = INT32_MIN;
    int32_t maximum_width;
    uint8_t maximum_columns;
    size_t i;
    bool valid;
    if (!object_valid(s_container)) return false;
    lv_obj_update_layout(s_container);
    for (i = 0U; i < s_tile_count; i++)
    {
        lv_area_t area;
        if (!object_valid(s_tiles[i].object)) continue;
        lv_obj_get_coords(s_tiles[i].object, &area);
        if (area.x1 > rightmost_x)
        {
            rightmost_x = area.x1;
            rightmost = &s_tiles[i];
        }
    }
    if (rightmost == RT_NULL) return false;
    tile_edit_enter(rightmost);
    lv_obj_update_layout(s_container);
    lv_obj_get_content_coords(s_container, &container_area);
    lv_obj_get_coords(rightmost->object, &before);
    start.x = before.x2;
    start.y = before.y2;
    beyond.y = start.y;
    resize_begin(rightmost, FT_TILE_HANDLE_RESIZE_BR, &start);
    maximum_width = s_resize_max_width;
    maximum_columns = s_resize_max_columns;
    resize_update(&beyond);
    lv_obj_update_layout(s_container);
    lv_obj_get_coords(rightmost->object, &after);
    valid = after.y1 == before.y1 &&
            after.x1 == before.x1 &&
            after.x2 <= container_area.x2 + 1 &&
            lv_area_get_width(&after) <= maximum_width &&
            rightmost->column_span <= maximum_columns;
    resize_finish();
    lv_obj_update_layout(s_container);
    lv_obj_get_coords(rightmost->object, &after);
    return valid && after.y1 == before.y1 &&
           after.x2 <= container_area.x2 + 1 &&
           rightmost->column_span <= maximum_columns;
}

bool ft_tiles_test_resize_anchors(size_t app_index)
{
    static const ft_tile_handle_id_t handles[] = {
        FT_TILE_HANDLE_RESIZE_TL,
        FT_TILE_HANDLE_RESIZE_TR,
        FT_TILE_HANDLE_RESIZE_BL,
        FT_TILE_HANDLE_RESIZE_BR
    };
    ft_start_tile_runtime_t *tile;
    lv_point_t start = {0, 0};
    lv_point_t moved;
    int32_t original_x;
    int32_t original_y;
    int32_t original_width;
    int32_t original_height;
    uint8_t original_columns;
    uint8_t original_rows;
    size_t i;
    bool valid = true;
    if (app_index >= s_tile_count) return false;
    tile_edit_enter(&s_tiles[app_index]);
    tile = s_selected;
    if (tile == RT_NULL || !object_valid(tile->object) ||
        !object_valid(s_placeholder) || tile->column_span < 2U || tile->row_span < 2U)
        return false;

    tile_animation_stop(tile);
    original_x = lv_obj_get_x(tile->object);
    original_y = lv_obj_get_y(tile->object);
    original_width = lv_obj_get_width(tile->object);
    original_height = lv_obj_get_height(tile->object);
    original_columns = tile->column_span;
    original_rows = tile->row_span;

    for (i = 0U; i < sizeof(handles) / sizeof(handles[0]); i++)
    {
        lv_area_t before;
        lv_area_t after;
        tile->column_span = original_columns;
        tile->row_span = original_rows;
        tile_visual_rect_apply(tile, original_x, original_y,
                               original_width, original_height);
        lv_obj_set_size(s_placeholder, original_width, original_height);
        lv_obj_update_layout(s_container);
        lv_obj_get_coords(tile->object, &before);

        moved.x = tile_width(1U) - original_width;
        moved.y = tile_height(1U) - original_height;
        if (handles[i] == FT_TILE_HANDLE_RESIZE_TL)
        {
            moved.x = -moved.x;
            moved.y = -moved.y;
        }
        else if (handles[i] == FT_TILE_HANDLE_RESIZE_TR)
            moved.y = -moved.y;
        else if (handles[i] == FT_TILE_HANDLE_RESIZE_BL)
            moved.x = -moved.x;
        resize_begin(tile, handles[i], &start);
        resize_update(&moved);
        lv_obj_update_layout(s_container);
        lv_obj_get_coords(tile->object, &after);

        if (lv_area_get_width(&after) != tile_width(1U) ||
            lv_area_get_height(&after) != tile_height(1U)) valid = false;
        if (handles[i] == FT_TILE_HANDLE_RESIZE_TL &&
            (after.x2 != before.x2 || after.y2 != before.y2)) valid = false;
        else if (handles[i] == FT_TILE_HANDLE_RESIZE_TR &&
            (after.x1 != before.x1 || after.y2 != before.y2)) valid = false;
        else if (handles[i] == FT_TILE_HANDLE_RESIZE_BL &&
                 (after.x2 != before.x2 || after.y1 != before.y1)) valid = false;
        else if (handles[i] == FT_TILE_HANDLE_RESIZE_BR &&
                 (after.x1 != before.x1 || after.y1 != before.y1)) valid = false;
        s_interaction = FT_TILE_INTERACTION_NONE;
    }

    tile->column_span = original_columns;
    tile->row_span = original_rows;
    tile_visual_rect_apply(tile, original_x, original_y,
                           original_width, original_height);
    lv_obj_set_size(s_placeholder, original_width, original_height);
    lv_obj_move_to_index(tile->object, -1);
    lv_obj_update_layout(s_container);
    tile_animation_start(tile);
    return valid;
}

size_t ft_tiles_test_order(size_t app_index)
{
    if (app_index >= s_tile_count || !object_valid(s_tiles[app_index].object)) return SIZE_MAX;
    return grid_order_of_tile(&s_tiles[app_index]);
}

uint8_t ft_tiles_test_columns(size_t app_index)
{ return app_index < s_tile_count ? s_tiles[app_index].column_span : 0U; }

uint8_t ft_tiles_test_rows(size_t app_index)
{ return app_index < s_tile_count ? s_tiles[app_index].row_span : 0U; }

bool ft_tiles_test_layout_valid(void)
{
    size_t i;
    size_t j;
    if (!object_valid(s_container)) return false;
    lv_obj_update_layout(s_container);
    for (i = 0U; i < s_tile_count; i++)
    {
        if (!object_valid(s_tiles[i].object) ||
            lv_obj_get_width(s_tiles[i].object) != tile_width(s_tiles[i].column_span) ||
            lv_obj_get_height(s_tiles[i].object) != tile_height(s_tiles[i].row_span) ||
            s_tiles[i].grid_column + s_tiles[i].column_span >
                ft_layout_get()->tile_columns ||
            s_tiles[i].grid_row + s_tiles[i].row_span > desktop_row_limit() ||
            lv_obj_get_style_x(s_tiles[i].object, LV_PART_MAIN) !=
                grid_x(s_tiles[i].grid_column) ||
            lv_obj_get_style_y(s_tiles[i].object, LV_PART_MAIN) !=
                grid_y(s_tiles[i].grid_row))
            return false;
        for (j = i + 1U; j < s_tile_count; j++)
            if (grid_rects_overlap(s_tiles[i].grid_column, s_tiles[i].grid_row,
                                   s_tiles[i].column_span, s_tiles[i].row_span,
                                   s_tiles[j].grid_column, s_tiles[j].grid_row,
                                   s_tiles[j].column_span, s_tiles[j].row_span))
                return false;
    }
    return true;
}

bool ft_tiles_test_restore_layout(void)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    uint8_t column = 0U;
    uint8_t row = 0U;
    uint8_t line_rows = 1U;
    size_t i;
    tile_edit_leave();
    for (i = 0U; i < s_tile_count; i++)
    {
        if (column + s_tiles[i].column_span > layout->tile_columns)
        {
            row += line_rows;
            column = 0U;
            line_rows = 1U;
        }
        tile_snap_to_grid(&s_tiles[i], column, row, false);
        column += s_tiles[i].column_span;
        if (s_tiles[i].row_span > line_rows) line_rows = s_tiles[i].row_span;
    }
    lv_obj_update_layout(s_container);
    return ft_tiles_test_layout_valid();
}

bool ft_tiles_test_resize_collision(void)
{
    size_t collided = 0U;
    size_t stationary = 0U;
    size_t i;
    size_t j;
    if (s_selected == RT_NULL || s_last_resize_displaced == SIZE_MAX ||
        s_last_resize_displaced == 0U) return false;
    for (i = 0U; i < s_tile_count; i++)
    {
        bool covered;
        bool moved;
        if (&s_tiles[i] == s_selected) continue;
        covered = grid_rects_overlap(s_selected->grid_column, s_selected->grid_row,
                                     s_selected->column_span, s_selected->row_span,
                                     s_resize_base_column[i], s_resize_base_row[i],
                                     s_tiles[i].column_span, s_tiles[i].row_span);
        moved = s_tiles[i].grid_column != s_resize_base_column[i] ||
                s_tiles[i].grid_row != s_resize_base_row[i];
        if (covered)
        {
            if (!moved ||
                (lv_anim_get(s_tiles[i].object, tile_translate_x_anim_cb) == RT_NULL &&
                 lv_anim_get(s_tiles[i].object, tile_translate_y_anim_cb) == RT_NULL))
                return false;
            collided++;
        }
        else
        {
            if (moved) return false;
            stationary++;
        }
    }
    for (i = 0U; i < s_tile_count; i++)
        for (j = i + 1U; j < s_tile_count; j++)
            if (grid_rects_overlap(s_tiles[i].grid_column, s_tiles[i].grid_row,
                                   s_tiles[i].column_span, s_tiles[i].row_span,
                                   s_tiles[j].grid_column, s_tiles[j].grid_row,
                                   s_tiles[j].column_span, s_tiles[j].row_span))
                return false;
    return collided > 0U && stationary > 0U &&
           collided == s_last_resize_displaced;
}

bool ft_tiles_test_set_common(size_t app_index, const char *name,
                              uint8_t opacity, ft_icon_id_t pattern_icon)
{
    const ft_ui_preferences_t *preferences;
    ft_start_tile_runtime_t *tile;
    if (app_index >= s_tile_count || name == RT_NULL ||
        pattern_icon > FT_ICON_COUNT) return false;
    tile = &s_tiles[app_index];
    rt_strncpy(tile->name, name, sizeof(tile->name) - 1U);
    tile->name[sizeof(tile->name) - 1U] = '\0';
    tile->opacity = opacity;
    tile->pattern_icon = pattern_icon;
    lv_label_set_text(tile->label, tile->name);
    update_pattern(tile);
    preferences = ft_preferences_get();
    lv_obj_set_style_bg_opa(tile->body,
                            effective_opacity(tile, preferences->tile_opa),
                            LV_PART_MAIN);
    return true;
}

const char *ft_tiles_test_name(size_t app_index)
{ return app_index < s_tile_count ? s_tiles[app_index].name : RT_NULL; }

uint8_t ft_tiles_test_opacity(size_t app_index)
{ return app_index < s_tile_count ? s_tiles[app_index].opacity : 0U; }

ft_icon_id_t ft_tiles_test_pattern(size_t app_index)
{ return app_index < s_tile_count ? s_tiles[app_index].pattern_icon : FT_ICON_COUNT; }

const char *ft_tiles_test_live_text(size_t app_index)
{
    if (app_index >= s_tile_count || !object_valid(s_tiles[app_index].live_label))
        return RT_NULL;
    return lv_label_get_text(s_tiles[app_index].live_label);
}

bool ft_tiles_test_live_advance(size_t app_index)
{
    if (app_index >= s_tile_count ||
        s_tiles[app_index].descriptor->app.live_content == RT_NULL) return false;
    live_content_refresh(&s_tiles[app_index], true);
    return true;
}

bool ft_tiles_test_live_enabled(size_t app_index)
{ return app_index < s_tile_count && s_tiles[app_index].live_enabled; }
#endif
