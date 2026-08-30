#ifndef FEATHERTALK_STORAGE_H
#define FEATHERTALK_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT_STORAGE_SD_MOUNT_PATH "/sdcard"
#define FT_STORAGE_PATH_MAX      256U
#define FT_STORAGE_NAME_MAX      256U

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
int ft_storage_list(const char *path, ft_storage_entry_type_t filter,
                    ft_storage_entry_cb_t callback, void *context);
int ft_storage_join_path(const char *parent, const char *name,
                         char *path, size_t path_size);
bool ft_storage_parent_path(char *path, const char *mount_path);
int ft_storage_read_preview(const char *path, uint8_t *buffer,
                            size_t buffer_size, bool *binary,
                            uint64_t *file_size);

#endif
