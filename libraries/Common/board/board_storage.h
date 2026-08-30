#ifndef BOARD_STORAGE_H
#define BOARD_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#define BOARD_SDCARD_NAME_MAX       16U
#define BOARD_SDCARD_MAX_PARTITIONS 8U

typedef enum
{
    BOARD_SDCARD_PARTITION_NONE = 0,
    BOARD_SDCARD_PARTITION_MBR,
    BOARD_SDCARD_PARTITION_GPT
} board_sdcard_partition_scheme_t;

typedef struct
{
    uint64_t first_sector;
    uint64_t sector_count;
    uint8_t mbr_type;
} board_sdcard_partition_t;

typedef struct
{
    bool present;
    bool mounted;
    bool exported;
    bool transitioning;
    bool partition_truncated;
    char device_name[BOARD_SDCARD_NAME_MAX];
    char mounted_device[BOARD_SDCARD_NAME_MAX];
    uint64_t sector_count;
    uint32_t bytes_per_sector;
    uint32_t erase_block_size;
    board_sdcard_partition_scheme_t partition_scheme;
    uint8_t partition_count;
    board_sdcard_partition_t partitions[BOARD_SDCARD_MAX_PARTITIONS];
} board_sdcard_info_t;

int board_sdcard_get_info(board_sdcard_info_t *info);
int board_sdcard_format_fat(void);

typedef struct
{
    bool present;
    bool mounted;
    bool exported;
    bool transitioning;
    char device_name[BOARD_SDCARD_NAME_MAX];
    uint64_t sector_count;
    uint32_t bytes_per_sector;
    uint32_t erase_block_size;
} board_flash_storage_info_t;

int board_flash_storage_device_init(void);
int board_flash_storage_sync(void);
int board_flash_storage_get_info(board_flash_storage_info_t *info);
int board_flash_storage_format_fat(void);
int board_flash_storage_export_begin(const char **device_name);
int board_flash_storage_export_end(void);
bool board_flash_storage_is_exported(void);

int board_sdcard_export_begin(const char **device_name);
int board_sdcard_export_end(void);
bool board_sdcard_export_present(void);
bool board_sdcard_is_exported(void);

#endif /* BOARD_STORAGE_H */
