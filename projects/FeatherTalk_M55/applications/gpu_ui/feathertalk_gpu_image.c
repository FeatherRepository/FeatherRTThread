#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <rtthread.h>
#include <board.h>

#include "feather_ui.h"
#include "feathertalk_gpu_image.h"
#include "tjpgd.h"

#define FT_GPU_IMAGE_EVENT_GALLERY   (1U << FT_GPU_IMAGE_GALLERY)
#define FT_GPU_IMAGE_EVENT_WALLPAPER (1U << FT_GPU_IMAGE_WALLPAPER)
#define FT_GPU_IMAGE_EVENT_ALL       (FT_GPU_IMAGE_EVENT_GALLERY | \
                                      FT_GPU_IMAGE_EVENT_WALLPAPER)
#define FT_GPU_IMAGE_THREAD_STACK    12288U
#define FT_GPU_IMAGE_THREAD_PRIORITY 21U
#define FT_GPU_IMAGE_THREAD_SLICE    10U
#define FT_GPU_IMAGE_JPEG_WORK       4096U
#define FT_GPU_IMAGE_MAX_FILE_BYTES  (16U * 1024U * 1024U)
#define FT_GPU_IMAGE_MAX_PIXELS      4000000UL
/* This is an SRAM budget, not a display resolution. Rows are padded to a
 * 32-byte GPU boundary at runtime, allowing the same storage to serve both
 * portrait and landscape surfaces. */
#define FT_GPU_IMAGE_STAGE_BYTES          (660U * 1024U)
#define FT_GPU_IMAGE_STAGE_PIXEL_CAPACITY \
    (FT_GPU_IMAGE_STAGE_BYTES / sizeof(uint16_t))
#define FT_GPU_IMAGE_STRIDE_ALIGNMENT_PIXELS 16U

typedef struct
{
    ft_gpu_image_state_t state;
    uint16_t *pixels;
    size_t capacity_bytes;
    uint16_t width;
    uint16_t height;
    uint16_t stride_pixels;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t requested_width;
    uint16_t requested_height;
    uint32_t revision;
    uint32_t decode_ms;
    char path[256];
    char error[40];
} ft_gpu_image_slot_state_t;

typedef struct
{
    int fd;
    uint16_t *destination;
    uint16_t destination_width;
    uint16_t destination_height;
    uint16_t destination_stride;
    uint16_t source_width;
    uint16_t source_height;
} ft_gpu_jpeg_context_t;

static ft_gpu_image_slot_state_t s_slots[FT_GPU_IMAGE_SLOT_COUNT];
static struct rt_mutex s_lock;
static struct rt_event s_event;
static rt_thread_t s_thread;
static bool s_initialized;
CY_SECTION(".cy_gpu_buf") CY_ALIGN(32)
static uint16_t s_gpu_image_pixels[FT_GPU_IMAGE_STAGE_PIXEL_CAPACITY];

static uint16_t read_le16(const uint8_t *value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t read_le32(const uint8_t *value)
{
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static bool read_exact(int fd, void *buffer, size_t size)
{
    uint8_t *output = buffer;
    size_t total = 0U;
    while (total < size)
    {
        int amount = read(fd, output + total, size - total);
        if (amount <= 0) return false;
        total += (size_t)amount;
    }
    return true;
}

static void fit_dimensions(uint16_t source_width, uint16_t source_height,
                           uint16_t maximum_width, uint16_t maximum_height,
                           uint16_t *width, uint16_t *height)
{
    uint32_t scaled;
    if ((uint32_t)source_width * maximum_height >
        (uint32_t)source_height * maximum_width)
    {
        *width = maximum_width;
        scaled = ((uint32_t)source_height * maximum_width) / source_width;
        *height = (uint16_t)(scaled == 0U ? 1U : scaled);
    }
    else
    {
        *height = maximum_height;
        scaled = ((uint32_t)source_width * maximum_height) / source_height;
        *width = (uint16_t)(scaled == 0U ? 1U : scaled);
    }
}

static int ensure_buffer(ft_gpu_image_slot_state_t *slot,
                         uint16_t width, uint16_t height)
{
    uint16_t stride = (uint16_t)(((uint32_t)width +
        FT_GPU_IMAGE_STRIDE_ALIGNMENT_PIXELS - 1U) &
        ~(FT_GPU_IMAGE_STRIDE_ALIGNMENT_PIXELS - 1U));
    size_t required = (size_t)stride * height * sizeof(uint16_t);
    if (stride < width || height == 0U)
        return -RT_EINVAL;
    if (required > FT_GPU_IMAGE_STAGE_BYTES) return -RT_ENOMEM;
    slot->pixels = s_gpu_image_pixels;
    slot->capacity_bytes = FT_GPU_IMAGE_STAGE_BYTES;
    slot->stride_pixels = stride;
    memset(slot->pixels, 0, required);
    return RT_EOK;
}

static size_t jpeg_input(JDEC *decoder, uint8_t *buffer, size_t bytes)
{
    ft_gpu_jpeg_context_t *context = decoder->device;
    if (context == RT_NULL || context->fd < 0) return 0U;
    if (buffer == RT_NULL)
        return lseek(context->fd, (off_t)bytes, SEEK_CUR) < 0 ? 0U : bytes;
    {
        int amount = read(context->fd, buffer, bytes);
        return amount > 0 ? (size_t)amount : 0U;
    }
}

static int jpeg_output(JDEC *decoder, void *bitmap, JRECT *rectangle)
{
    ft_gpu_jpeg_context_t *context = decoder->device;
    const uint16_t *source = bitmap;
    uint32_t dx_begin;
    uint32_t dx_end;
    uint32_t dy_begin;
    uint32_t dy_end;
    uint16_t block_width;
    uint32_t dy;
    if (context == RT_NULL || source == RT_NULL || rectangle == RT_NULL ||
        context->destination == RT_NULL)
        return 0;

    block_width = (uint16_t)(rectangle->right - rectangle->left + 1U);
    dx_begin = ((uint32_t)rectangle->left * context->destination_width +
                context->source_width - 1U) / context->source_width;
    dx_end = ((uint32_t)(rectangle->right + 1U) * context->destination_width +
              context->source_width - 1U) / context->source_width;
    dy_begin = ((uint32_t)rectangle->top * context->destination_height +
                context->source_height - 1U) / context->source_height;
    dy_end = ((uint32_t)(rectangle->bottom + 1U) * context->destination_height +
              context->source_height - 1U) / context->source_height;
    if (dx_end > context->destination_width) dx_end = context->destination_width;
    if (dy_end > context->destination_height) dy_end = context->destination_height;
    for (dy = dy_begin; dy < dy_end; dy++)
    {
        uint32_t source_y = (dy * context->source_height) /
                            context->destination_height;
        uint16_t *destination = context->destination +
                                dy * context->destination_stride;
        uint32_t dx;
        if (source_y < rectangle->top) source_y = rectangle->top;
        if (source_y > rectangle->bottom) source_y = rectangle->bottom;
        for (dx = dx_begin; dx < dx_end; dx++)
        {
            uint32_t source_x = (dx * context->source_width) /
                                context->destination_width;
            if (source_x < rectangle->left) source_x = rectangle->left;
            if (source_x > rectangle->right) source_x = rectangle->right;
            destination[dx] = source[(source_y - rectangle->top) * block_width +
                                     source_x - rectangle->left];
        }
    }
    return 1;
}

static int decode_jpeg(ft_gpu_image_slot_state_t *slot, int fd)
{
    uint8_t work[FT_GPU_IMAGE_JPEG_WORK];
    ft_gpu_jpeg_context_t context;
    JDEC decoder;
    JRESULT result;
    uint16_t width;
    uint16_t height;
    memset(&context, 0, sizeof(context));
    context.fd = fd;
    result = jd_prepare(&decoder, jpeg_input, work, sizeof(work), &context);
    if (result != JDR_OK) return -RT_ERROR;
    if (decoder.width == 0U || decoder.height == 0U ||
        (uint32_t)decoder.width * decoder.height > FT_GPU_IMAGE_MAX_PIXELS)
        return -RT_EINVAL;
    fit_dimensions(decoder.width, decoder.height, slot->requested_width,
                   slot->requested_height, &width, &height);
    if (ensure_buffer(slot, width, height) != RT_EOK) return -RT_ENOMEM;
    context.destination = slot->pixels;
    context.destination_width = width;
    context.destination_height = height;
    context.destination_stride = slot->stride_pixels;
    context.source_width = decoder.width;
    context.source_height = decoder.height;
    result = jd_decomp(&decoder, jpeg_output, 0U);
    if (result != JDR_OK) return -RT_ERROR;
    slot->width = width;
    slot->height = height;
    slot->source_width = decoder.width;
    slot->source_height = decoder.height;
    return RT_EOK;
}

static uint16_t bmp_pixel(const uint8_t *pixel, uint16_t bits,
                          uint32_t red_mask, uint32_t green_mask,
                          uint32_t blue_mask)
{
    if (bits == 24U || bits == 32U)
    {
        uint8_t blue = pixel[0];
        uint8_t green = pixel[1];
        uint8_t red = pixel[2];
        return (uint16_t)(((uint16_t)(red >> 3) << 11) |
                          ((uint16_t)(green >> 2) << 5) | (blue >> 3));
    }
    {
        uint16_t value = read_le16(pixel);
        if (red_mask == 0xf800U && green_mask == 0x07e0U && blue_mask == 0x001fU)
            return value;
        return (uint16_t)(((value & red_mask) != 0U ?
                           ((value & red_mask) >> 10) & 0x1fU : 0U) << 11 |
                          (((value & green_mask) >> 5) & 0x1fU) << 6 |
                          (value & blue_mask));
    }
}

static int decode_bmp(ft_gpu_image_slot_state_t *slot, int fd)
{
    uint8_t header[70];
    uint8_t *row = RT_NULL;
    uint32_t pixel_offset;
    int32_t signed_width;
    int32_t signed_height;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t row_stride;
    uint32_t compression;
    uint32_t red_mask = 0x7c00U;
    uint32_t green_mask = 0x03e0U;
    uint32_t blue_mask = 0x001fU;
    uint16_t bits;
    uint16_t width;
    uint16_t height;
    uint16_t destination_y;
    bool top_down;
    int result = -RT_ERROR;
    if (lseek(fd, 0, SEEK_SET) < 0 || !read_exact(fd, header, sizeof(header)) ||
        header[0] != 'B' || header[1] != 'M' || read_le32(header + 14) < 40U)
        return -RT_EINVAL;
    pixel_offset = read_le32(header + 10);
    signed_width = (int32_t)read_le32(header + 18);
    signed_height = (int32_t)read_le32(header + 22);
    bits = read_le16(header + 28);
    compression = read_le32(header + 30);
    if (signed_width <= 0 || signed_height == 0 ||
        (bits != 16U && bits != 24U && bits != 32U) ||
        (compression != 0U && compression != 3U))
        return -RT_EINVAL;
    top_down = signed_height < 0;
    source_width = (uint32_t)signed_width;
    source_height = (uint32_t)(top_down ? -signed_height : signed_height);
    if (source_width > UINT16_MAX || source_height > UINT16_MAX ||
        (uint64_t)source_width * source_height > FT_GPU_IMAGE_MAX_PIXELS)
        return -RT_EINVAL;
    if (compression == 3U && bits == 16U)
    {
        red_mask = read_le32(header + 54);
        green_mask = read_le32(header + 58);
        blue_mask = read_le32(header + 62);
    }
    row_stride = ((source_width * bits + 31U) / 32U) * 4U;
    if (row_stride == 0U || row_stride > 262144U) return -RT_EINVAL;
    fit_dimensions((uint16_t)source_width, (uint16_t)source_height,
                   slot->requested_width, slot->requested_height,
                   &width, &height);
    if (ensure_buffer(slot, width, height) != RT_EOK) return -RT_ENOMEM;
    row = rt_malloc(row_stride);
    if (row == RT_NULL) return -RT_ENOMEM;
    for (destination_y = 0U; destination_y < height; destination_y++)
    {
        uint32_t source_y = ((uint32_t)destination_y * source_height) / height;
        uint32_t file_y = top_down ? source_y : source_height - 1U - source_y;
        uint16_t destination_x;
        uint16_t bytes_per_pixel = (uint16_t)(bits / 8U);
        if (lseek(fd, (off_t)(pixel_offset + file_y * row_stride), SEEK_SET) < 0 ||
            !read_exact(fd, row, row_stride))
            goto done;
        for (destination_x = 0U; destination_x < width; destination_x++)
        {
            uint32_t source_x = ((uint32_t)destination_x * source_width) / width;
            slot->pixels[(uint32_t)destination_y * slot->stride_pixels +
                         destination_x] =
                bmp_pixel(row + source_x * bytes_per_pixel, bits,
                          red_mask, green_mask, blue_mask);
        }
    }
    slot->width = width;
    slot->height = height;
    slot->source_width = (uint16_t)source_width;
    slot->source_height = (uint16_t)source_height;
    result = RT_EOK;
done:
    rt_free(row);
    return result;
}

static bool extension_matches(const char *path, const char *extension)
{
    const char *dot = strrchr(path, '.');
    if (dot == RT_NULL) return false;
    dot++;
    while (*dot != '\0' && *extension != '\0')
    {
        char left = *dot++;
        char right = *extension++;
        if (left >= 'A' && left <= 'Z') left = (char)(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z') right = (char)(right + ('a' - 'A'));
        if (left != right) return false;
    }
    return *dot == '\0' && *extension == '\0';
}

static int decode_path(ft_gpu_image_slot_state_t *slot)
{
    struct stat status;
    int fd;
    int result;
    if ((strncmp(slot->path, "/flash/", 7U) != 0 &&
         strncmp(slot->path, "/sdcard/", 8U) != 0) ||
        stat(slot->path, &status) != 0 || status.st_size <= 0 ||
        (uint64_t)status.st_size > FT_GPU_IMAGE_MAX_FILE_BYTES)
        return -RT_EINVAL;
    fd = open(slot->path, O_RDONLY, 0);
    if (fd < 0) return -RT_ENOENT;
    if (extension_matches(slot->path, "jpg") ||
        extension_matches(slot->path, "jpeg"))
        result = decode_jpeg(slot, fd);
    else if (extension_matches(slot->path, "bmp"))
        result = decode_bmp(slot, fd);
    else
        result = -RT_ENOSYS;
    close(fd);
    return result;
}

static const char *decode_error(int result)
{
    if (result == -RT_ENOMEM) return "NOT ENOUGH IMAGE MEMORY";
    if (result == -RT_ENOSYS) return "PNG DECODER NOT AVAILABLE";
    if (result == -RT_ENOENT) return "IMAGE FILE NOT AVAILABLE";
    if (result == -RT_EINVAL) return "UNSUPPORTED OR INVALID IMAGE";
    return "IMAGE DECODE FAILED";
}

static void process_slot(ft_gpu_image_slot_t index)
{
    ft_gpu_image_slot_state_t *slot = &s_slots[index];
    ft_gpu_image_slot_state_t working;
    uint32_t revision;
    uint32_t started;
    int result;
    memset(&working, 0, sizeof(working));
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    revision = slot->revision;
    working.requested_width = slot->requested_width;
    working.requested_height = slot->requested_height;
    rt_strncpy(working.path, slot->path, sizeof(working.path) - 1U);
    rt_mutex_release(&s_lock);
    started = rt_tick_get_millisecond();
    result = decode_path(&working);
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (slot->revision == revision && slot->state == FT_GPU_IMAGE_LOADING)
    {
        slot->decode_ms = rt_tick_get_millisecond() - started;
        if (result == RT_EOK)
        {
            slot->pixels = working.pixels;
            slot->capacity_bytes = working.capacity_bytes;
            slot->width = working.width;
            slot->height = working.height;
            slot->stride_pixels = working.stride_pixels;
            slot->source_width = working.source_width;
            slot->source_height = working.source_height;
            working.pixels = RT_NULL;
            slot->state = FT_GPU_IMAGE_READY;
            slot->error[0] = '\0';
        }
        else
        {
            slot->state = FT_GPU_IMAGE_ERROR;
            rt_strncpy(slot->error, decode_error(result), sizeof(slot->error) - 1U);
            slot->error[sizeof(slot->error) - 1U] = '\0';
        }
    }
    rt_mutex_release(&s_lock);
    fui_engine_invalidate();
}

static void image_worker(void *parameter)
{
    (void)parameter;
    for (;;)
    {
        rt_uint32_t events = 0U;
        if (rt_event_recv(&s_event, FT_GPU_IMAGE_EVENT_ALL,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &events) != RT_EOK)
            continue;
        if ((events & FT_GPU_IMAGE_EVENT_GALLERY) != 0U)
            process_slot(FT_GPU_IMAGE_GALLERY);
        if ((events & FT_GPU_IMAGE_EVENT_WALLPAPER) != 0U)
            process_slot(FT_GPU_IMAGE_WALLPAPER);
    }
}

int ft_gpu_image_init(void)
{
    if (s_initialized) return RT_EOK;
    memset(s_slots, 0, sizeof(s_slots));
    if (rt_mutex_init(&s_lock, "fui_img", RT_IPC_FLAG_PRIO) != RT_EOK)
        return -RT_ERROR;
    if (rt_event_init(&s_event, "fui_img", RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        rt_mutex_detach(&s_lock);
        return -RT_ERROR;
    }
    s_thread = rt_thread_create("fui_image", image_worker, RT_NULL,
                                FT_GPU_IMAGE_THREAD_STACK,
                                FT_GPU_IMAGE_THREAD_PRIORITY,
                                FT_GPU_IMAGE_THREAD_SLICE);
    if (s_thread == RT_NULL) return -RT_ENOMEM;
    if (rt_thread_startup(s_thread) != RT_EOK) return -RT_ERROR;
    s_initialized = true;
    return RT_EOK;
}

int ft_gpu_image_request(ft_gpu_image_slot_t index, const char *path,
                         uint16_t maximum_width, uint16_t maximum_height)
{
    ft_gpu_image_slot_state_t *slot;
    if (!s_initialized || index >= FT_GPU_IMAGE_SLOT_COUNT || path == RT_NULL ||
        maximum_width == 0U || maximum_height == 0U || strlen(path) >= 256U)
        return -RT_EINVAL;
    slot = &s_slots[index];
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    {
        unsigned other;
        for (other = 0U; other < FT_GPU_IMAGE_SLOT_COUNT; other++)
            if (other != (unsigned)index)
                s_slots[other].state = FT_GPU_IMAGE_EMPTY;
    }
    slot->revision++;
    slot->state = FT_GPU_IMAGE_LOADING;
    slot->requested_width = maximum_width;
    slot->requested_height = maximum_height;
    slot->decode_ms = 0U;
    slot->error[0] = '\0';
    rt_strncpy(slot->path, path, sizeof(slot->path) - 1U);
    slot->path[sizeof(slot->path) - 1U] = '\0';
    rt_mutex_release(&s_lock);
    return rt_event_send(&s_event, 1U << index);
}

void ft_gpu_image_clear(ft_gpu_image_slot_t index)
{
    ft_gpu_image_slot_state_t *slot;
    if (!s_initialized || index >= FT_GPU_IMAGE_SLOT_COUNT) return;
    slot = &s_slots[index];
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    slot->revision++;
    slot->state = FT_GPU_IMAGE_EMPTY;
    slot->width = 0U;
    slot->height = 0U;
    slot->stride_pixels = 0U;
    slot->source_width = 0U;
    slot->source_height = 0U;
    slot->path[0] = '\0';
    slot->error[0] = '\0';
    rt_mutex_release(&s_lock);
}

bool ft_gpu_image_get(ft_gpu_image_slot_t index, ft_gpu_image_info_t *info)
{
    const ft_gpu_image_slot_state_t *slot;
    if (!s_initialized || index >= FT_GPU_IMAGE_SLOT_COUNT || info == RT_NULL)
        return false;
    slot = &s_slots[index];
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    memset(info, 0, sizeof(*info));
    info->state = slot->state;
    info->image.pixels = slot->pixels;
    info->image.width = slot->width;
    info->image.height = slot->height;
    info->image.stride_pixels = slot->stride_pixels;
    info->source_width = slot->source_width;
    info->source_height = slot->source_height;
    info->decode_ms = slot->decode_ms;
    rt_strncpy(info->path, slot->path, sizeof(info->path) - 1U);
    rt_strncpy(info->error, slot->error, sizeof(info->error) - 1U);
    rt_mutex_release(&s_lock);
    return true;
}
