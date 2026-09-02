#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rtthread.h>
#include <rtdevice.h>
#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#include "cy_pdl.h"
#include "feathertalk_icon_vector_assets.h"
#include "feathertalk_ui_internal.h"
#include "lv_gpu_batch.h"
#include "lv_os.h"
#include "vg_lite.h"

#define FT_VG_TEST_WIDTH        64U
#define FT_VG_TEST_HEIGHT       64U
#define FT_VG_TEST_TIMEOUT_MS   8000U

typedef enum
{
    FT_VG_PATH_INLINE = 0,
    FT_VG_PATH_UPLOAD_ONLY,
    FT_VG_PATH_CALL_DEFAULT,
    FT_VG_PATH_CALL_NO_STALL,
    FT_VG_PATH_CALL_WITH_STALL
} ft_vg_path_test_mode_t;

typedef struct
{
    struct rt_completion completion;
    ft_vg_path_test_mode_t mode;
    bool use_asset;
    bool frame_loop;
    bool omit_end;
    bool close_end;
    uint32_t iterations;
    vg_lite_error_t result;
    uint32_t checksum;
    uint32_t changed_pixels;
} ft_vg_path_test_request_t;

/* A deliberately small, known-good FP32 triangle.  Each opcode occupies one
 * 32-bit word and every coordinate is an IEEE-754 word, matching the native
 * streams generated for the product SVG assets. */
static uint32_t s_triangle_path[] =
{
    VLC_OP_MOVE, 0x41000000U, 0x41000000U, /*  8.0,  8.0 */
    VLC_OP_LINE, 0x42600000U, 0x41000000U, /* 56.0,  8.0 */
    VLC_OP_LINE, 0x42000000U, 0x42600000U, /* 32.0, 56.0 */
    VLC_OP_END
};

static uint32_t s_triangle_close_path[] =
{
    VLC_OP_MOVE, 0x41000000U, 0x41000000U,
    VLC_OP_LINE, 0x42600000U, 0x41000000U,
    VLC_OP_LINE, 0x42000000U, 0x42600000U,
    VLC_OP_CLOSE
};

static void invalidate_cpu_view(const void *memory, size_t size)
{
    uintptr_t start = (uintptr_t)memory &
        ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1U);
    uintptr_t end = ((uintptr_t)memory + size + __SCB_DCACHE_LINE_SIZE - 1U) &
        ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1U);

    SCB_InvalidateDCache_by_Addr((void *)start, (int32_t)(end - start));
}

static uint32_t checksum_words(const uint32_t *words, size_t count)
{
    uint32_t hash = 2166136261UL;
    size_t index;

    for (index = 0U; index < count; index++)
    {
        hash ^= words[index];
        hash *= 16777619UL;
    }
    return hash;
}

static void dump_uploaded_path(const vg_lite_path_t *path)
{
    const uint32_t *words = (const uint32_t *)path->uploaded.memory;
    uint32_t word_count = path->uploaded.bytes / sizeof(uint32_t);
    uint32_t index;

    rt_kprintf("[VG-CALL] upload handle=%p cpu=%p gpu=0x%08lx bytes=%lu "
               "align8=%lu align64=%lu property=0x%08lx\n",
               path->uploaded.handle, path->uploaded.memory,
               (unsigned long)path->uploaded.address,
               (unsigned long)path->uploaded.bytes,
               (unsigned long)(path->uploaded.address & 7U),
               (unsigned long)(path->uploaded.address & 63U),
               (unsigned long)path->uploaded.property);
    if (word_count <= 32U)
    {
        rt_kprintf("[VG-CALL] stream");
        for (index = 0U; index < word_count; index++)
            rt_kprintf(" %08lx", (unsigned long)words[index]);
        rt_kprintf("\n");
    }
    else
    {
        rt_kprintf("[VG-CALL] stream head");
        for (index = 0U; index < 8U; index++)
            rt_kprintf(" %08lx", (unsigned long)words[index]);
        rt_kprintf(" ... tail");
        for (index = word_count - 8U; index < word_count; index++)
            rt_kprintf(" %08lx", (unsigned long)words[index]);
        rt_kprintf("\n");
    }
    rt_kprintf("[VG-CALL] expected call=0x%08lx address=0x%08lx qwords=%lu\n",
               (unsigned long)(0x60000000UL | ((path->uploaded.bytes + 7U) / 8U)),
               (unsigned long)path->uploaded.address,
               (unsigned long)((path->uploaded.bytes + 7U) / 8U));
}

static vg_lite_error_t run_path_test(ft_vg_path_test_request_t *request)
{
    vg_lite_buffer_t target;
    vg_lite_path_t path;
    vg_lite_matrix_t matrix;
    vg_lite_error_t result;
    vg_lite_uint32_t used_before = 0U;
    vg_lite_uint32_t used_after = 0U;
    vg_lite_uint32_t capacity = 0U;
    vg_lite_uint32_t chip_id = 0U;
    vg_lite_uint32_t chip_rev = 0U;
    char product[32];
    uint32_t *pixels;
    const uint32_t *path_data;
    uint32_t path_bytes;
    float min_x;
    float min_y;
    float max_x;
    float max_y;
    uint32_t background;
    uint32_t pixel_count;
    uint32_t index;
    uint32_t frame;
    bool uploaded = false;
    bool use_call = false;

    memset(&target, 0, sizeof(target));
    memset(&path, 0, sizeof(path));
    memset(product, 0, sizeof(product));
    request->checksum = 0U;
    request->changed_pixels = 0U;

    lv_gpu_batch_force_sync(LV_GPU_BATCH_BOUNDARY_EXPLICIT);
    result = vg_lite_finish();
    if (result != VG_LITE_SUCCESS)
    {
        rt_kprintf("[VG-CALL] preflight finish failed=%d\n", result);
        return result;
    }

    (void)vg_lite_get_product_info(product, &chip_id, &chip_rev);
    rt_kprintf("[VG-CALL] begin mode=%d source=%s%s%s iterations=%lu product=%s "
               "chip=0x%08lx rev=0x%08lx\n", request->mode,
               request->use_asset ? "tile-pattern" : "triangle",
               request->omit_end ? "-no-END" : "",
               request->close_end ? "-CLOSE" : "",
               (unsigned long)request->iterations, product,
               (unsigned long)chip_id, (unsigned long)chip_rev);

    if (request->mode == FT_VG_PATH_CALL_NO_STALL)
        vg_lite_set_call_stall_for_diagnostics(0);
    else if (request->mode == FT_VG_PATH_CALL_WITH_STALL)
        vg_lite_set_call_stall_for_diagnostics(1);
    else
        vg_lite_set_call_stall_for_diagnostics(-1);

    target.width = FT_VG_TEST_WIDTH;
    target.height = FT_VG_TEST_HEIGHT;
    target.format = VG_LITE_BGRA8888;
    target.tiled = VG_LITE_LINEAR;
    result = vg_lite_allocate(&target);
    if (result != VG_LITE_SUCCESS)
    {
        rt_kprintf("[VG-CALL] target allocate failed=%d\n", result);
        goto done;
    }
    rt_kprintf("[VG-CALL] target cpu=%p gpu=0x%08lx stride=%ld bytes=%lu "
               "align64=%lu\n", target.memory,
               (unsigned long)target.address, (long)target.stride,
               (unsigned long)((uint32_t)target.stride * target.height),
               (unsigned long)(target.address & 63U));

    if (request->use_asset)
    {
        path_data = ft_icon_vector_tile_pattern.fill_path;
        path_bytes = ft_icon_vector_tile_pattern.fill_path_bytes;
        min_x = ft_icon_vector_tile_pattern.fill_min_x;
        min_y = ft_icon_vector_tile_pattern.fill_min_y;
        max_x = ft_icon_vector_tile_pattern.fill_max_x;
        max_y = ft_icon_vector_tile_pattern.fill_max_y;
    }
    else
    {
        path_data = request->close_end ? s_triangle_close_path : s_triangle_path;
        path_bytes = request->close_end ? sizeof(s_triangle_close_path) :
                     sizeof(s_triangle_path) -
                     (request->omit_end ? sizeof(uint32_t) : 0U);
        min_x = 8.0f;
        min_y = 8.0f;
        max_x = 56.0f;
        max_y = 56.0f;
    }
    rt_kprintf("[VG-CALL] native bytes=%lu mod8=%lu bounds=%ld,%ld..%ld,%ld\n",
               (unsigned long)path_bytes, (unsigned long)(path_bytes & 7U),
               (long)min_x, (long)min_y, (long)max_x, (long)max_y);
    if (request->close_end)
    {
        /* vg_lite_init_path() treats a terminal CLOSE as legacy input and
         * overwrites it with END.  Attach the static stream after initializing
         * only the descriptor so this diagnostic really reaches the GPU with
         * CLOSE as the final word and no implicit 8-byte padding. */
        result = vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH,
                                   0U, NULL,
                                   min_x, min_y, max_x, max_y);
        if (result == VG_LITE_SUCCESS)
        {
            path.path = (void *)path_data;
            path.path_length = path_bytes;
            path.path_changed = 1;
        }
    }
    else
    {
        result = vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH,
                                   path_bytes, (void *)path_data,
                                   min_x, min_y, max_x, max_y);
    }
    if (result != VG_LITE_SUCCESS)
    {
        rt_kprintf("[VG-CALL] init path failed=%d\n", result);
        goto done;
    }
    vg_lite_identity(&matrix);

    if (request->mode != FT_VG_PATH_INLINE)
    {
        result = vg_lite_upload_path(&path);
        if (result != VG_LITE_SUCCESS)
        {
            rt_kprintf("[VG-CALL] upload failed=%d\n", result);
            goto done;
        }
        uploaded = true;
        dump_uploaded_path(&path);
        use_call = request->mode != FT_VG_PATH_UPLOAD_ONLY;
        if (!use_call)
            VLM_PATH_DISABLE_UPLOAD(path);
    }

    vg_lite_get_command_buffer_usage(&used_before, &capacity);
    rt_kprintf("[VG-CALL] render start tick=%lu\n",
               (unsigned long)rt_tick_get_millisecond());
    if (request->frame_loop)
    {
        for (frame = 0U; frame < request->iterations; frame++)
        {
            result = vg_lite_clear(&target, NULL, 0x102030FFUL);
            if (result == VG_LITE_SUCCESS)
                result = vg_lite_draw(&target, &path,
                                      VG_LITE_FILL_NON_ZERO, &matrix,
                                      VG_LITE_BLEND_SRC_OVER, 0xE05020FFUL);
            if (frame == 0U)
                vg_lite_get_command_buffer_usage(&used_after, NULL);
            if (result == VG_LITE_SUCCESS) result = vg_lite_finish();
            if (result != VG_LITE_SUCCESS)
            {
                rt_kprintf("[VG-CALL] frame=%lu failed=%d\n",
                           (unsigned long)frame, result);
                break;
            }
        }
    }
    else
    {
        result = vg_lite_clear(&target, NULL, 0x102030FFUL);
        for (index = 0U; result == VG_LITE_SUCCESS &&
             index < request->iterations; index++)
            result = vg_lite_draw(&target, &path, VG_LITE_FILL_NON_ZERO,
                                  &matrix, VG_LITE_BLEND_SRC_OVER,
                                  0xE05020FFUL);
        vg_lite_get_command_buffer_usage(&used_after, NULL);
        if (result == VG_LITE_SUCCESS) result = vg_lite_finish();
    }
    rt_kprintf("[VG-CALL] rendered path=%s cmd_before=%lu first_cmd=%lu "
               "delta=%lu capacity=%lu end_tick=%lu result=%d\n",
               use_call ? "CALL" : "INLINE",
               (unsigned long)used_before, (unsigned long)used_after,
               (unsigned long)(used_after - used_before),
               (unsigned long)capacity,
               (unsigned long)rt_tick_get_millisecond(), result);
    if (result != VG_LITE_SUCCESS)
    {
        /* A timed-out CALL leaves GPU/context ownership uncertain.  Do not
         * submit cleanup work; an external debugger reset is the safe next
         * step and the serial diagnostics above remain available. */
        return result;
    }

    pixel_count = ((uint32_t)target.stride / sizeof(uint32_t)) *
                  (uint32_t)target.height;
    invalidate_cpu_view(target.memory, (size_t)target.stride * target.height);
    pixels = (uint32_t *)target.memory;
    background = pixels[0];
    for (index = 0U; index < pixel_count; index++)
        if (pixels[index] != background) request->changed_pixels++;
    request->checksum = checksum_words(pixels, pixel_count);
    rt_kprintf("[VG-CALL] verify bg=0x%08lx changed=%lu checksum=0x%08lx\n",
               (unsigned long)background,
               (unsigned long)request->changed_pixels,
               (unsigned long)request->checksum);
    if (request->changed_pixels == 0U)
        result = VG_LITE_GENERIC_IO;

done:
    if (path.path != NULL || uploaded)
        (void)vg_lite_clear_path(&path);
    if (target.handle != NULL)
        (void)vg_lite_free(&target);
    vg_lite_set_call_stall_for_diagnostics(-1);
    return result;
}

static void path_test_async_cb(void *user_data)
{
    ft_vg_path_test_request_t *request =
        (ft_vg_path_test_request_t *)user_data;

    if (request == RT_NULL) return;
    request->result = run_path_test(request);
    rt_completion_done(&request->completion);
}

static bool parse_mode(const char *text, ft_vg_path_test_mode_t *mode,
                       bool *use_asset, bool *frame_loop, bool *omit_end,
                       bool *close_end)
{
    *use_asset = false;
    *frame_loop = false;
    *omit_end = false;
    *close_end = false;
    if (strcmp(text, "inline") == 0) *mode = FT_VG_PATH_INLINE;
    else if (strcmp(text, "upload") == 0) *mode = FT_VG_PATH_UPLOAD_ONLY;
    else if (strcmp(text, "call") == 0) *mode = FT_VG_PATH_CALL_DEFAULT;
    else if (strcmp(text, "call-nostall") == 0) *mode = FT_VG_PATH_CALL_NO_STALL;
    else if (strcmp(text, "call-stall") == 0) *mode = FT_VG_PATH_CALL_WITH_STALL;
    else if (strcmp(text, "asset-inline") == 0)
    {
        *mode = FT_VG_PATH_INLINE;
        *use_asset = true;
    }
    else if (strcmp(text, "asset-call") == 0)
    {
        *mode = FT_VG_PATH_CALL_DEFAULT;
        *use_asset = true;
    }
    else if (strcmp(text, "asset-call-nostall") == 0)
    {
        *mode = FT_VG_PATH_CALL_NO_STALL;
        *use_asset = true;
    }
    else if (strcmp(text, "asset-frames") == 0)
    {
        *mode = FT_VG_PATH_CALL_DEFAULT;
        *use_asset = true;
        *frame_loop = true;
    }
    else if (strcmp(text, "noend-inline") == 0)
    {
        *mode = FT_VG_PATH_INLINE;
        *omit_end = true;
    }
    else if (strcmp(text, "noend-call") == 0)
    {
        *mode = FT_VG_PATH_CALL_DEFAULT;
        *omit_end = true;
    }
    else if (strcmp(text, "close-inline") == 0)
    {
        *mode = FT_VG_PATH_INLINE;
        *close_end = true;
    }
    else if (strcmp(text, "close-call") == 0)
    {
        *mode = FT_VG_PATH_CALL_DEFAULT;
        *close_end = true;
    }
    else return false;
    return true;
}

static int feather_vg_path_test(int argc, char **argv)
{
    ft_vg_path_test_request_t request;
    lv_result_t schedule_result;
    rt_err_t wait_result;

    if ((argc != 2 && argc != 3) ||
        !parse_mode(argv[1], &request.mode, &request.use_asset,
                    &request.frame_loop, &request.omit_end,
                    &request.close_end))
    {
        rt_kprintf("usage: feather_vg_path_test "
                   "inline|upload|call|call-nostall|call-stall|"
                   "asset-inline|asset-call|asset-call-nostall|"
                   "asset-frames|noend-inline|noend-call|"
                   "close-inline|close-call [iterations]\n");
        return -RT_EINVAL;
    }

    request.iterations = argc == 3 ? (uint32_t)strtoul(argv[2], NULL, 0) :
                         (request.frame_loop ? 100U : 1U);
    if (request.iterations == 0U || request.iterations > 512U)
    {
        rt_kprintf("iterations must be 1..512\n");
        return -RT_EINVAL;
    }

    request.result = VG_LITE_GENERIC_IO;
    request.checksum = 0U;
    request.changed_pixels = 0U;
    rt_completion_init(&request.completion);
    lv_lock();
    schedule_result = lv_async_call(path_test_async_cb, &request);
    lv_unlock();
    if (schedule_result != LV_RESULT_OK)
    {
        rt_kprintf("[VG-CALL] scheduling failed=%d\n", schedule_result);
        return -RT_ENOMEM;
    }
    wait_result = rt_completion_wait(&request.completion,
        rt_tick_from_millisecond(FT_VG_TEST_TIMEOUT_MS));
    if (wait_result != RT_EOK)
    {
        rt_kprintf("[VG-CALL] host wait timeout=%d; reset target before retry\n",
                   wait_result);
        return wait_result;
    }
    rt_kprintf("[VG-CALL] complete result=%d changed=%lu checksum=0x%08lx\n",
               request.result, (unsigned long)request.changed_pixels,
               (unsigned long)request.checksum);
    return request.result == VG_LITE_SUCCESS ? RT_EOK : -RT_ERROR;
}
MSH_CMD_EXPORT(feather_vg_path_test,
               A/B test inline and uploaded VG-Lite vector paths.);
