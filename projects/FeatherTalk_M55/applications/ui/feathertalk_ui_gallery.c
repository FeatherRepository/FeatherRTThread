#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <rtthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "feathertalk_storage.h"
#include "feathertalk_ui.h"
#include "feathertalk_ui_gallery.h"
#include "feathertalk_ui_internal.h"
#include "lv_image_decoder_private.h"

#define FT_GALLERY_MAX_ENTRIES        64U
#define FT_GALLERY_MONITOR_PERIOD_MS 500U
#define FT_GALLERY_PNG_MAX_PIXELS 160000ULL
#define FT_GALLERY_LARGE_MAX_PIXELS 4000000ULL
#define FT_GALLERY_PNG_MAX_BYTES (2ULL * 1024ULL * 1024ULL)
#define FT_GALLERY_LARGE_MAX_BYTES (16ULL * 1024ULL * 1024ULL)
#define FT_GALLERY_INVALID_INDEX ((size_t)-1)
#define FT_GALLERY_THUMB_WIDTH        64U
#define FT_GALLERY_THUMB_HEIGHT       48U
#define FT_GALLERY_THUMB_PERIOD_MS   160U
#define FT_GALLERY_PREVIEW_DELAY_MS   40U
#define FT_GALLERY_FLASH_COLLECTION FT_STORAGE_FLASH_MOUNT_PATH "/Pictures"
#define FT_GALLERY_SD_COLLECTION    FT_STORAGE_SD_MOUNT_PATH "/Pictures"

typedef enum
{
    FT_GALLERY_ENTRY_IMAGE = 0
} ft_gallery_entry_kind_t;

typedef enum
{
    FT_GALLERY_IMAGE_NONE = 0,
    FT_GALLERY_IMAGE_PNG,
    FT_GALLERY_IMAGE_JPEG,
    FT_GALLERY_IMAGE_BMP
} ft_gallery_image_format_t;

typedef enum
{
    FT_GALLERY_ERROR_NONE = 0,
    FT_GALLERY_ERROR_MEDIA,
    FT_GALLERY_ERROR_SIZE,
    FT_GALLERY_ERROR_DECODE,
    FT_GALLERY_ERROR_RESOLUTION,
    FT_GALLERY_ERROR_PATH
} ft_gallery_error_t;

typedef struct
{
    ft_gallery_entry_kind_t kind;
    char name[FT_STORAGE_NAME_MAX];
    uint64_t size_bytes;
    lv_obj_t *row;
    lv_obj_t *thumbnail_image;
    lv_obj_t *thumbnail_placeholder;
    lv_obj_t *detail_label;
    ft_gallery_rendered_image_t thumbnail;
} ft_gallery_entry_t;

typedef struct
{
    ft_gallery_entry_kind_t kind;
    bool failed;
} ft_gallery_list_context_t;

typedef struct
{
    ft_gallery_image_format_t format;
    uint32_t width;
    uint32_t height;
    uint64_t file_size;
} ft_gallery_image_validation_t;

static lv_obj_t *s_page;
static lv_obj_t *s_title_label;
static lv_obj_t *s_hint_label;
static lv_obj_t *s_browser;
static lv_obj_t *s_source_buttons[FT_GALLERY_SOURCE_COUNT];
static lv_obj_t *s_source_labels[FT_GALLERY_SOURCE_COUNT];
static lv_obj_t *s_path_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_refresh_button;
static lv_obj_t *s_refresh_label;
static lv_obj_t *s_list;
static lv_obj_t *s_viewer;
static lv_obj_t *s_image_host;
static lv_obj_t *s_image;
static lv_obj_t *s_image_loading;
static lv_obj_t *s_image_info;
static lv_obj_t *s_image_error;
static lv_obj_t *s_previous_button;
static lv_obj_t *s_previous_label;
static lv_obj_t *s_next_button;
static lv_obj_t *s_next_label;
static lv_obj_t *s_close_button;
static lv_obj_t *s_close_label;
static lv_obj_t *s_wallpaper_button;
static lv_obj_t *s_wallpaper_label;
static lv_obj_t *s_delete_button;
static lv_obj_t *s_delete_label;
static lv_obj_t *s_delete_box;
static lv_obj_t *s_delete_cancel;
static lv_obj_t *s_delete_confirm;
static lv_timer_t *s_monitor_timer;
static lv_timer_t *s_thumbnail_timer;
static lv_timer_t *s_preview_timer;

static ft_gallery_entry_t s_entries[FT_GALLERY_MAX_ENTRIES];
static size_t s_entry_count;
static size_t s_image_count;
static size_t s_current_entry = FT_GALLERY_INVALID_INDEX;
static ft_gallery_source_t s_source = FT_GALLERY_SOURCE_FLASH;
static char s_current_path[FT_STORAGE_PATH_MAX];
static char s_current_file[FT_STORAGE_PATH_MAX];
static char s_decoder_path[FT_STORAGE_PATH_MAX + 3U];
static char s_pending_file[FT_STORAGE_PATH_MAX];
static lv_image_header_t s_image_header;
static uint64_t s_image_size;
static bool s_viewer_active;
static bool s_external_viewer;
static bool s_image_valid;
static bool s_image_decoder_verified;
static bool s_preview_loading;
static bool s_list_truncated;
static size_t s_thumbnail_next;
static size_t s_thumbnail_ready_count;
static ft_gallery_error_t s_image_error_code;
static uint8_t s_source_signature[FT_GALLERY_SOURCE_COUNT];
static ft_gallery_rendered_image_t s_preview_cache;

static bool gallery_source_available(ft_gallery_source_t source);
static void gallery_close_delete_confirmation(void);
static void gallery_stop_thumbnail_loader(void);
static void gallery_resume_thumbnail_loader(void);

static bool gallery_object_valid(lv_obj_t *obj)
{
    return obj != RT_NULL && lv_obj_is_valid(obj);
}

static void gallery_style_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *gallery_create_button(lv_obj_t *parent, const char *text,
                                       lv_event_cb_t callback, void *user_data,
                                       lv_obj_t **label_out)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);

    lv_obj_set_height(button, ft_layout_get()->control_height);
    lv_obj_set_style_radius(button, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    ft_ui_register_accent(button, FT_ACCENT_BACKGROUND);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
    lv_obj_center(label);
    if (callback != RT_NULL)
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    if (label_out != RT_NULL) *label_out = label;
    return button;
}

static const char *gallery_collection_path(ft_gallery_source_t source)
{
    return source == FT_GALLERY_SOURCE_SD ? FT_GALLERY_SD_COLLECTION :
                                            FT_GALLERY_FLASH_COLLECTION;
}

static bool gallery_collection_is_directory(ft_gallery_source_t source)
{
    struct stat status;
    return stat(gallery_collection_path(source), &status) == 0 &&
           S_ISDIR(status.st_mode);
}

static int gallery_ensure_collection(ft_gallery_source_t source)
{
    const char *path = gallery_collection_path(source);

    if (!gallery_source_available(source)) return -RT_EBUSY;
    if (gallery_collection_is_directory(source)) return RT_EOK;
    if (mkdir(path, 0777) == 0) return RT_EOK;
    /* Another task can create the directory between stat() and mkdir(). */
    return gallery_collection_is_directory(source) ? RT_EOK : -RT_EIO;
}

static int gallery_get_device_info(ft_gallery_source_t source,
                                   ft_storage_device_info_t *info)
{
    if (info == RT_NULL) return -RT_EINVAL;
    return source == FT_GALLERY_SOURCE_SD ? ft_storage_get_device_info(info) :
                                            ft_storage_get_flash_info(info);
}

static uint8_t gallery_source_state(ft_gallery_source_t source,
                                    ft_storage_device_info_t *info_out)
{
    ft_storage_device_info_t info;
    uint8_t state;

    rt_memset(&info, 0, sizeof(info));
    if (gallery_get_device_info(source, &info) != RT_EOK)
        state = 0U;
    else if (!info.present)
        state = 1U;
    else if (info.usb_exported)
        state = 2U;
    else if (info.busy)
        state = 3U;
    else if (!info.mounted)
        state = 4U;
    else
        state = 5U;
    if (info_out != RT_NULL) *info_out = info;
    return state;
}

static bool gallery_source_available(ft_gallery_source_t source)
{
    return gallery_source_state(source, RT_NULL) == 5U;
}

static const char *gallery_state_text(uint8_t state)
{
    switch (state)
    {
    case 1U:
        return ft_preferences_text("未插入", "Absent");
    case 2U:
        return ft_preferences_text("USB 正在使用", "In use by USB");
    case 3U:
        return ft_preferences_text("设备忙", "Busy");
    case 4U:
        return ft_preferences_text("未挂载", "Not mounted");
    case 5U:
        return ft_preferences_text("可用", "Available");
    default:
        return ft_preferences_text("不可用", "Unavailable");
    }
}

static bool gallery_path_has_root(const char *path, const char *root)
{
    size_t root_length;
    if (path == RT_NULL || root == RT_NULL) return false;
    root_length = strlen(root);
    return strncmp(path, root, root_length) == 0 &&
           (path[root_length] == '\0' || path[root_length] == '/');
}

static bool gallery_current_path_safe(void)
{
    return strcmp(s_current_path, gallery_collection_path(s_source)) == 0;
}

static bool gallery_name_safe(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;
    if (name == RT_NULL || name[0] == '\0' ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;
    while (*cursor != '\0')
    {
        if (*cursor < 0x20U || *cursor == '/' || *cursor == '\\' ||
            *cursor == ':')
            return false;
        cursor++;
    }
    return true;
}

static bool gallery_managed_file_source(const char *file_path,
                                        ft_gallery_source_t *source_out)
{
    static const char *roots[] =
    {
        FT_STORAGE_FLASH_MOUNT_PATH,
        FT_STORAGE_SD_MOUNT_PATH,
    };
    size_t source;
    size_t length;

    if (file_path == RT_NULL || file_path[0] != '/' ||
        strstr(file_path, "/../") != RT_NULL ||
        strstr(file_path, "/./") != RT_NULL ||
        strstr(file_path, "//") != RT_NULL)
        return false;
    length = strlen(file_path);
    if ((length >= 3U && strcmp(file_path + length - 3U, "/..") == 0) ||
        (length >= 2U && strcmp(file_path + length - 2U, "/.") == 0))
        return false;
    for (source = 0U; source < FT_GALLERY_SOURCE_COUNT; source++)
    {
        size_t root_length = strlen(roots[source]);
        if (strncmp(file_path, roots[source], root_length) == 0 &&
            file_path[root_length] == '/' && file_path[root_length + 1U] != '\0')
        {
            if (source_out != RT_NULL)
                *source_out = (ft_gallery_source_t)source;
            return true;
        }
    }
    return false;
}

static ft_gallery_image_format_t gallery_image_format(const char *name)
{
    const char *extension;
    char lower[8];
    size_t i;

    if (name == RT_NULL) return FT_GALLERY_IMAGE_NONE;
    extension = strrchr(name, '.');
    if (extension == RT_NULL || extension[1] == '\0')
        return FT_GALLERY_IMAGE_NONE;
    extension++;
    for (i = 0U; i + 1U < sizeof(lower) && extension[i] != '\0'; i++)
        lower[i] = (char)tolower((unsigned char)extension[i]);
    lower[i] = '\0';
    if (extension[i] != '\0') return FT_GALLERY_IMAGE_NONE;
    if (strcmp(lower, "png") == 0) return FT_GALLERY_IMAGE_PNG;
    if (strcmp(lower, "jpg") == 0 || strcmp(lower, "jpeg") == 0)
        return FT_GALLERY_IMAGE_JPEG;
    if (strcmp(lower, "bmp") == 0) return FT_GALLERY_IMAGE_BMP;
    return FT_GALLERY_IMAGE_NONE;
}

static uint16_t gallery_read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t gallery_read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static uint16_t gallery_read_le16(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t gallery_read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool gallery_read_exact_at(int file, uint64_t offset,
                                  void *buffer, size_t size)
{
    uint8_t *target = buffer;
    size_t done = 0U;

    if (file < 0 || buffer == RT_NULL || offset > (uint64_t)INT_MAX ||
        lseek(file, (off_t)offset, SEEK_SET) < 0)
        return false;
    while (done < size)
    {
        int count = read(file, target + done, size - done);
        if (count <= 0) return false;
        done += (size_t)count;
    }
    return true;
}

static bool gallery_dimensions_valid(uint32_t width, uint32_t height,
                                     uint64_t pixel_limit)
{
    if (width == 0U || height == 0U || width > UINT16_MAX ||
        height > UINT16_MAX || (uint64_t)width > pixel_limit / height)
        return false;
    return (uint64_t)width * height <= pixel_limit;
}

static bool gallery_stride_valid(uint32_t width, uint32_t bytes_per_pixel)
{
    return width > 0U && bytes_per_pixel > 0U &&
           width <= UINT16_MAX / bytes_per_pixel;
}

static bool gallery_png_bit_depth_valid(uint8_t color_type, uint8_t bit_depth)
{
    switch (color_type)
    {
    case 0U:
        return bit_depth == 1U || bit_depth == 2U || bit_depth == 4U ||
               bit_depth == 8U || bit_depth == 16U;
    case 2U:
    case 4U:
    case 6U:
        return bit_depth == 8U || bit_depth == 16U;
    case 3U:
        return bit_depth == 1U || bit_depth == 2U || bit_depth == 4U ||
               bit_depth == 8U;
    default:
        return false;
    }
}

static bool gallery_png_type_is(const uint8_t *type, const char *name)
{
    return type != RT_NULL && name != RT_NULL &&
           memcmp(type, name, 4U) == 0;
}

static bool gallery_validate_png_header(int file, uint64_t file_size,
                                        uint64_t pixel_limit,
                                        uint32_t *width_out,
                                        uint32_t *height_out)
{
    static const uint8_t signature[8] =
        {0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    uint8_t header[33];
    uint8_t chunk[8];
    uint64_t position = 8U;
    uint64_t idat_bytes = 0U;
    uint32_t width;
    uint32_t height;
    uint8_t color_type;
    bool seen_ihdr = false;
    bool seen_plte = false;
    bool seen_idat = false;
    bool idat_ended = false;
    uint32_t chunk_count = 0U;

    if (file_size < sizeof(header) ||
        !gallery_read_exact_at(file, 0U, header, sizeof(header)) ||
        memcmp(header, signature, sizeof(signature)) != 0 ||
        gallery_read_be32(header + 8U) != 13U ||
        memcmp(header + 12U, "IHDR", 4U) != 0)
        return false;
    width = gallery_read_be32(header + 16U);
    height = gallery_read_be32(header + 20U);
    color_type = header[25];
    if (!gallery_dimensions_valid(width, height, pixel_limit) ||
        !gallery_stride_valid(width, 4U) ||
        !gallery_png_bit_depth_valid(color_type, header[24]) ||
        header[26] != 0U || header[27] != 0U || header[28] > 1U)
        return false;

    while (position <= file_size && file_size - position >= 12U)
    {
        uint32_t chunk_size;
        uint64_t next;
        bool critical;
        size_t i;

        if (++chunk_count > 4096U ||
            !gallery_read_exact_at(file, position, chunk, sizeof(chunk)))
            return false;
        chunk_size = gallery_read_be32(chunk);
        if ((uint64_t)chunk_size > file_size - position - 12U)
            return false;
        next = position + 12U + chunk_size;
        for (i = 4U; i < 8U; i++)
            if (!((chunk[i] >= 'A' && chunk[i] <= 'Z') ||
                  (chunk[i] >= 'a' && chunk[i] <= 'z')))
                return false;
        if (chunk[6] < 'A' || chunk[6] > 'Z') return false;
        critical = chunk[4] >= 'A' && chunk[4] <= 'Z';

        if (gallery_png_type_is(chunk + 4U, "IHDR"))
        {
            if (seen_ihdr || position != 8U || chunk_size != 13U)
                return false;
            seen_ihdr = true;
        }
        else if (gallery_png_type_is(chunk + 4U, "PLTE"))
        {
            if (!seen_ihdr || seen_plte || seen_idat || chunk_size == 0U ||
                chunk_size > 768U || (chunk_size % 3U) != 0U ||
                color_type == 0U || color_type == 4U)
                return false;
            seen_plte = true;
        }
        else if (gallery_png_type_is(chunk + 4U, "IDAT"))
        {
            if (!seen_ihdr || idat_ended ||
                idat_bytes > UINT64_MAX - chunk_size)
                return false;
            seen_idat = true;
            idat_bytes += chunk_size;
        }
        else if (gallery_png_type_is(chunk + 4U, "IEND"))
        {
            if (!seen_ihdr || !seen_idat || idat_bytes == 0U ||
                chunk_size != 0U || next != file_size ||
                (color_type == 3U && !seen_plte))
                return false;
            *width_out = width;
            *height_out = height;
            return true;
        }
        else
        {
            if (critical) return false;
            if (seen_idat) idat_ended = true;
        }
        position = next;
    }
    return false;
}

static bool gallery_jpeg_marker_is_sof(uint8_t marker)
{
    return marker >= 0xC0U && marker <= 0xCFU && marker != 0xC4U &&
           marker != 0xC8U && marker != 0xCCU;
}

static bool gallery_validate_jpeg_header(int file, uint64_t file_size,
                                         uint64_t pixel_limit,
                                         uint32_t *width_out,
                                         uint32_t *height_out)
{
    uint8_t bytes[16];
    uint64_t position = 2U;
    uint32_t width = 0U;
    uint32_t height = 0U;
    uint8_t components = 0U;
    bool seen_sof = false;
    uint32_t segment_count = 0U;

    if (file_size < 12U ||
        !gallery_read_exact_at(file, 0U, bytes, 2U) ||
        bytes[0] != 0xFFU || bytes[1] != 0xD8U ||
        !gallery_read_exact_at(file, file_size - 2U, bytes, 2U) ||
        bytes[0] != 0xFFU || bytes[1] != 0xD9U)
        return false;

    while (position + 4U <= file_size - 2U)
    {
        uint8_t marker;
        uint16_t segment_size;
        uint64_t payload_size;

        if (++segment_count > 4096U ||
            !gallery_read_exact_at(file, position, bytes, 2U) ||
            bytes[0] != 0xFFU)
            return false;
        do
        {
            marker = bytes[1];
            position++;
            if (marker == 0xFFU &&
                !gallery_read_exact_at(file, position, bytes + 1U, 1U))
                return false;
        } while (marker == 0xFFU);
        position++;
        if (marker == 0x00U || marker == 0xD8U || marker == 0xD9U ||
            marker == 0x01U || (marker >= 0xD0U && marker <= 0xD7U) ||
            position + 2U > file_size - 2U ||
            !gallery_read_exact_at(file, position, bytes, 2U))
            return false;
        segment_size = gallery_read_be16(bytes);
        if (segment_size < 2U ||
            (uint64_t)segment_size > file_size - position)
            return false;
        payload_size = segment_size - 2U;

        if (gallery_jpeg_marker_is_sof(marker))
        {
            if (marker != 0xC0U || seen_sof || payload_size < 6U ||
                !gallery_read_exact_at(file, position + 2U, bytes, 6U))
                return false;
            components = bytes[5];
            height = gallery_read_be16(bytes + 1U);
            width = gallery_read_be16(bytes + 3U);
            if (bytes[0] != 8U || (components != 1U && components != 3U) ||
                segment_size != (uint16_t)(8U + 3U * components) ||
                !gallery_dimensions_valid(width, height, pixel_limit) ||
                !gallery_stride_valid(width, 3U))
                return false;
            seen_sof = true;
        }
        else if (marker == 0xDAU)
        {
            uint8_t scan_components;
            if (!seen_sof || payload_size < 4U ||
                !gallery_read_exact_at(file, position + 2U, bytes, 1U))
                return false;
            scan_components = bytes[0];
            if (scan_components != components ||
                segment_size != (uint16_t)(6U + 2U * scan_components) ||
                position + segment_size >= file_size - 2U)
                return false;
            *width_out = width;
            *height_out = height;
            return true;
        }
        position += segment_size;
    }
    return false;
}

static bool gallery_validate_bmp_header(int file, uint64_t file_size,
                                        uint64_t pixel_limit,
                                        uint32_t *width_out,
                                        uint32_t *height_out)
{
    uint8_t header[54];
    uint32_t declared_size;
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t signed_width;
    int32_t signed_height;
    uint16_t bits_per_pixel;
    uint64_t row_bits;
    uint64_t row_bytes;
    uint64_t pixel_bytes;

    if (file_size < sizeof(header) || file_size > UINT32_MAX ||
        !gallery_read_exact_at(file, 0U, header, sizeof(header)) ||
        header[0] != 'B' || header[1] != 'M')
        return false;
    declared_size = gallery_read_le32(header + 2U);
    pixel_offset = gallery_read_le32(header + 10U);
    dib_size = gallery_read_le32(header + 14U);
    signed_width = (int32_t)gallery_read_le32(header + 18U);
    signed_height = (int32_t)gallery_read_le32(header + 22U);
    bits_per_pixel = gallery_read_le16(header + 28U);
    if (declared_size != file_size || dib_size < 40U ||
        (uint64_t)14U + dib_size > file_size || signed_width <= 0 ||
        signed_height <= 0 || gallery_read_le16(header + 26U) != 1U ||
        (bits_per_pixel != 16U && bits_per_pixel != 24U &&
         bits_per_pixel != 32U) || gallery_read_le32(header + 30U) != 0U ||
        pixel_offset < 14U + dib_size || pixel_offset >= file_size ||
        !gallery_dimensions_valid((uint32_t)signed_width,
                                  (uint32_t)signed_height, pixel_limit) ||
        !gallery_stride_valid((uint32_t)signed_width,
                              bits_per_pixel / 8U))
        return false;
    row_bits = (uint64_t)(uint32_t)signed_width * bits_per_pixel;
    row_bytes = ((row_bits + 31U) / 32U) * 4U;
    if (row_bytes > UINT64_MAX / (uint32_t)signed_height)
        return false;
    pixel_bytes = row_bytes * (uint32_t)signed_height;
    if ((uint64_t)pixel_offset > file_size ||
        pixel_bytes > file_size - pixel_offset)
        return false;
    *width_out = (uint32_t)signed_width;
    *height_out = (uint32_t)signed_height;
    return true;
}

static bool gallery_validate_image_file(
    const char *file_path, ft_gallery_source_t source,
    ft_gallery_image_format_t format,
    uint64_t byte_limit, uint64_t pixel_limit,
    ft_gallery_image_validation_t *validation)
{
    struct stat status;
    ft_gallery_source_t path_source;
    uint32_t width = 0U;
    uint32_t height = 0U;
    bool valid = false;
    int file;

    if (file_path == RT_NULL || validation == RT_NULL ||
        format == FT_GALLERY_IMAGE_NONE ||
        !gallery_managed_file_source(file_path, &path_source) ||
        path_source != source)
        return false;
    file = open(file_path, O_RDONLY, 0);
    if (file < 0) return false;
    if (fstat(file, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size <= 0 || (uint64_t)status.st_size > byte_limit ||
        (uint64_t)status.st_size > (uint64_t)INT_MAX)
        goto close_file;
    if (format == FT_GALLERY_IMAGE_PNG)
        valid = gallery_validate_png_header(file, (uint64_t)status.st_size,
                                            pixel_limit, &width, &height);
    else if (format == FT_GALLERY_IMAGE_JPEG)
        valid = gallery_validate_jpeg_header(file, (uint64_t)status.st_size,
                                             pixel_limit, &width, &height);
    else if (format == FT_GALLERY_IMAGE_BMP)
        valid = gallery_validate_bmp_header(file, (uint64_t)status.st_size,
                                            pixel_limit, &width, &height);
    if (valid)
    {
        validation->format = format;
        validation->width = width;
        validation->height = height;
        validation->file_size = (uint64_t)status.st_size;
    }

close_file:
    close(file);
    return valid;
}

static void gallery_format_bytes(uint64_t bytes, char *text, size_t text_size)
{
    static const char *units[] = {"B", "KB", "MB", "GB"};
    uint64_t value10 = bytes * 10ULL;
    size_t unit = 0U;

    if (text == RT_NULL || text_size == 0U) return;
    while (value10 >= 10240ULL && unit + 1U < sizeof(units) / sizeof(units[0]))
    {
        value10 = (value10 + 512ULL) / 1024ULL;
        unit++;
    }
    if (unit == 0U)
        rt_snprintf(text, text_size, "%lu %s",
                    (unsigned long)bytes, units[unit]);
    else
        rt_snprintf(text, text_size, "%lu.%lu %s",
                    (unsigned long)(value10 / 10ULL),
                    (unsigned long)(value10 % 10ULL), units[unit]);
}

static void gallery_release_entry_thumbnails(bool detach_sources)
{
    size_t index;
    bool has_buffers = false;

    for (index = 0U; index < FT_GALLERY_MAX_ENTRIES; index++)
    {
        if (detach_sources && gallery_object_valid(s_entries[index].thumbnail_image))
            lv_image_set_src(s_entries[index].thumbnail_image, RT_NULL);
        if (s_entries[index].thumbnail.draw_buf != RT_NULL)
            has_buffers = true;
    }
    if (has_buffers) lv_draw_wait_for_finish();
    for (index = 0U; index < FT_GALLERY_MAX_ENTRIES; index++)
        ft_gallery_release_rendered_image(&s_entries[index].thumbnail);
}

static void gallery_update_source_buttons(void)
{
    size_t i;
    char text[48];

    for (i = 0U; i < FT_GALLERY_SOURCE_COUNT; i++)
    {
        uint8_t state = gallery_source_state((ft_gallery_source_t)i, RT_NULL);
        if (!gallery_object_valid(s_source_buttons[i]) ||
            !gallery_object_valid(s_source_labels[i]))
            continue;
        rt_snprintf(text, sizeof(text), "%s\n%s",
                    i == FT_GALLERY_SOURCE_FLASH ?
                        ft_preferences_text("内置 Flash", "Internal Flash") :
                        ft_preferences_text("SD 卡", "SD card"),
                    gallery_state_text(state));
        lv_label_set_text(s_source_labels[i], text);
        lv_obj_set_style_bg_color(s_source_buttons[i],
                                  i == (size_t)s_source ? ft_ui_accent_color() :
                                                        lv_color_hex(0x202020),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_source_buttons[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_source_buttons[i],
                                      i == (size_t)s_source ? 2 : 1,
                                      LV_PART_MAIN);
        lv_obj_set_style_border_color(s_source_buttons[i],
                                      i == (size_t)s_source ? ft_ui_accent_color() :
                                                            lv_color_hex(0x484848),
                                      LV_PART_MAIN);
    }
}

static void gallery_set_path_label(void)
{
    char text[96];
    const char *source_name = s_source == FT_GALLERY_SOURCE_FLASH ?
        ft_preferences_text("内置 Flash", "Internal Flash") :
        ft_preferences_text("SD 卡", "SD card");

    if (!gallery_object_valid(s_path_label)) return;
    rt_snprintf(text, sizeof(text),
                ft_preferences_text("%s 照片集合", "%s photo collection"),
                source_name);
    lv_label_set_text(s_path_label, text);
}

static void gallery_add_empty_message(const char *text)
{
    lv_obj_t *label;
    if (!gallery_object_valid(s_list)) return;
    label = lv_label_create(s_list);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_all(label, ft_layout_px(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ft_layout_font(14), LV_PART_MAIN);
}

static void gallery_show_browser(void);
static void gallery_refresh(bool manual_refresh);
static void gallery_open_entry(size_t index);
static bool gallery_entry_decodable(const ft_storage_entry_t *entry);

static void gallery_entry_clicked_cb(lv_event_t *event)
{
    const ft_gallery_entry_t *entry = lv_event_get_user_data(event);
    size_t index;
    if (entry == RT_NULL || entry < s_entries ||
        entry >= s_entries + FT_GALLERY_MAX_ENTRIES)
        return;
    index = (size_t)(entry - s_entries);
    if (index >= s_entry_count) return;
    gallery_open_entry(index);
}

static void gallery_entry_pressed_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    /* Give touch handling priority over progressive filesystem decoding. */
    gallery_stop_thumbnail_loader();
}

static void gallery_entry_released_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    gallery_resume_thumbnail_loader();
}

static bool gallery_add_entry(const ft_storage_entry_t *storage_entry,
                              void *context)
{
    ft_gallery_list_context_t *list_context = context;
    ft_gallery_entry_t *entry;
    lv_obj_t *row;
    lv_obj_t *thumbnail_host;
    lv_obj_t *text_column;
    lv_obj_t *name;
    lv_obj_t *detail;
    char size_text[24];

    if (storage_entry == RT_NULL || list_context == RT_NULL ||
        !gallery_object_valid(s_list))
    {
        if (list_context != RT_NULL) list_context->failed = true;
        return false;
    }
    if (s_entry_count >= FT_GALLERY_MAX_ENTRIES)
    {
        s_list_truncated = true;
        return false;
    }
    if (!gallery_name_safe(storage_entry->name) ||
        strcmp(storage_entry->name, ".feathertalk") == 0)
        return true;
    if (list_context->kind != FT_GALLERY_ENTRY_IMAGE ||
        gallery_image_format(storage_entry->name) == FT_GALLERY_IMAGE_NONE ||
        !gallery_entry_decodable(storage_entry))
        return true;

    entry = &s_entries[s_entry_count];
    rt_memset(entry, 0, sizeof(*entry));
    entry->kind = list_context->kind;
    entry->size_bytes = storage_entry->size_bytes;
    rt_strncpy(entry->name, storage_entry->name, sizeof(entry->name) - 1U);

    row = lv_button_create(s_list);
    entry->row = row;
    lv_obj_set_size(row, lv_pct(100), ft_layout_px(76));
    lv_obj_set_style_bg_color(row, lv_color_hex(0x181818), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x303030),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, ft_layout_px(10), LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_ext_click_area(row, ft_layout_px(2));
    lv_obj_add_event_cb(row, gallery_entry_pressed_cb, LV_EVENT_PRESSED, entry);
    lv_obj_add_event_cb(row, gallery_entry_released_cb, LV_EVENT_RELEASED, entry);
    lv_obj_add_event_cb(row, gallery_entry_released_cb, LV_EVENT_PRESS_LOST, entry);
    lv_obj_add_event_cb(row, gallery_entry_clicked_cb, LV_EVENT_CLICKED, entry);

    thumbnail_host = lv_obj_create(row);
    lv_obj_set_size(thumbnail_host, ft_layout_px(68), ft_layout_px(54));
    lv_obj_set_style_bg_color(thumbnail_host, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(thumbnail_host, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(thumbnail_host, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(thumbnail_host, lv_color_hex(0x484848), LV_PART_MAIN);
    lv_obj_set_style_radius(thumbnail_host, ft_layout_px(2), LV_PART_MAIN);
    lv_obj_set_style_pad_all(thumbnail_host, 0, LV_PART_MAIN);
    lv_obj_remove_flag(thumbnail_host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(thumbnail_host, LV_OBJ_FLAG_CLICKABLE);
    entry->thumbnail_image = lv_image_create(thumbnail_host);
    lv_obj_remove_flag(entry->thumbnail_image, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_antialias(entry->thumbnail_image, true);
    lv_obj_center(entry->thumbnail_image);
    entry->thumbnail_placeholder = lv_label_create(thumbnail_host);
    lv_obj_remove_flag(entry->thumbnail_placeholder, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(entry->thumbnail_placeholder, "...");
    lv_obj_set_style_text_color(entry->thumbnail_placeholder,
                                lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_set_style_text_font(entry->thumbnail_placeholder,
                               ft_layout_font(14), LV_PART_MAIN);
    lv_obj_center(entry->thumbnail_placeholder);

    text_column = lv_obj_create(row);
    gallery_style_container(text_column);
    lv_obj_remove_flag(text_column, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(text_column, 0, lv_pct(100));
    lv_obj_set_flex_grow(text_column, 1);
    lv_obj_set_flex_flow(text_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_column, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(text_column, ft_layout_px(3), LV_PART_MAIN);

    name = lv_label_create(text_column);
    lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(name, entry->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_style_text_font(name, ft_layout_font(16), LV_PART_MAIN);

    detail = lv_label_create(text_column);
    lv_obj_remove_flag(detail, LV_OBJ_FLAG_CLICKABLE);
    entry->detail_label = detail;
    gallery_format_bytes(entry->size_bytes, size_text, sizeof(size_text));
    lv_label_set_text(detail, size_text);
    lv_obj_set_width(detail, lv_pct(100));
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    s_image_count++;
    lv_obj_set_style_text_color(detail, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_set_style_text_font(detail, ft_layout_font(12), LV_PART_MAIN);
    s_entry_count++;
    return true;
}

static void gallery_stop_thumbnail_loader(void)
{
    if (s_thumbnail_timer != RT_NULL)
    {
        lv_timer_delete(s_thumbnail_timer);
        s_thumbnail_timer = RT_NULL;
    }
}

static void gallery_thumbnail_timer_cb(lv_timer_t *timer)
{
    ft_gallery_entry_t *entry;
    char file_path[FT_STORAGE_PATH_MAX];
    char size_text[24];
    char detail_text[64];

    LV_UNUSED(timer);
    if (s_viewer_active || s_thumbnail_next >= s_entry_count ||
        !gallery_source_available(s_source))
    {
        gallery_stop_thumbnail_loader();
        return;
    }
    entry = &s_entries[s_thumbnail_next++];
    if (ft_storage_join_path(s_current_path, entry->name,
                             file_path, sizeof(file_path)) == RT_EOK &&
        ft_gallery_render_image_path(file_path,
                                     FT_GALLERY_THUMB_WIDTH,
                                     FT_GALLERY_THUMB_HEIGHT,
                                     &entry->thumbnail))
    {
        if (gallery_object_valid(entry->thumbnail_image))
        {
            lv_image_set_src(entry->thumbnail_image, entry->thumbnail.draw_buf);
            lv_obj_center(entry->thumbnail_image);
        }
        if (gallery_object_valid(entry->thumbnail_placeholder))
            lv_obj_add_flag(entry->thumbnail_placeholder, LV_OBJ_FLAG_HIDDEN);
        gallery_format_bytes(entry->size_bytes, size_text, sizeof(size_text));
        rt_snprintf(detail_text, sizeof(detail_text), "%lu x %lu  ·  %s",
                    (unsigned long)entry->thumbnail.source_width,
                    (unsigned long)entry->thumbnail.source_height, size_text);
        if (gallery_object_valid(entry->detail_label))
            lv_label_set_text(entry->detail_label, detail_text);
        s_thumbnail_ready_count++;
    }
    else if (gallery_object_valid(entry->thumbnail_placeholder))
    {
        lv_label_set_text(entry->thumbnail_placeholder, "!");
    }
    if (s_thumbnail_next >= s_entry_count)
        gallery_stop_thumbnail_loader();
}

static void gallery_start_thumbnail_loader(void)
{
    gallery_stop_thumbnail_loader();
    s_thumbnail_next = 0U;
    s_thumbnail_ready_count = 0U;
    if (s_entry_count > 0U && !s_viewer_active)
        s_thumbnail_timer = lv_timer_create(gallery_thumbnail_timer_cb,
                                            FT_GALLERY_THUMB_PERIOD_MS,
                                            RT_NULL);
}

static void gallery_resume_thumbnail_loader(void)
{
    if (s_thumbnail_timer == RT_NULL && !s_viewer_active &&
        s_thumbnail_next < s_entry_count && gallery_object_valid(s_browser) &&
        !lv_obj_has_flag(s_browser, LV_OBJ_FLAG_HIDDEN) &&
        gallery_source_available(s_source))
    {
        s_thumbnail_timer = lv_timer_create(gallery_thumbnail_timer_cb,
                                            FT_GALLERY_THUMB_PERIOD_MS,
                                            RT_NULL);
    }
}

static void gallery_refresh(bool manual_refresh)
{
    ft_gallery_list_context_t image_context =
        {FT_GALLERY_ENTRY_IMAGE, false};
    uint8_t state;
    int images = 0;
    char status[160];

    LV_UNUSED(manual_refresh);
    if (!gallery_object_valid(s_list) || !gallery_object_valid(s_status_label))
        return;
    if (!gallery_current_path_safe())
    {
        rt_strncpy(s_current_path, gallery_collection_path(s_source),
                   sizeof(s_current_path) - 1U);
        s_current_path[sizeof(s_current_path) - 1U] = '\0';
    }
    gallery_stop_thumbnail_loader();
    gallery_release_entry_thumbnails(true);
    lv_obj_clean(s_list);
    rt_memset(s_entries, 0, sizeof(s_entries));
    s_entry_count = 0U;
    s_image_count = 0U;
    s_thumbnail_next = 0U;
    s_thumbnail_ready_count = 0U;
    s_list_truncated = false;
    gallery_update_source_buttons();
    gallery_set_path_label();
    state = gallery_source_state(s_source, RT_NULL);
    if (state != 5U)
    {
        lv_label_set_text(s_status_label, gallery_state_text(state));
        gallery_add_empty_message(state == 2U ?
            ft_preferences_text("USB 存储正在占用此介质。停止 USB 存储后可浏览照片。",
                                "USB storage owns this medium. Stop USB storage to browse photos.") :
            s_source == FT_GALLERY_SOURCE_SD ?
            ft_preferences_text("插入并挂载 SD 卡后，可在这里浏览照片。",
                                "Insert and mount an SD card to browse photos here.") :
            ft_preferences_text("内置 Flash 暂时不可用。",
                                "Internal Flash is temporarily unavailable."));
        return;
    }

    if (gallery_ensure_collection(s_source) != RT_EOK)
    {
        lv_label_set_text(s_status_label,
                          ft_preferences_text("无法建立照片集合。",
                                              "Unable to prepare the photo collection."));
        gallery_add_empty_message(ft_preferences_text(
            "介质可能为只读、空间不足或文件系统不可写。",
            "The medium may be read-only, full, or not writable."));
        return;
    }

    images = ft_storage_list(s_current_path, FT_STORAGE_ENTRY_FILE,
                             gallery_add_entry, &image_context);
    if (images < 0 || image_context.failed)
    {
        gallery_release_entry_thumbnails(true);
        lv_obj_clean(s_list);
        rt_memset(s_entries, 0, sizeof(s_entries));
        s_entry_count = 0U;
        s_image_count = 0U;
        lv_label_set_text(s_status_label,
                          ft_preferences_text("无法读取此文件夹。",
                                              "Unable to read this folder."));
        gallery_add_empty_message(ft_preferences_text("请检查介质后重试。",
                                                      "Check the medium and try again."));
    }
    else
    {
        rt_snprintf(status, sizeof(status), s_list_truncated ?
                    ft_preferences_text("%lu 张照片 · 仅显示前 %u 项",
                                        "%lu photos · first %u items") :
                    ft_preferences_text("%lu 张照片",
                                        "%lu photos"),
                    (unsigned long)s_image_count,
                    (unsigned int)FT_GALLERY_MAX_ENTRIES);
        lv_label_set_text(s_status_label, status);
        if (s_entry_count == 0U)
            gallery_add_empty_message(ft_preferences_text(
                "Pictures 中没有可解码的照片（JPG、PNG、BMP）。",
                "Pictures has no decodable photos (JPG, PNG, BMP)."));
        else
            gallery_start_thumbnail_loader();
    }
}

static bool gallery_make_decoder_path(const char *file_path,
                                      ft_gallery_source_t source,
                                      char *decoder_path,
                                      size_t decoder_path_size)
{
    const char *extension;
    char *output_extension;
    ft_gallery_source_t path_source;
    size_t i;
    int written;

    if (decoder_path != RT_NULL && decoder_path_size > 0U)
        decoder_path[0] = '\0';
    if (file_path == RT_NULL || decoder_path == RT_NULL ||
        decoder_path_size == 0U ||
        !gallery_managed_file_source(file_path, &path_source) ||
        path_source != source)
        return false;
    written = rt_snprintf(decoder_path, decoder_path_size, "P:%s", file_path);
    if (written < 0 || (size_t)written >= decoder_path_size) return false;
    extension = strrchr(file_path, '.');
    if (extension == RT_NULL) return false;
    output_extension = decoder_path + 2U + (extension - file_path) + 1U;
    for (i = 0U; output_extension[i] != '\0'; i++)
        output_extension[i] = (char)tolower((unsigned char)output_extension[i]);
    return true;
}

static bool gallery_decoded_buffer_valid(const lv_draw_buf_t *decoded,
                                         uint32_t expected_width,
                                         uint32_t expected_height)
{
    uint64_t required_size;

    if (decoded == RT_NULL || decoded->data == RT_NULL ||
        expected_width == 0U || expected_height == 0U ||
        decoded->header.w != expected_width ||
        decoded->header.h != expected_height ||
        decoded->header.stride == 0U)
        return false;
    required_size = (uint64_t)decoded->header.stride * expected_height;
    return required_size <= decoded->data_size;
}

static bool gallery_decoder_probe(
    const char *decoder_path,
    const ft_gallery_image_validation_t *validation,
    lv_image_header_t *verified_header)
{
    lv_image_decoder_dsc_t decoder;
    lv_image_decoder_args_t args;
    lv_area_t requested_area;
    lv_area_t decoded_area;
    uint32_t decoded_width;
    uint32_t decoded_height;
    bool valid = false;

    if (decoder_path == RT_NULL || validation == RT_NULL ||
        verified_header == RT_NULL || validation->width == 0U ||
        validation->height == 0U || validation->width > UINT16_MAX ||
        validation->height > UINT16_MAX)
        return false;

    /* Never accept a stale decoded image or stale 16-bit cached header. */
    lv_image_cache_drop(decoder_path);
    rt_memset(&decoder, 0, sizeof(decoder));
    rt_memset(&args, 0, sizeof(args));
    args.no_cache = true;
    if (lv_image_decoder_open(&decoder, decoder_path, &args) != LV_RESULT_OK)
        return false;

    if (decoder.header.w != validation->width ||
        decoder.header.h != validation->height)
        goto close_decoder;

    if (decoder.decoded != RT_NULL)
    {
        valid = gallery_decoded_buffer_valid(decoder.decoded,
                                             validation->width,
                                             validation->height);
    }
    else
    {
        /* Streaming decoders are proved with one bounded tile/row, not by
         * merely accepting their header.  TJPGD produces one MCU and BMP one
         * row, so this keeps the probe buffer small. */
        requested_area.x1 = 0;
        requested_area.y1 = 0;
        requested_area.x2 = (int32_t)(validation->width > 32U ?
                                      31U : validation->width - 1U);
        requested_area.y2 = (int32_t)(validation->height > 32U ?
                                      31U : validation->height - 1U);
        decoded_area.x1 = LV_COORD_MIN;
        decoded_area.y1 = LV_COORD_MIN;
        decoded_area.x2 = LV_COORD_MIN;
        decoded_area.y2 = LV_COORD_MIN;
        if (lv_image_decoder_get_area(&decoder, &requested_area,
                                      &decoded_area) != LV_RESULT_OK ||
            decoded_area.x1 < 0 || decoded_area.y1 < 0 ||
            decoded_area.x2 < decoded_area.x1 ||
            decoded_area.y2 < decoded_area.y1 ||
            (uint32_t)decoded_area.x2 >= validation->width ||
            (uint32_t)decoded_area.y2 >= validation->height)
            goto close_decoder;
        decoded_width = (uint32_t)(decoded_area.x2 - decoded_area.x1 + 1);
        decoded_height = (uint32_t)(decoded_area.y2 - decoded_area.y1 + 1);
        valid = gallery_decoded_buffer_valid(decoder.decoded,
                                             decoded_width,
                                             decoded_height);
    }

    if (valid) *verified_header = decoder.header;

close_decoder:
    lv_image_decoder_close(&decoder);
    return valid;
}

bool ft_gallery_validate_image_path(const char *native_path,
                                    ft_gallery_source_t *source_out,
                                    char *lv_path, size_t lv_path_size,
                                    lv_image_header_t *verified_header)
{
    ft_gallery_source_t source;
    ft_gallery_image_format_t format;
    ft_gallery_image_validation_t validation;
    lv_image_header_t header;
    uint64_t byte_limit;
    uint64_t pixel_limit;
    char normalized_path[FT_STORAGE_PATH_MAX + 3U];
    size_t normalized_length;

    if (lv_path != RT_NULL && lv_path_size > 0U) lv_path[0] = '\0';
    if (verified_header != RT_NULL)
        rt_memset(verified_header, 0, sizeof(*verified_header));
    if (native_path == RT_NULL || lv_path == RT_NULL ||
        lv_path_size == 0U || verified_header == RT_NULL ||
        !gallery_managed_file_source(native_path, &source) ||
        !gallery_source_available(source))
        return false;

    format = gallery_image_format(native_path);
    if (format == FT_GALLERY_IMAGE_NONE) return false;
    byte_limit = format == FT_GALLERY_IMAGE_PNG ?
                 FT_GALLERY_PNG_MAX_BYTES : FT_GALLERY_LARGE_MAX_BYTES;
    pixel_limit = format == FT_GALLERY_IMAGE_PNG ?
                  FT_GALLERY_PNG_MAX_PIXELS : FT_GALLERY_LARGE_MAX_PIXELS;
    rt_memset(&validation, 0, sizeof(validation));
    rt_memset(&header, 0, sizeof(header));
    if (!gallery_validate_image_file(native_path, source, format, byte_limit,
                                     pixel_limit, &validation) ||
        !gallery_make_decoder_path(native_path, source, normalized_path,
                                   sizeof(normalized_path)) ||
        !gallery_decoder_probe(normalized_path, &validation, &header) ||
        !gallery_source_available(source))
        return false;

    normalized_length = strlen(normalized_path);
    if (normalized_length + 1U > lv_path_size) return false;
    rt_memcpy(lv_path, normalized_path, normalized_length + 1U);
    *verified_header = header;
    if (source_out != RT_NULL) *source_out = source;
    return true;
}

bool ft_gallery_can_open_file(const char *native_path)
{
    struct stat status;
    ft_gallery_source_t source;
    return gallery_managed_file_source(native_path, &source) &&
           gallery_image_format(native_path) != FT_GALLERY_IMAGE_NONE &&
           gallery_source_available(source) &&
           stat(native_path, &status) == 0 && S_ISREG(status.st_mode) &&
           status.st_size > 0;
}

bool ft_gallery_request_open_file(const char *native_path)
{
    if (!ft_gallery_can_open_file(native_path)) return false;
    rt_strncpy(s_pending_file, native_path, sizeof(s_pending_file) - 1U);
    s_pending_file[sizeof(s_pending_file) - 1U] = '\0';
    return true;
}

static bool gallery_read_rgb565(const lv_draw_buf_t *source,
                                uint32_t x, uint32_t y,
                                uint16_t *rgb565)
{
    const uint8_t *pixel;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha = 255U;
    uint16_t native;
    uint8_t pixel_size;

    if (source == RT_NULL || source->data == RT_NULL || rgb565 == RT_NULL ||
        x >= source->header.w || y >= source->header.h)
        return false;
    pixel_size = lv_color_format_get_size(source->header.cf);
    if (pixel_size == 0U ||
        (uint64_t)y * source->header.stride +
        (uint64_t)(x + 1U) * pixel_size > source->data_size)
        return false;
    pixel = source->data + (size_t)y * source->header.stride +
            (size_t)x * pixel_size;

    if (source->header.cf == LV_COLOR_FORMAT_RGB565)
    {
        /* Do not assume the decoder's row is naturally aligned. */
        rt_memcpy(&native, pixel, sizeof(native));
        *rgb565 = native;
        return true;
    }
    if (source->header.cf != LV_COLOR_FORMAT_RGB888 &&
        source->header.cf != LV_COLOR_FORMAT_XRGB8888 &&
        source->header.cf != LV_COLOR_FORMAT_ARGB8888)
        return false;

    /* LVGL stores the 24/32-bit color channels as B, G, R, [A]. */
    blue = pixel[0];
    green = pixel[1];
    red = pixel[2];
    if (source->header.cf == LV_COLOR_FORMAT_ARGB8888)
        alpha = pixel[3];
    if (alpha != 255U)
    {
        red = (uint8_t)(((uint16_t)red * alpha + 127U) / 255U);
        green = (uint8_t)(((uint16_t)green * alpha + 127U) / 255U);
        blue = (uint8_t)(((uint16_t)blue * alpha + 127U) / 255U);
    }
    *rgb565 = (uint16_t)(((uint16_t)(red & 0xF8U) << 8U) |
                        ((uint16_t)(green & 0xFCU) << 3U) |
                        ((uint16_t)blue >> 3U));
    return true;
}

static bool gallery_copy_decoded_area(
    const lv_draw_buf_t *decoded, const lv_area_t *decoded_area,
    uint32_t source_width, uint32_t source_height,
    lv_draw_buf_t *destination, uint64_t *written_pixels)
{
    uint32_t local_x;
    uint32_t local_y;
    uint32_t decoded_width;
    uint32_t decoded_height;

    if (decoded == RT_NULL || decoded_area == RT_NULL ||
        destination == RT_NULL || destination->data == RT_NULL ||
        written_pixels == RT_NULL || decoded_area->x1 < 0 ||
        decoded_area->y1 < 0 || decoded_area->x2 < decoded_area->x1 ||
        decoded_area->y2 < decoded_area->y1 ||
        (uint32_t)decoded_area->x2 >= source_width ||
        (uint32_t)decoded_area->y2 >= source_height)
        return false;
    decoded_width = (uint32_t)lv_area_get_width(decoded_area);
    decoded_height = (uint32_t)lv_area_get_height(decoded_area);
    if (!gallery_decoded_buffer_valid(decoded, decoded_width, decoded_height) ||
        destination->header.cf != LV_COLOR_FORMAT_RGB565)
        return false;

    for (local_y = 0U; local_y < decoded_height; local_y++)
    {
        uint32_t source_y = (uint32_t)decoded_area->y1 + local_y;
        uint32_t destination_y = (uint32_t)(((uint64_t)source_y *
                                             destination->header.h) /
                                            source_height);
        uint16_t *destination_row = (uint16_t *)(destination->data +
            (size_t)destination_y * destination->header.stride);

        for (local_x = 0U; local_x < decoded_width; local_x++)
        {
            uint32_t source_x = (uint32_t)decoded_area->x1 + local_x;
            uint32_t destination_x = (uint32_t)(((uint64_t)source_x *
                                                 destination->header.w) /
                                                source_width);
            uint16_t color;
            if (!gallery_read_rgb565(decoded, local_x, local_y, &color))
                return false;
            destination_row[destination_x] = color;
            (*written_pixels)++;
        }
    }
    return true;
}

void ft_gallery_release_rendered_image(ft_gallery_rendered_image_t *rendered)
{
    if (rendered == RT_NULL) return;
    if (rendered->draw_buf != RT_NULL)
        lv_draw_buf_destroy(rendered->draw_buf);
    rt_memset(rendered, 0, sizeof(*rendered));
}

bool ft_gallery_render_image_path(const char *native_path,
                                  uint32_t maximum_width,
                                  uint32_t maximum_height,
                                  ft_gallery_rendered_image_t *rendered)
{
    lv_image_decoder_dsc_t decoder;
    lv_image_decoder_args_t args;
    lv_image_header_t source_header;
    lv_area_t requested_area;
    lv_area_t decoded_area;
    ft_gallery_rendered_image_t result;
    ft_gallery_source_t source;
    ft_gallery_image_format_t format;
    ft_gallery_image_validation_t validation;
    uint64_t written_pixels = 0U;
    uint64_t byte_limit;
    uint64_t pixel_limit;
    uint32_t output_width;
    uint32_t output_height;
    uint32_t width_scale;
    uint32_t height_scale;
    uint32_t scale;
    uint32_t x;
    uint32_t y;
    char decoder_path[FT_STORAGE_PATH_MAX + 3U];
    bool decoder_open = false;
    bool decoded_any = false;
    bool complete = false;

    if (rendered == RT_NULL || maximum_width == 0U || maximum_height == 0U)
        return false;
    rt_memset(&result, 0, sizeof(result));
    rt_memset(&source_header, 0, sizeof(source_header));
    rt_memset(&validation, 0, sizeof(validation));
    if (!gallery_managed_file_source(native_path, &source) ||
        !gallery_source_available(source))
        return false;
    format = gallery_image_format(native_path);
    byte_limit = format == FT_GALLERY_IMAGE_PNG ?
                 FT_GALLERY_PNG_MAX_BYTES : FT_GALLERY_LARGE_MAX_BYTES;
    pixel_limit = format == FT_GALLERY_IMAGE_PNG ?
                  FT_GALLERY_PNG_MAX_PIXELS : FT_GALLERY_LARGE_MAX_PIXELS;
    /* Header validation is sufficient here.  The decoder is opened below and
     * its dimensions are checked again, so probing it first only decoded the
     * same image twice and made every thumbnail/viewer open unnecessarily slow. */
    if (format == FT_GALLERY_IMAGE_NONE ||
        !gallery_validate_image_file(native_path, source, format, byte_limit,
                                     pixel_limit, &validation) ||
        !gallery_make_decoder_path(native_path, source, decoder_path,
                                   sizeof(decoder_path)))
        return false;
    source_header.w = validation.width;
    source_header.h = validation.height;

    width_scale = (uint32_t)(((uint64_t)maximum_width * 65536ULL) /
                             source_header.w);
    height_scale = (uint32_t)(((uint64_t)maximum_height * 65536ULL) /
                              source_header.h);
    scale = width_scale < height_scale ? width_scale : height_scale;
    if (scale > 65536U) scale = 65536U;
    if (scale == 0U) scale = 1U;
    output_width = (uint32_t)(((uint64_t)source_header.w * scale) / 65536ULL);
    output_height = (uint32_t)(((uint64_t)source_header.h * scale) / 65536ULL);
    if (output_width == 0U) output_width = 1U;
    if (output_height == 0U) output_height = 1U;

    result.draw_buf = lv_draw_buf_create(output_width, output_height,
                                         LV_COLOR_FORMAT_RGB565,
                                         LV_STRIDE_AUTO);
    if (result.draw_buf == RT_NULL) goto finish;
    lv_draw_buf_clear(result.draw_buf, RT_NULL);

    rt_memset(&decoder, 0, sizeof(decoder));
    rt_memset(&args, 0, sizeof(args));
    args.no_cache = true;
    if (lv_image_decoder_open(&decoder, decoder_path, &args) != LV_RESULT_OK)
        goto finish;
    decoder_open = true;
    if (decoder.header.w != source_header.w ||
        decoder.header.h != source_header.h)
        goto finish;

    requested_area.x1 = 0;
    requested_area.y1 = 0;
    requested_area.x2 = (int32_t)source_header.w - 1;
    requested_area.y2 = (int32_t)source_header.h - 1;
    if (decoder.decoded != RT_NULL)
    {
        decoded_area = requested_area;
        decoded_any = gallery_copy_decoded_area(
            decoder.decoded, &decoded_area, source_header.w, source_header.h,
            result.draw_buf, &written_pixels);
    }
    else
    {
        decoded_area.x1 = LV_COORD_MIN;
        decoded_area.y1 = LV_COORD_MIN;
        decoded_area.x2 = LV_COORD_MIN;
        decoded_area.y2 = LV_COORD_MIN;
        while (lv_image_decoder_get_area(&decoder, &requested_area,
                                         &decoded_area) == LV_RESULT_OK)
        {
            if (!gallery_copy_decoded_area(
                    decoder.decoded, &decoded_area, source_header.w,
                    source_header.h, result.draw_buf, &written_pixels))
                goto finish;
            decoded_any = true;
        }
    }
    if (!decoded_any || written_pixels <
        (uint64_t)source_header.w * source_header.h)
        goto finish;

    result.source_width = source_header.w;
    result.source_height = source_header.h;
    result.checksum = 2166136261UL;
    for (y = 0U; y < result.draw_buf->header.h; y++)
    {
        const uint16_t *row = (const uint16_t *)(result.draw_buf->data +
            (size_t)y * result.draw_buf->header.stride);
        for (x = 0U; x < result.draw_buf->header.w; x++)
        {
            uint16_t color = row[x];
            if (color != 0U) result.non_black_pixels++;
            result.checksum ^= color;
            result.checksum *= 16777619UL;
        }
    }
    lv_draw_buf_flush_cache(result.draw_buf, RT_NULL);
    complete = true;

finish:
    if (decoder_open) lv_image_decoder_close(&decoder);
    if (!complete)
    {
        ft_gallery_release_rendered_image(&result);
        return false;
    }
    *rendered = result;
    return true;
}

static bool gallery_entry_decodable(const ft_storage_entry_t *entry)
{
    ft_gallery_image_format_t format;
    ft_gallery_image_validation_t validation;
    uint64_t byte_limit;
    uint64_t pixel_limit;
    char file_path[FT_STORAGE_PATH_MAX];

    if (entry == RT_NULL || entry->type != FT_STORAGE_ENTRY_FILE) return false;
    format = gallery_image_format(entry->name);
    if (format == FT_GALLERY_IMAGE_NONE) return false;
    byte_limit = format == FT_GALLERY_IMAGE_PNG ?
                 FT_GALLERY_PNG_MAX_BYTES : FT_GALLERY_LARGE_MAX_BYTES;
    pixel_limit = format == FT_GALLERY_IMAGE_PNG ?
                  FT_GALLERY_PNG_MAX_PIXELS : FT_GALLERY_LARGE_MAX_PIXELS;
    if (entry->size_bytes > byte_limit ||
        ft_storage_join_path(s_current_path, entry->name, file_path,
                             sizeof(file_path)) != RT_EOK)
        return false;
    rt_memset(&validation, 0, sizeof(validation));
    if (!gallery_validate_image_file(file_path, s_source, format, byte_limit,
                                     pixel_limit, &validation))
        return false;
    return validation.file_size == entry->size_bytes;
}

static const char *gallery_error_text(ft_gallery_error_t error)
{
    switch (error)
    {
    case FT_GALLERY_ERROR_MEDIA:
        return ft_preferences_text("介质已移除或正被 USB 使用。",
                                   "The medium was removed or is in use by USB.");
    case FT_GALLERY_ERROR_SIZE:
        return ft_preferences_text("文件过大，未加载以保护系统内存。",
                                   "The file is too large to load safely.");
    case FT_GALLERY_ERROR_DECODE:
        return ft_preferences_text("无法解码图片。文件可能损坏或解码器不可用。",
                                   "Unable to decode the image. It may be damaged or its decoder is unavailable.");
    case FT_GALLERY_ERROR_RESOLUTION:
        return ft_preferences_text("图片分辨率超过安全限制。",
                                   "The image resolution exceeds the safe limit.");
    case FT_GALLERY_ERROR_PATH:
        return ft_preferences_text("图片路径无效或过长。",
                                   "The image path is invalid or too long.");
    default:
        return "";
    }
}

static void gallery_update_viewer_text(void)
{
    char size_text[28];
    char info[FT_STORAGE_NAME_MAX + 72U];
    const char *display_name = RT_NULL;

    if (!gallery_object_valid(s_image_info) ||
        !gallery_object_valid(s_image_error))
        return;
    gallery_format_bytes(s_image_size, size_text, sizeof(size_text));
    if (s_current_entry < s_entry_count)
        display_name = s_entries[s_current_entry].name;
    else if (s_external_viewer && s_current_file[0] != '\0')
    {
        display_name = strrchr(s_current_file, '/');
        display_name = display_name != RT_NULL ? display_name + 1U : s_current_file;
    }
    if (display_name != RT_NULL)
    {
        if (s_image_header.w > 0U && s_image_header.h > 0U)
            rt_snprintf(info, sizeof(info), "%s\n%lu x %lu  ·  %s",
                        display_name,
                        (unsigned long)s_image_header.w,
                        (unsigned long)s_image_header.h, size_text);
        else
            rt_snprintf(info, sizeof(info), "%s\n%s",
                        display_name, size_text);
        lv_label_set_text(s_image_info, info);
    }
    else
        lv_label_set_text(s_image_info, "");
    lv_label_set_text(s_image_error, gallery_error_text(s_image_error_code));
    if (s_image_error_code == FT_GALLERY_ERROR_NONE)
        lv_obj_add_flag(s_image_error, LV_OBJ_FLAG_HIDDEN);
    else
            lv_obj_remove_flag(s_image_error, LV_OBJ_FLAG_HIDDEN);
}

static void gallery_set_preview_loading(bool loading)
{
    s_preview_loading = loading;
    if (!gallery_object_valid(s_image_loading)) return;
    if (loading)
        lv_obj_remove_flag(s_image_loading, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_image_loading, LV_OBJ_FLAG_HIDDEN);
}

static void gallery_stop_preview_loader(void)
{
    if (s_preview_timer != RT_NULL)
    {
        lv_timer_delete(s_preview_timer);
        s_preview_timer = RT_NULL;
    }
    gallery_set_preview_loading(false);
}

static void gallery_clear_image(void)
{
    gallery_stop_preview_loader();
    if (gallery_object_valid(s_image))
        lv_image_set_src(s_image, RT_NULL);
    if (s_preview_cache.draw_buf != RT_NULL)
    {
        lv_draw_wait_for_finish();
        ft_gallery_release_rendered_image(&s_preview_cache);
    }
    s_image_valid = false;
    s_image_decoder_verified = false;
}

static void gallery_fit_image(void)
{
    int32_t available_width;
    int32_t available_height;
    uint32_t scale = LV_SCALE_NONE;
    uint32_t width_scale;
    uint32_t height_scale;

    if (!gallery_object_valid(s_image) || !gallery_object_valid(s_image_host) ||
        s_preview_cache.draw_buf == RT_NULL ||
        s_preview_cache.draw_buf->header.w == 0U ||
        s_preview_cache.draw_buf->header.h == 0U)
        return;
    lv_obj_update_layout(s_page);
    available_width = lv_obj_get_content_width(s_image_host);
    available_height = lv_obj_get_content_height(s_image_host);
    if (available_width <= 0 || available_height <= 0) return;
    width_scale = (uint32_t)(((uint64_t)(uint32_t)available_width *
                              LV_SCALE_NONE) /
                             s_preview_cache.draw_buf->header.w);
    height_scale = (uint32_t)(((uint64_t)(uint32_t)available_height *
                               LV_SCALE_NONE) /
                              s_preview_cache.draw_buf->header.h);
    if (width_scale < scale) scale = width_scale;
    if (height_scale < scale) scale = height_scale;
    if (scale == 0U) scale = 1U;
    lv_image_set_scale(s_image, scale);
    lv_obj_center(s_image);
}

static void gallery_update_viewer_controls(void)
{
    bool image_ready = s_image_valid && s_image_decoder_verified;
    bool can_navigate = !s_preview_loading && !s_external_viewer;

    if (gallery_object_valid(s_wallpaper_button))
    {
        if (image_ready && !s_preview_loading)
            lv_obj_remove_state(s_wallpaper_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_wallpaper_button, LV_STATE_DISABLED);
    }
    if (gallery_object_valid(s_delete_button))
    {
        if (s_current_file[0] != '\0' && !s_preview_loading)
            lv_obj_remove_state(s_delete_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_delete_button, LV_STATE_DISABLED);
    }
    if (gallery_object_valid(s_previous_button))
    {
        if (can_navigate)
            lv_obj_remove_state(s_previous_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_previous_button, LV_STATE_DISABLED);
    }
    if (gallery_object_valid(s_next_button))
    {
        if (can_navigate)
            lv_obj_remove_state(s_next_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_next_button, LV_STATE_DISABLED);
    }
}

static void gallery_show_viewer_shell(bool loading)
{
    s_viewer_active = true;
    if (gallery_object_valid(s_browser))
        lv_obj_add_flag(s_browser, LV_OBJ_FLAG_HIDDEN);
    if (gallery_object_valid(s_viewer))
        lv_obj_remove_flag(s_viewer, LV_OBJ_FLAG_HIDDEN);
    gallery_set_preview_loading(loading);
    gallery_update_viewer_controls();
    gallery_update_viewer_text();
}

static void gallery_load_current_preview(void)
{
    struct stat status;
    ft_gallery_source_t source;
    ft_gallery_image_format_t format;
    uint64_t byte_limit;

    if (!s_viewer_active || s_current_file[0] == '\0')
    {
        s_image_error_code = FT_GALLERY_ERROR_PATH;
        return;
    }
    if (!gallery_managed_file_source(s_current_file, &source) ||
        source != s_source)
    {
        s_image_error_code = FT_GALLERY_ERROR_PATH;
        return;
    }
    if (!gallery_source_available(source) ||
        stat(s_current_file, &status) != 0 || !S_ISREG(status.st_mode))
    {
        s_image_error_code = FT_GALLERY_ERROR_MEDIA;
        return;
    }
    s_image_size = (uint64_t)status.st_size;
    format = gallery_image_format(s_current_file);
    byte_limit = format == FT_GALLERY_IMAGE_PNG ?
                 FT_GALLERY_PNG_MAX_BYTES : FT_GALLERY_LARGE_MAX_BYTES;
    if (format == FT_GALLERY_IMAGE_NONE)
    {
        s_image_error_code = FT_GALLERY_ERROR_DECODE;
        return;
    }
    if (s_image_size > byte_limit)
    {
        s_image_error_code = FT_GALLERY_ERROR_SIZE;
        return;
    }
    if (!gallery_make_decoder_path(s_current_file, source, s_decoder_path,
                                   sizeof(s_decoder_path)))
    {
        s_image_error_code = FT_GALLERY_ERROR_PATH;
        return;
    }
    if (!ft_gallery_render_image_path(s_current_file, 480U, 800U,
                                      &s_preview_cache))
    {
        s_decoder_path[0] = '\0';
        s_image_error_code = FT_GALLERY_ERROR_DECODE;
        return;
    }

    s_image_header.w = s_preview_cache.source_width;
    s_image_header.h = s_preview_cache.source_height;
    s_image_header.cf = LV_COLOR_FORMAT_RGB565;
    s_image_decoder_verified = true;
    if (gallery_object_valid(s_image))
        lv_image_set_src(s_image, s_preview_cache.draw_buf);
    s_image_valid = true;
}

static void gallery_preview_timer_cb(lv_timer_t *timer)
{
    if (timer != s_preview_timer) return;
    lv_timer_delete(timer);
    s_preview_timer = RT_NULL;
    if (!s_viewer_active)
    {
        gallery_set_preview_loading(false);
        return;
    }

    gallery_load_current_preview();
    gallery_set_preview_loading(false);
    gallery_update_viewer_controls();
    gallery_update_viewer_text();
    if (s_image_valid && s_image_decoder_verified) gallery_fit_image();
}

static void gallery_schedule_preview_load(void)
{
    gallery_set_preview_loading(true);
    s_preview_timer = lv_timer_create(gallery_preview_timer_cb,
                                      FT_GALLERY_PREVIEW_DELAY_MS, RT_NULL);
    if (s_preview_timer == RT_NULL)
    {
        gallery_set_preview_loading(false);
        s_image_error_code = FT_GALLERY_ERROR_DECODE;
        gallery_update_viewer_controls();
        gallery_update_viewer_text();
    }
}

static void gallery_open_image(size_t index)
{
    char file_path[FT_STORAGE_PATH_MAX];

    if (index >= s_entry_count ||
        s_entries[index].kind != FT_GALLERY_ENTRY_IMAGE)
        return;
    gallery_stop_thumbnail_loader();
    gallery_clear_image();
    gallery_close_delete_confirmation();
    s_external_viewer = false;
    s_current_entry = index;
    s_image_size = s_entries[index].size_bytes;
    s_image_error_code = FT_GALLERY_ERROR_NONE;
    rt_memset(&s_image_header, 0, sizeof(s_image_header));
    s_current_file[0] = '\0';
    s_decoder_path[0] = '\0';
    if (ft_storage_join_path(s_current_path, s_entries[index].name,
                             file_path, sizeof(file_path)) != RT_EOK ||
        !gallery_path_has_root(file_path, gallery_collection_path(s_source)))
    {
        s_image_error_code = FT_GALLERY_ERROR_PATH;
        gallery_show_viewer_shell(false);
        return;
    }
    rt_strncpy(s_current_file, file_path, sizeof(s_current_file) - 1U);
    s_current_file[sizeof(s_current_file) - 1U] = '\0';
    gallery_show_viewer_shell(true);
    gallery_schedule_preview_load();
}

static void gallery_open_external_image(const char *file_path)
{
    ft_gallery_source_t source;

    if (!gallery_managed_file_source(file_path, &source)) return;
    gallery_stop_thumbnail_loader();
    gallery_clear_image();
    gallery_close_delete_confirmation();
    s_source = source;
    rt_strncpy(s_current_path, gallery_collection_path(source),
               sizeof(s_current_path) - 1U);
    s_current_path[sizeof(s_current_path) - 1U] = '\0';
    s_current_entry = FT_GALLERY_INVALID_INDEX;
    s_external_viewer = true;
    s_image_size = 0U;
    s_image_error_code = FT_GALLERY_ERROR_NONE;
    rt_memset(&s_image_header, 0, sizeof(s_image_header));
    rt_strncpy(s_current_file, file_path, sizeof(s_current_file) - 1U);
    s_current_file[sizeof(s_current_file) - 1U] = '\0';
    s_decoder_path[0] = '\0';
    gallery_show_viewer_shell(true);
    gallery_schedule_preview_load();
}

static void gallery_open_entry(size_t index)
{
    if (index >= s_entry_count) return;
    gallery_open_image(index);
}

static void gallery_show_browser(void)
{
    gallery_close_delete_confirmation();
    gallery_clear_image();
    s_viewer_active = false;
    s_external_viewer = false;
    s_current_entry = FT_GALLERY_INVALID_INDEX;
    s_current_file[0] = '\0';
    s_decoder_path[0] = '\0';
    s_image_size = 0U;
    s_image_error_code = FT_GALLERY_ERROR_NONE;
    rt_memset(&s_image_header, 0, sizeof(s_image_header));
    if (gallery_object_valid(s_viewer))
        lv_obj_add_flag(s_viewer, LV_OBJ_FLAG_HIDDEN);
    if (gallery_object_valid(s_browser))
        lv_obj_remove_flag(s_browser, LV_OBJ_FLAG_HIDDEN);
    if (s_thumbnail_next < s_entry_count)
    {
        gallery_stop_thumbnail_loader();
        s_thumbnail_timer = lv_timer_create(gallery_thumbnail_timer_cb,
                                            FT_GALLERY_THUMB_PERIOD_MS,
                                            RT_NULL);
    }
}

static void gallery_source_clicked_cb(lv_event_t *event)
{
    size_t source = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (source >= FT_GALLERY_SOURCE_COUNT) return;
    gallery_show_browser();
    s_source = (ft_gallery_source_t)source;
    rt_strncpy(s_current_path, gallery_collection_path(s_source),
               sizeof(s_current_path) - 1U);
    s_current_path[sizeof(s_current_path) - 1U] = '\0';
    gallery_refresh(false);
}

static void gallery_refresh_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    gallery_refresh(true);
}

static void gallery_close_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    gallery_show_browser();
}

static size_t gallery_adjacent_image(bool next)
{
    size_t candidate;
    size_t step;
    if (s_image_count == 0U || s_current_entry >= s_entry_count)
        return FT_GALLERY_INVALID_INDEX;
    candidate = s_current_entry;
    for (step = 0U; step < s_entry_count; step++)
    {
        candidate = next ? (candidate + 1U) % s_entry_count :
                           (candidate == 0U ? s_entry_count - 1U : candidate - 1U);
        if (s_entries[candidate].kind == FT_GALLERY_ENTRY_IMAGE)
            return candidate;
    }
    return FT_GALLERY_INVALID_INDEX;
}

static void gallery_previous_clicked_cb(lv_event_t *event)
{
    size_t index;
    LV_UNUSED(event);
    index = gallery_adjacent_image(false);
    if (index != FT_GALLERY_INVALID_INDEX) gallery_open_image(index);
}

static void gallery_next_clicked_cb(lv_event_t *event)
{
    size_t index;
    LV_UNUSED(event);
    index = gallery_adjacent_image(true);
    if (index != FT_GALLERY_INVALID_INDEX) gallery_open_image(index);
}

static void gallery_wallpaper_clicked_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if (!s_image_valid || !s_image_decoder_verified ||
        s_current_file[0] == '\0' ||
        !gallery_source_available(s_source))
        return;
    ft_preferences_set_wallpaper_file(s_current_file);
    feathertalk_ui_alert(ft_preferences_text("壁纸", "Wallpaper"),
                        ft_preferences_text("已将此图片设为壁纸。",
                                            "This image is now the wallpaper."));
}

static void gallery_close_delete_confirmation(void)
{
    if (gallery_object_valid(s_delete_box)) lv_msgbox_close(s_delete_box);
    s_delete_box = RT_NULL;
    s_delete_cancel = RT_NULL;
    s_delete_confirm = RT_NULL;
}

static void gallery_delete_cancel_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    gallery_close_delete_confirmation();
}

static void gallery_delete_confirm_cb(lv_event_t *event)
{
    char deleted_path[FT_STORAGE_PATH_MAX];
    const ft_ui_preferences_t *preferences = ft_preferences_get();
    bool was_wallpaper;
    int result;

    LV_UNUSED(event);
    rt_strncpy(deleted_path, s_current_file, sizeof(deleted_path) - 1U);
    deleted_path[sizeof(deleted_path) - 1U] = '\0';
    was_wallpaper = preferences != RT_NULL &&
                    preferences->background == FT_BACKGROUND_CUSTOM &&
                    strcmp(preferences->wallpaper_path, deleted_path) == 0;
    result = ft_storage_delete_path(deleted_path);
    gallery_close_delete_confirmation();
    if (result != RT_EOK)
    {
        feathertalk_ui_alert(ft_preferences_text("无法删除", "Unable to delete"),
                            ft_preferences_text("图片可能已被移除、介质为只读，或正在被其他功能使用。",
                                                "The image may be gone, read-only, or in use."));
        return;
    }
    if (was_wallpaper) ft_preferences_set_background(FT_BACKGROUND_BLACK);
    gallery_show_browser();
    gallery_refresh(false);
    feathertalk_ui_alert(ft_preferences_text("已删除", "Deleted"),
                        ft_preferences_text("图片已从存储设备删除。",
                                            "The image was removed from storage."));
}

static void gallery_delete_clicked_cb(lv_event_t *event)
{
    lv_obj_t *title;
    lv_obj_t *text;
    const char *name;
    char message[FT_STORAGE_NAME_MAX + 80U];

    LV_UNUSED(event);
    if (s_current_file[0] == '\0') return;
    gallery_close_delete_confirmation();
    name = strrchr(s_current_file, '/');
    name = name != RT_NULL ? name + 1U : s_current_file;
    rt_snprintf(message, sizeof(message),
                ft_preferences_text("删除图片“%s”？\n此操作不可撤销。",
                                    "Delete image \"%s\"?\nThis cannot be undone."),
                name);
    s_delete_box = lv_msgbox_create(RT_NULL);
    lv_obj_set_width(s_delete_box, lv_pct(88));
    title = lv_msgbox_add_title(s_delete_box,
                                ft_preferences_text("删除图片", "Delete image"));
    text = lv_msgbox_add_text(s_delete_box, message);
    lv_obj_set_style_text_font(title, ft_layout_font(18), LV_PART_MAIN);
    lv_obj_set_style_text_font(text, ft_layout_font(14), LV_PART_MAIN);
    s_delete_cancel = lv_msgbox_add_footer_button(
        s_delete_box, ft_preferences_text("取消", "Cancel"));
    s_delete_confirm = lv_msgbox_add_footer_button(
        s_delete_box, ft_preferences_text("删除", "Delete"));
    if (lv_obj_get_child_count(s_delete_cancel) > 0U)
        lv_obj_set_style_text_font(lv_obj_get_child(s_delete_cancel, 0U),
                                   ft_layout_font(14), LV_PART_MAIN);
    if (lv_obj_get_child_count(s_delete_confirm) > 0U)
        lv_obj_set_style_text_font(lv_obj_get_child(s_delete_confirm, 0U),
                                   ft_layout_font(14), LV_PART_MAIN);
    lv_obj_add_event_cb(s_delete_cancel, gallery_delete_cancel_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(s_delete_confirm, gallery_delete_confirm_cb,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_set_style_bg_color(s_delete_confirm, lv_color_hex(0xC42B1C),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_delete_confirm, LV_OPA_COVER, LV_PART_MAIN);
}

static void gallery_monitor_cb(lv_timer_t *timer)
{
    uint8_t signature[FT_GALLERY_SOURCE_COUNT];
    size_t i;
    bool changed = false;
    LV_UNUSED(timer);

    for (i = 0U; i < FT_GALLERY_SOURCE_COUNT; i++)
    {
        signature[i] = gallery_source_state((ft_gallery_source_t)i, RT_NULL);
        if (signature[i] != s_source_signature[i]) changed = true;
    }
    if (!changed) return;
    rt_memcpy(s_source_signature, signature, sizeof(signature));
    for (i = 0U; i < FT_GALLERY_SOURCE_COUNT; i++)
        if (signature[i] == 5U)
            (void)gallery_ensure_collection((ft_gallery_source_t)i);
    if (s_viewer_active && !gallery_source_available(s_source))
        gallery_show_browser();
    gallery_refresh(false);
}

static void gallery_page_deleted_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    gallery_close_delete_confirmation();
    gallery_stop_thumbnail_loader();
    gallery_stop_preview_loader();
    if (s_monitor_timer != RT_NULL)
    {
        lv_timer_delete(s_monitor_timer);
        s_monitor_timer = RT_NULL;
    }
    s_page = RT_NULL;
    s_title_label = RT_NULL;
    s_hint_label = RT_NULL;
    s_browser = RT_NULL;
    rt_memset(s_source_buttons, 0, sizeof(s_source_buttons));
    rt_memset(s_source_labels, 0, sizeof(s_source_labels));
    s_path_label = RT_NULL;
    s_status_label = RT_NULL;
    s_refresh_button = RT_NULL;
    s_refresh_label = RT_NULL;
    s_list = RT_NULL;
    s_viewer = RT_NULL;
    s_image_host = RT_NULL;
    s_image = RT_NULL;
    s_image_loading = RT_NULL;
    s_image_info = RT_NULL;
    s_image_error = RT_NULL;
    s_previous_button = RT_NULL;
    s_previous_label = RT_NULL;
    s_next_button = RT_NULL;
    s_next_label = RT_NULL;
    s_close_button = RT_NULL;
    s_close_label = RT_NULL;
    s_wallpaper_button = RT_NULL;
    s_wallpaper_label = RT_NULL;
    s_delete_button = RT_NULL;
    s_delete_label = RT_NULL;
    gallery_release_entry_thumbnails(false);
    rt_memset(s_entries, 0, sizeof(s_entries));
    s_entry_count = 0U;
    s_image_count = 0U;
    s_current_entry = FT_GALLERY_INVALID_INDEX;
    s_viewer_active = false;
    s_external_viewer = false;
    s_image_valid = false;
    s_image_decoder_verified = false;
    s_preview_loading = false;
}

static void gallery_apply_static_language(void)
{
    lv_label_set_text(s_title_label, ft_preferences_text("相册", "Gallery"));
    lv_label_set_text(s_hint_label,
                      ft_preferences_text("Flash 与 SD 卡", "Flash and SD card"));
    lv_label_set_text(s_refresh_label, ft_preferences_text("刷新", "Refresh"));
    lv_label_set_text(s_previous_label, ft_preferences_text("上一张", "Previous"));
    lv_label_set_text(s_next_label, ft_preferences_text("下一张", "Next"));
    lv_label_set_text(s_close_label, ft_preferences_text("关闭", "Close"));
    lv_label_set_text(s_wallpaper_label,
                      ft_preferences_text("设为壁纸", "Set wallpaper"));
    lv_label_set_text(s_delete_label, ft_preferences_text("删除", "Delete"));
    if (gallery_object_valid(s_image_loading))
        lv_label_set_text(s_image_loading,
                          ft_preferences_text("正在加载...", "Loading..."));
}

lv_obj_t *ft_gallery_create_page(lv_obj_t *parent)
{
    const ft_ui_layout_t *layout = ft_layout_get();
    lv_obj_t *header;
    lv_obj_t *source_row;
    lv_obj_t *toolbar;
    lv_obj_t *controls;
    size_t i;

    if (parent == RT_NULL) return RT_NULL;
    s_page = lv_obj_create(parent);
    ft_ui_style_page(s_page);
    ft_ui_register_page_background(s_page);
    lv_obj_set_style_pad_all(s_page, layout->page_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_page, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_page, gallery_page_deleted_cb, LV_EVENT_DELETE, RT_NULL);

    header = lv_obj_create(s_page);
    gallery_style_container(header);
    lv_obj_set_size(header, lv_pct(100), ft_layout_px(38));
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_title_label = lv_label_create(header);
    lv_obj_set_style_text_font(s_title_label, ft_layout_font(22), LV_PART_MAIN);
    ft_ui_register_accent(s_title_label, FT_ACCENT_TEXT);
    s_hint_label = lv_label_create(header);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_hint_label, ft_layout_font(12), LV_PART_MAIN);

    s_browser = lv_obj_create(s_page);
    gallery_style_container(s_browser);
    lv_obj_set_size(s_browser, lv_pct(100), 0);
    lv_obj_set_flex_grow(s_browser, 1);
    lv_obj_set_style_pad_row(s_browser, ft_layout_px(7), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_browser, LV_FLEX_FLOW_COLUMN);

    source_row = lv_obj_create(s_browser);
    gallery_style_container(source_row);
    lv_obj_set_size(source_row, lv_pct(100), ft_layout_px(64));
    lv_obj_set_style_pad_column(source_row, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(source_row, LV_FLEX_FLOW_ROW);
    for (i = 0U; i < FT_GALLERY_SOURCE_COUNT; i++)
    {
        s_source_buttons[i] = gallery_create_button(source_row, "",
            gallery_source_clicked_cb, (void *)(uintptr_t)i,
            &s_source_labels[i]);
        lv_obj_set_height(s_source_buttons[i], ft_layout_px(64));
        lv_obj_set_width(s_source_buttons[i], 0);
        lv_obj_set_flex_grow(s_source_buttons[i], 1);
        lv_label_set_long_mode(s_source_labels[i], LV_LABEL_LONG_WRAP);
    }

    s_path_label = lv_label_create(s_browser);
    lv_obj_set_width(s_path_label, lv_pct(100));
    lv_label_set_long_mode(s_path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_path_label, ft_layout_font(18), LV_PART_MAIN);
    ft_ui_register_accent(s_path_label, FT_ACCENT_TEXT);
    s_status_label = lv_label_create(s_browser);
    lv_obj_set_width(s_status_label, lv_pct(100));
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xA8A8A8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status_label, ft_layout_font(12), LV_PART_MAIN);

    toolbar = lv_obj_create(s_browser);
    gallery_style_container(toolbar);
    lv_obj_set_size(toolbar, lv_pct(100), layout->control_height);
    lv_obj_set_style_pad_column(toolbar, ft_layout_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    s_refresh_button = gallery_create_button(toolbar, "", gallery_refresh_clicked_cb,
                                             RT_NULL, &s_refresh_label);
    lv_obj_set_width(s_refresh_button, lv_pct(100));

    s_list = lv_obj_create(s_browser);
    gallery_style_container(s_list);
    lv_obj_set_size(s_list, lv_pct(100), 0);
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_pad_row(s_list, ft_layout_px(3), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    s_viewer = lv_obj_create(s_page);
    gallery_style_container(s_viewer);
    lv_obj_set_size(s_viewer, lv_pct(100), 0);
    lv_obj_set_flex_grow(s_viewer, 1);
    lv_obj_set_style_pad_row(s_viewer, ft_layout_px(7), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_viewer, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_viewer, LV_OBJ_FLAG_HIDDEN);

    s_image_host = lv_obj_create(s_viewer);
    lv_obj_set_size(s_image_host, lv_pct(100), 0);
    lv_obj_set_flex_grow(s_image_host, 1);
    lv_obj_set_style_bg_color(s_image_host, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_image_host, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_image_host, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_image_host, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_radius(s_image_host, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_image_host, ft_layout_px(4), LV_PART_MAIN);
    lv_obj_remove_flag(s_image_host, LV_OBJ_FLAG_SCROLLABLE);
    s_image = lv_image_create(s_image_host);
    lv_image_set_antialias(s_image, true);
    lv_obj_center(s_image);
    s_image_loading = lv_label_create(s_image_host);
    lv_label_set_text(s_image_loading,
                      ft_preferences_text("正在加载...", "Loading..."));
    lv_obj_set_style_text_color(s_image_loading, lv_color_hex(0xC8C8C8),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_image_loading, ft_layout_font(16), LV_PART_MAIN);
    lv_obj_center(s_image_loading);
    lv_obj_add_flag(s_image_loading, LV_OBJ_FLAG_HIDDEN);

    s_image_info = lv_label_create(s_viewer);
    lv_obj_set_width(s_image_info, lv_pct(100));
    lv_label_set_long_mode(s_image_info, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_image_info, ft_layout_font(14), LV_PART_MAIN);
    s_image_error = lv_label_create(s_viewer);
    lv_obj_set_width(s_image_error, lv_pct(100));
    lv_label_set_long_mode(s_image_error, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_image_error, lv_color_hex(0xFF9A85), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_image_error, ft_layout_font(12), LV_PART_MAIN);
    lv_obj_add_flag(s_image_error, LV_OBJ_FLAG_HIDDEN);

    controls = lv_obj_create(s_viewer);
    gallery_style_container(controls);
    lv_obj_set_width(controls, lv_pct(100));
    lv_obj_set_height(controls, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(controls, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_set_style_pad_row(controls, ft_layout_px(6), LV_PART_MAIN);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW_WRAP);
    s_previous_button = gallery_create_button(controls, "",
        gallery_previous_clicked_cb, RT_NULL, &s_previous_label);
    s_next_button = gallery_create_button(controls, "",
        gallery_next_clicked_cb, RT_NULL, &s_next_label);
    s_close_button = gallery_create_button(controls, "",
        gallery_close_clicked_cb, RT_NULL, &s_close_label);
    s_wallpaper_button = gallery_create_button(controls, "",
        gallery_wallpaper_clicked_cb, RT_NULL, &s_wallpaper_label);
    s_delete_button = gallery_create_button(controls, "",
        gallery_delete_clicked_cb, RT_NULL, &s_delete_label);
    lv_obj_set_style_bg_color(s_delete_button, lv_color_hex(0x6A1B16), LV_PART_MAIN);
    for (i = 0U; i < 5U; i++)
    {
        lv_obj_t *button = i == 0U ? s_previous_button :
                           i == 1U ? s_next_button :
                           i == 2U ? s_close_button :
                           i == 3U ? s_wallpaper_button : s_delete_button;
        lv_obj_set_width(button, i < 3U ? lv_pct(31) : lv_pct(48));
    }
    lv_obj_add_state(s_wallpaper_button, LV_STATE_DISABLED);
    lv_obj_add_state(s_delete_button, LV_STATE_DISABLED);

    s_source = FT_GALLERY_SOURCE_FLASH;
    rt_strncpy(s_current_path, gallery_collection_path(s_source),
               sizeof(s_current_path) - 1U);
    s_current_path[sizeof(s_current_path) - 1U] = '\0';
    s_current_file[0] = '\0';
    s_decoder_path[0] = '\0';
    s_current_entry = FT_GALLERY_INVALID_INDEX;
    s_viewer_active = false;
    s_external_viewer = false;
    s_image_valid = false;
    s_image_decoder_verified = false;
    s_preview_loading = false;
    rt_memset(s_source_signature, 0xFF, sizeof(s_source_signature));
    gallery_apply_static_language();
    return s_page;
}

void ft_gallery_page_enter(void)
{
    size_t i;
    char pending[FT_STORAGE_PATH_MAX];
    if (!gallery_object_valid(s_page)) return;
    for (i = 0U; i < FT_GALLERY_SOURCE_COUNT; i++)
    {
        s_source_signature[i] = gallery_source_state((ft_gallery_source_t)i,
                                                     RT_NULL);
        if (s_source_signature[i] == 5U)
            (void)gallery_ensure_collection((ft_gallery_source_t)i);
    }
    if (s_viewer_active && !gallery_source_available(s_source))
        gallery_show_browser();
    gallery_refresh(false);
    if (s_pending_file[0] != '\0')
    {
        rt_strncpy(pending, s_pending_file, sizeof(pending) - 1U);
        pending[sizeof(pending) - 1U] = '\0';
        s_pending_file[0] = '\0';
        gallery_open_external_image(pending);
    }
    if (s_monitor_timer == RT_NULL)
        s_monitor_timer = lv_timer_create(gallery_monitor_cb,
                                          FT_GALLERY_MONITOR_PERIOD_MS,
                                          RT_NULL);
}

bool ft_gallery_page_back(void)
{
    if (!gallery_object_valid(s_page)) return false;
    if (s_viewer_active)
    {
        gallery_show_browser();
        return true;
    }
    return false;
}

void ft_gallery_page_leave(void)
{
    gallery_close_delete_confirmation();
    gallery_stop_thumbnail_loader();
    gallery_stop_preview_loader();
    if (s_monitor_timer != RT_NULL)
    {
        lv_timer_delete(s_monitor_timer);
        s_monitor_timer = RT_NULL;
    }
    gallery_clear_image();
    gallery_release_entry_thumbnails(true);
}

void ft_gallery_apply_language(void)
{
    if (!gallery_object_valid(s_page)) return;
    gallery_apply_static_language();
    if (s_viewer_active)
    {
        gallery_update_source_buttons();
        gallery_update_viewer_text();
    }
    else
        gallery_refresh(false);
}

#ifdef FEATHERTALK_UI_TEST_MODE
lv_obj_t *ft_gallery_test_get_source_button(size_t index)
{
    return index < FT_GALLERY_SOURCE_COUNT ? s_source_buttons[index] : RT_NULL;
}

lv_obj_t *ft_gallery_test_get_refresh_button(void) { return s_refresh_button; }

lv_obj_t *ft_gallery_test_get_entry(size_t index)
{
    return index < s_entry_count ? s_entries[index].row : RT_NULL;
}

lv_obj_t *ft_gallery_test_get_first_image(void)
{
    size_t i;
    for (i = 0U; i < s_entry_count; i++)
        if (s_entries[i].kind == FT_GALLERY_ENTRY_IMAGE)
            return s_entries[i].row;
    return RT_NULL;
}

lv_obj_t *ft_gallery_test_get_previous_button(void) { return s_previous_button; }
lv_obj_t *ft_gallery_test_get_next_button(void) { return s_next_button; }
lv_obj_t *ft_gallery_test_get_close_button(void) { return s_close_button; }
lv_obj_t *ft_gallery_test_get_wallpaper_button(void) { return s_wallpaper_button; }
lv_obj_t *ft_gallery_test_get_delete_button(void) { return s_delete_button; }
lv_obj_t *ft_gallery_test_get_delete_cancel(void) { return s_delete_cancel; }
size_t ft_gallery_test_entry_count(void) { return s_entry_count; }
size_t ft_gallery_test_image_count(void) { return s_image_count; }
size_t ft_gallery_test_selected_source(void) { return (size_t)s_source; }
const char *ft_gallery_test_current_path(void) { return s_current_path; }
const char *ft_gallery_test_current_file(void) { return s_current_file; }
const char *ft_gallery_test_decoder_path(void) { return s_decoder_path; }

bool ft_gallery_test_browser_visible(void)
{
    return gallery_object_valid(s_browser) &&
           !lv_obj_has_flag(s_browser, LV_OBJ_FLAG_HIDDEN) && !s_viewer_active;
}

bool ft_gallery_test_viewer_visible(void)
{
    return gallery_object_valid(s_viewer) &&
           !lv_obj_has_flag(s_viewer, LV_OBJ_FLAG_HIDDEN) && s_viewer_active;
}

bool ft_gallery_test_preview_loading(void)
{
    return s_preview_loading && s_preview_timer != RT_NULL &&
           gallery_object_valid(s_image_loading) &&
           !lv_obj_has_flag(s_image_loading, LV_OBJ_FLAG_HIDDEN);
}

bool ft_gallery_test_entry_hit_target(size_t index)
{
    const ft_gallery_entry_t *entry;
    if (index >= s_entry_count) return false;
    entry = &s_entries[index];
    return gallery_object_valid(entry->row) &&
           lv_obj_has_flag(entry->row, LV_OBJ_FLAG_CLICKABLE) &&
           lv_obj_get_height(entry->row) >= ft_layout_px(72) &&
           gallery_object_valid(entry->thumbnail_image) &&
           !lv_obj_has_flag(entry->thumbnail_image, LV_OBJ_FLAG_CLICKABLE) &&
           gallery_object_valid(entry->thumbnail_placeholder) &&
           !lv_obj_has_flag(entry->thumbnail_placeholder, LV_OBJ_FLAG_CLICKABLE) &&
           gallery_object_valid(entry->detail_label) &&
           !lv_obj_has_flag(entry->detail_label, LV_OBJ_FLAG_CLICKABLE);
}

bool ft_gallery_test_current_image_valid(void)
{
    return ft_gallery_test_current_image_verified();
}

bool ft_gallery_test_current_image_verified(void)
{
    return s_image_decoder_verified && s_image_valid &&
           s_current_file[0] != '\0' && s_decoder_path[0] == 'P' &&
           s_decoder_path[1] == ':' && s_image_header.w > 0U &&
           s_image_header.h > 0U &&
           gallery_managed_file_source(s_current_file, RT_NULL) &&
           strcmp(s_decoder_path + 2U, s_current_file) == 0;
}

bool ft_gallery_test_current_image_cached(void)
{
    return ft_gallery_test_current_image_verified() &&
           s_preview_cache.draw_buf != RT_NULL &&
           s_preview_cache.draw_buf->data != RT_NULL &&
           s_preview_cache.draw_buf->header.cf == LV_COLOR_FORMAT_RGB565 &&
           s_preview_cache.draw_buf->header.w > 0U &&
           s_preview_cache.draw_buf->header.h > 0U &&
           s_preview_cache.non_black_pixels > 0U &&
           s_preview_cache.checksum != 0U;
}

uint32_t ft_gallery_test_current_image_non_black_pixels(void)
{
    return s_preview_cache.non_black_pixels;
}

uint32_t ft_gallery_test_current_image_checksum(void)
{
    return s_preview_cache.checksum;
}

bool ft_gallery_test_source_available(size_t index)
{
    return index < FT_GALLERY_SOURCE_COUNT &&
           gallery_source_available((ft_gallery_source_t)index);
}

bool ft_gallery_test_path_safe(void)
{
    if (!gallery_current_path_safe()) return false;
    if (s_current_file[0] != '\0' &&
        !gallery_managed_file_source(s_current_file, RT_NULL))
        return false;
    return s_decoder_path[0] == '\0' ||
           (s_decoder_path[0] == 'P' && s_decoder_path[1] == ':' &&
            gallery_managed_file_source(s_decoder_path + 2U, RT_NULL));
}

bool ft_gallery_test_delete_confirmation_visible(void)
{
    return gallery_object_valid(s_delete_box) &&
           gallery_object_valid(s_delete_cancel) &&
           gallery_object_valid(s_delete_confirm) &&
           s_current_file[0] != '\0';
}

bool ft_gallery_test_uses_dedicated_collection(void)
{
    if (strcmp(s_current_path, gallery_collection_path(s_source)) != 0 ||
        strcmp(s_current_path, FT_STORAGE_FLASH_MOUNT_PATH) == 0 ||
        strcmp(s_current_path, FT_STORAGE_SD_MOUNT_PATH) == 0)
        return false;
    return !gallery_source_available(s_source) ||
           gallery_collection_is_directory(s_source);
}

bool ft_gallery_test_entries_decodable(void)
{
    size_t i;
    for (i = 0U; i < s_entry_count; i++)
    {
        ft_storage_entry_t entry;
        ft_gallery_image_format_t format;
        ft_gallery_image_validation_t validation;
        lv_image_header_t verified_header;
        uint64_t byte_limit;
        uint64_t pixel_limit;
        char file_path[FT_STORAGE_PATH_MAX];
        char decoder_path[FT_STORAGE_PATH_MAX + 3U];

        rt_memset(&entry, 0, sizeof(entry));
        entry.type = FT_STORAGE_ENTRY_FILE;
        entry.size_bytes = s_entries[i].size_bytes;
        rt_strncpy(entry.name, s_entries[i].name, sizeof(entry.name) - 1U);
        if (s_entries[i].kind != FT_GALLERY_ENTRY_IMAGE ||
            !gallery_entry_decodable(&entry))
            return false;
        format = gallery_image_format(entry.name);
        byte_limit = format == FT_GALLERY_IMAGE_PNG ?
                     FT_GALLERY_PNG_MAX_BYTES : FT_GALLERY_LARGE_MAX_BYTES;
        pixel_limit = format == FT_GALLERY_IMAGE_PNG ?
                      FT_GALLERY_PNG_MAX_PIXELS : FT_GALLERY_LARGE_MAX_PIXELS;
        if (format == FT_GALLERY_IMAGE_NONE ||
            ft_storage_join_path(s_current_path, entry.name, file_path,
                                 sizeof(file_path)) != RT_EOK ||
            !gallery_make_decoder_path(file_path, s_source, decoder_path,
                                       sizeof(decoder_path)))
            return false;
        rt_memset(&validation, 0, sizeof(validation));
        rt_memset(&verified_header, 0, sizeof(verified_header));
        if (!gallery_validate_image_file(file_path, s_source, format, byte_limit,
                                         pixel_limit, &validation) ||
            validation.file_size != entry.size_bytes ||
            !gallery_decoder_probe(decoder_path, &validation,
                                   &verified_header))
            return false;
    }
    return true;
}

bool ft_gallery_test_thumbnails_ready(void)
{
    size_t index;
    if (s_thumbnail_ready_count != s_entry_count || s_thumbnail_timer != RT_NULL)
        return false;
    for (index = 0U; index < s_entry_count; index++)
    {
        const ft_gallery_entry_t *entry = &s_entries[index];
        const char *detail;
        if (!gallery_object_valid(entry->thumbnail_image) ||
            !gallery_object_valid(entry->detail_label) ||
            entry->thumbnail.draw_buf == RT_NULL ||
            entry->thumbnail.draw_buf->data == RT_NULL ||
            entry->thumbnail.draw_buf->header.cf != LV_COLOR_FORMAT_RGB565 ||
            entry->thumbnail.draw_buf->header.w == 0U ||
            entry->thumbnail.draw_buf->header.h == 0U ||
            entry->thumbnail.source_width == 0U ||
            entry->thumbnail.source_height == 0U)
            return false;
        detail = lv_label_get_text(entry->detail_label);
        if (detail == RT_NULL || strchr(detail, '%') != RT_NULL ||
            strstr(detail, "(NULL)") != RT_NULL)
            return false;
    }
    return true;
}
#endif
