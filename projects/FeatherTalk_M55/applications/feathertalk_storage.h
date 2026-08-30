#ifndef FEATHERTALK_STORAGE_H
#define FEATHERTALK_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT_STORAGE_BROWSE_ROOT      "/"
#define FT_STORAGE_FLASH_MOUNT_PATH "/flash"
#define FT_STORAGE_SD_MOUNT_PATH    "/sdcard"
#define FT_STORAGE_PATH_MAX         256U
#define FT_STORAGE_NAME_MAX         256U
#define FT_STORAGE_MAX_PARTITIONS 8U

typedef enum
{
    FT_STORAGE_ENTRY_DIRECTORY = 0,
    FT_STORAGE_ENTRY_FILE,
    FT_STORAGE_ENTRY_ANY
} ft_storage_entry_type_t;

typedef struct
{
    bool mounted;
    char filesystem[12];
    uint64_t total_bytes;
    uint64_t free_bytes;
} ft_storage_volume_info_t;

typedef enum
{
    FT_STORAGE_PARTITION_NONE = 0,
    FT_STORAGE_PARTITION_MBR,
    FT_STORAGE_PARTITION_GPT
} ft_storage_partition_scheme_t;

typedef struct
{
    uint64_t first_sector;
    uint64_t sector_count;
    uint8_t mbr_type;
} ft_storage_partition_t;

typedef struct
{
    bool present;
    bool mounted;
    bool usb_exported;
    bool busy;
    bool can_format;
    bool partition_truncated;
    char device[16];
    char mounted_device[16];
    char filesystem[12];
    uint64_t total_bytes;
    uint64_t volume_total_bytes;
    uint64_t volume_free_bytes;
    uint64_t sector_count;
    uint32_t bytes_per_sector;
    uint32_t erase_block_size;
    ft_storage_partition_scheme_t partition_scheme;
    uint8_t partition_count;
    ft_storage_partition_t partitions[FT_STORAGE_MAX_PARTITIONS];
} ft_storage_device_info_t;

typedef struct
{
    ft_storage_entry_type_t type;
    char name[FT_STORAGE_NAME_MAX];
    uint64_t size_bytes;
} ft_storage_entry_t;

typedef bool (*ft_storage_entry_cb_t)(const ft_storage_entry_t *entry,
                                      void *context);

int ft_storage_get_volume(const char *mount_path,
                          ft_storage_volume_info_t *info);
int ft_storage_get_device_info(ft_storage_device_info_t *info);
int ft_storage_get_flash_info(ft_storage_device_info_t *info);
int ft_storage_format_sd(void);
int ft_storage_format_flash(void);
int ft_storage_list(const char *path, ft_storage_entry_type_t filter,
                    ft_storage_entry_cb_t callback, void *context);
int ft_storage_join_path(const char *parent, const char *name,
                         char *path, size_t path_size);
bool ft_storage_parent_path(char *path, const char *mount_path);
int ft_storage_read_preview(const char *path, uint8_t *buffer,
                            size_t buffer_size, bool *binary,
                            uint64_t *file_size);

#endif
