#include <rtthread.h>

#ifdef RT_USING_DFS
#include <dfs_fs.h>
#include <rtdevice.h>
#ifdef BSP_USING_FLASH
#include <fal.h>
#endif
#include <drivers/mmcsd_core.h>
#include "board_storage.h"
#include "dfs_romfs.h"

#define DBG_TAG "app.filesystem"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define SDCARD_MOUNT_POINT      "/sdcard"
#define SDCARD_FS_TYPE          "elm"
#define SDCARD_POLL_MS          1000
#define SDCARD_RESCAN_MS        3000
#define SDCARD_REMOVE_ERRORS    2
#define SDCARD_QUIET_MS         8000
#define FLASH_MOUNT_POINT       "/flash"
#define FLASH_FS_TYPE           "elm"
#define FLASH_DEVICE_NAME       "flash_storage"

extern rt_err_t rt_hw_sdio_rescan(void);
extern rt_err_t rt_hw_sdio_force_change(void);
extern void rt_hw_sdio_quiet_for(rt_uint32_t timeout_ms);
extern void rt_hw_sdio_quiet_begin(void);
extern void rt_hw_sdio_quiet_end(void);

static rt_bool_t g_sdcard_mounted = RT_FALSE;
static rt_bool_t g_sdcard_ejected = RT_FALSE;
static rt_bool_t g_sdcard_rescan_pending = RT_TRUE;
static rt_bool_t g_sdcard_exported = RT_FALSE;
static rt_bool_t g_sdcard_export_present = RT_FALSE;
static rt_bool_t g_sdcard_transitioning = RT_FALSE;
static const char *g_sdcard_mounted_device = RT_NULL;
static rt_bool_t g_flash_mounted = RT_FALSE;
static rt_bool_t g_flash_exported = RT_FALSE;
static rt_bool_t g_flash_transitioning = RT_FALSE;

static rt_uint16_t _sdcard_le16(const rt_uint8_t *value)
{
    return (rt_uint16_t)value[0] | ((rt_uint16_t)value[1] << 8);
}

static rt_uint32_t _sdcard_le32(const rt_uint8_t *value)
{
    return (rt_uint32_t)value[0] | ((rt_uint32_t)value[1] << 8) |
           ((rt_uint32_t)value[2] << 16) | ((rt_uint32_t)value[3] << 24);
}

static rt_uint64_t _sdcard_le64(const rt_uint8_t *value)
{
    return (rt_uint64_t)_sdcard_le32(value) |
           ((rt_uint64_t)_sdcard_le32(value + 4) << 32);
}

static rt_bool_t _sdcard_guid_is_zero(const rt_uint8_t *guid)
{
    int i;
    for (i = 0; i < 16; i++)
        if (guid[i] != 0U) return RT_FALSE;
    return RT_TRUE;
}

static rt_bool_t _sdcard_read_sector(rt_device_t device, rt_uint64_t sector,
                                     rt_uint8_t *buffer)
{
    rt_size_t read_count;
    rt_hw_sdio_quiet_begin();
    read_count = rt_device_read(device, sector, buffer, 1U);
    rt_hw_sdio_quiet_end();
    return read_count == 1U ? RT_TRUE : RT_FALSE;
}

static void _sdcard_record_partition(board_sdcard_info_t *info,
                                     rt_uint64_t first_sector,
                                     rt_uint64_t sector_count,
                                     rt_uint8_t mbr_type)
{
    if (sector_count == 0U || first_sector >= info->sector_count ||
        sector_count > info->sector_count - first_sector)
        return;
    if (info->partition_count < BOARD_SDCARD_MAX_PARTITIONS)
    {
        board_sdcard_partition_t *partition =
            &info->partitions[info->partition_count];
        partition->first_sector = first_sector;
        partition->sector_count = sector_count;
        partition->mbr_type = mbr_type;
    }
    else
    {
        info->partition_truncated = true;
    }
    if (info->partition_count < UINT8_MAX) info->partition_count++;
}

static void _sdcard_scan_gpt(rt_device_t device, board_sdcard_info_t *info,
                             rt_uint8_t *sector)
{
    rt_uint64_t entry_lba;
    rt_uint32_t entry_count;
    rt_uint32_t entry_size;
    rt_uint32_t index;
    rt_uint64_t loaded_sector = UINT64_MAX;

    if (!_sdcard_read_sector(device, 1U, sector) ||
        rt_memcmp(sector, "EFI PART", 8U) != 0)
        return;
    entry_lba = _sdcard_le64(sector + 72U);
    entry_count = _sdcard_le32(sector + 80U);
    entry_size = _sdcard_le32(sector + 84U);
    if (entry_size < 56U || entry_size > info->bytes_per_sector ||
        entry_count == 0U || entry_lba >= info->sector_count)
        return;

    info->partition_scheme = BOARD_SDCARD_PARTITION_GPT;
    info->partition_count = 0U;
    for (index = 0U; index < entry_count; index++)
    {
        rt_uint64_t byte_offset = (rt_uint64_t)index * entry_size;
        rt_uint64_t entry_sector = entry_lba + byte_offset / info->bytes_per_sector;
        rt_uint32_t offset = (rt_uint32_t)(byte_offset % info->bytes_per_sector);
        rt_uint64_t first_sector;
        rt_uint64_t last_sector;

        if (entry_sector >= info->sector_count ||
            offset + 56U > info->bytes_per_sector)
            break;
        if (loaded_sector != entry_sector)
        {
            if (!_sdcard_read_sector(device, entry_sector, sector)) break;
            loaded_sector = entry_sector;
        }
        if (_sdcard_guid_is_zero(sector + offset)) continue;
        first_sector = _sdcard_le64(sector + offset + 32U);
        last_sector = _sdcard_le64(sector + offset + 40U);
        if (last_sector < first_sector) continue;
        _sdcard_record_partition(info, first_sector,
                                 last_sector - first_sector + 1U, 0U);
    }
}

static void _sdcard_scan_partitions(rt_device_t device,
                                    board_sdcard_info_t *info)
{
    rt_uint8_t *sector;
    int index;
    rt_bool_t protective_gpt = RT_FALSE;

    if (info->bytes_per_sector < 512U) return;
    sector = rt_malloc_align(info->bytes_per_sector, 32U);
    if (sector == RT_NULL) return;
    if (!_sdcard_read_sector(device, 0U, sector) ||
        _sdcard_le16(sector + 510U) != 0xAA55U)
    {
        rt_free_align(sector);
        return;
    }

    for (index = 0; index < 4; index++)
    {
        const rt_uint8_t *entry = sector + 446U + index * 16U;
        rt_uint8_t type = entry[4];
        rt_uint32_t first_sector = _sdcard_le32(entry + 8U);
        rt_uint32_t sector_count = _sdcard_le32(entry + 12U);
        if (type == 0xEEU)
        {
            protective_gpt = RT_TRUE;
            break;
        }
        if (type != 0U && sector_count != 0U)
            _sdcard_record_partition(info, first_sector, sector_count, type);
    }
    if (protective_gpt)
        _sdcard_scan_gpt(device, info, sector);
    else if (info->partition_count > 0U)
        info->partition_scheme = BOARD_SDCARD_PARTITION_MBR;
    rt_free_align(sector);
}

#ifndef BSP_USING_XiaoZhi
static const struct romfs_dirent _romfs_root[] =
{
#ifdef BSP_USING_FLASH
    {ROMFS_DIRENT_DIR, "flash", RT_NULL, 0},
#endif
#ifdef BSP_USING_SDCARD
    {ROMFS_DIRENT_DIR, "sdcard", RT_NULL, 0},
#endif
};

const struct romfs_dirent romfs_root =
{
    ROMFS_DIRENT_DIR, "/", (rt_uint8_t *)_romfs_root, sizeof(_romfs_root) / sizeof(_romfs_root[0])
};
#endif

static const char *_sdcard_find_device(void)
{
#ifdef BSP_USING_SDCARD
    const char *sd_device_names[] = {"sd", "sd0", "sd1", "sd2"};
    int i;

    for (i = 0; i < sizeof(sd_device_names) / sizeof(sd_device_names[0]); i++)
    {
        if (rt_device_find(sd_device_names[i]) != RT_NULL)
        {
            return sd_device_names[i];
        }
    }
#endif
    return RT_NULL;
}

static rt_bool_t _sdcard_probe_device(const char *device_name)
{
#ifdef BSP_USING_SDCARD
    rt_device_t device;
    rt_uint8_t *sector;
    rt_size_t read_count;

    if (device_name == RT_NULL)
    {
        return RT_FALSE;
    }

    device = rt_device_find(device_name);
    if (device == RT_NULL)
    {
        return RT_FALSE;
    }

    sector = rt_malloc_align(512, 32);
    if (sector == RT_NULL)
    {
        return RT_TRUE;
    }

    rt_hw_sdio_quiet_begin();
    read_count = rt_device_read(device, 0, sector, 1);
    rt_hw_sdio_quiet_end();
    rt_free_align(sector);

    return (read_count == 1) ? RT_TRUE : RT_FALSE;
#else
    RT_UNUSED(device_name);
    return RT_FALSE;
#endif
}

static rt_bool_t _sdcard_mount(const char **mounted_device)
{
#ifdef BSP_USING_SDCARD
    const char *device_name = _sdcard_find_device();

    if (device_name == RT_NULL)
    {
        return RT_FALSE;
    }

    rt_thread_mdelay(200);

    if (dfs_mount(device_name, SDCARD_MOUNT_POINT, SDCARD_FS_TYPE, 0, 0) == RT_EOK)
    {
        *mounted_device = device_name;
        LOG_I("sd card '%s' mount to '%s' success!", device_name, SDCARD_MOUNT_POINT);
        return RT_TRUE;
    }

    LOG_E("sd card mount to '%s' failed. Run 'sdcard_mkfs' manually to format.", SDCARD_MOUNT_POINT);
#endif /* BSP_USING_SDCARD */
    return RT_FALSE;
}

#ifdef BSP_USING_FLASH
static void _fal_mount(void)
{
#ifdef FEATHERTALK_USING_FLASH_STORAGE
    if (board_flash_storage_device_init() != RT_EOK)
    {
        LOG_E("Can't create FeatherTalk flash block device");
        return;
    }
    rt_thread_mdelay(100);
    if (dfs_mount(FLASH_DEVICE_NAME, FLASH_MOUNT_POINT,
                  FLASH_FS_TYPE, 0, 0) != RT_EOK)
    {
        LOG_W("flash FAT mount failed; initializing reserved user storage");
        if (dfs_mkfs(FLASH_FS_TYPE, FLASH_DEVICE_NAME) != RT_EOK ||
            dfs_mount(FLASH_DEVICE_NAME, FLASH_MOUNT_POINT,
                      FLASH_FS_TYPE, 0, 0) != RT_EOK)
        {
            LOG_E("initialize flash user storage failed");
            return;
        }
    }
    g_flash_mounted = RT_TRUE;
    LOG_I("flash user storage mounted at '%s'", FLASH_MOUNT_POINT);
#else
    struct rt_device *flash_dev = fal_mtd_nor_device_create("filesystem");
    if (flash_dev == NULL)
    {
        LOG_E("Can't create block device for filesystem");
        return;
    }
    else
    {
        LOG_I("Block device created for filesystem");

        rt_thread_mdelay(200);

        /* Try to mount filesystem */
        if (dfs_mount("filesystem", "/flash", "lfs", 0, 0) != 0)
        {
            LOG_E("Mount filesystem failed, try to mkfs");

            /* Format filesystem */
            rt_thread_mdelay(200);

            if (dfs_mkfs("lfs", "filesystem") != 0)
            {
                LOG_E("mkfs failed");
                return;
            }
            else
            {
                LOG_I("Filesystem formatted");

                /* Mount after format */
                if (dfs_mount("filesystem", "/flash", "lfs", 0, 0) != 0)
                {
                    LOG_E("Mount filesystem failed after mkfs");
                    return;
                }
                else
                {
                    LOG_I("Filesystem mounted successfully");
                }
            }
        }
        else
        {
            LOG_I("Filesystem mounted successfully");
        }
    }
#endif /* FEATHERTALK_USING_FLASH_STORAGE */
}
#endif /* BSP_USING_FLASH */

static void sd_hotplug_thread(void *parameter)
{
    rt_tick_t last_rescan_tick = 0;
    int remove_errors = 0;

    RT_UNUSED(parameter);

    rt_thread_mdelay(200);

    while (1)
    {
        int cd_event;

        /*
         * USB Device MSC owns the raw block device while it is exported.
         * Do not probe or mount the same media locally during that period:
         * concurrent FAT and host writes would corrupt the card.  We still
         * consume card-detect events so the product layer can disconnect MSC
         * promptly when the user removes the card.
         */
        if (g_sdcard_exported || g_sdcard_transitioning)
        {
            cd_event = mmcsd_wait_cd_changed(
                rt_tick_from_millisecond(SDCARD_POLL_MS));
            if (g_sdcard_exported && cd_event == MMCSD_HOST_UNPLUGED)
            {
                g_sdcard_export_present = RT_FALSE;
            }
            continue;
        }

        if (!g_sdcard_mounted)
        {
            const char *device_name = _sdcard_find_device();

            if ((device_name != RT_NULL) && (g_sdcard_ejected == RT_FALSE))
            {
                g_sdcard_mounted = _sdcard_mount(&g_sdcard_mounted_device);
                remove_errors = 0;
                g_sdcard_rescan_pending = !g_sdcard_mounted;
                rt_thread_mdelay(SDCARD_POLL_MS);
                continue;
            }

            if ((device_name == RT_NULL) && (g_sdcard_ejected == RT_TRUE))
            {
                g_sdcard_ejected = RT_FALSE;
                g_sdcard_rescan_pending = RT_TRUE;
            }

            if (g_sdcard_rescan_pending ||
                ((rt_tick_get() - last_rescan_tick) >= rt_tick_from_millisecond(SDCARD_RESCAN_MS)))
            {
                rt_hw_sdio_quiet_for(SDCARD_QUIET_MS);
                (void)rt_hw_sdio_rescan();
                last_rescan_tick = rt_tick_get();
                g_sdcard_rescan_pending = RT_FALSE;
            }

            cd_event = mmcsd_wait_cd_changed(rt_tick_from_millisecond(SDCARD_POLL_MS));
            if ((cd_event == MMCSD_HOST_PLUGED) && (g_sdcard_ejected == RT_FALSE))
            {
                g_sdcard_mounted = _sdcard_mount(&g_sdcard_mounted_device);
                remove_errors = 0;
                g_sdcard_rescan_pending = !g_sdcard_mounted;
            }
        }
        else
        {
            cd_event = mmcsd_wait_cd_changed(rt_tick_from_millisecond(SDCARD_POLL_MS));
            if (cd_event == MMCSD_HOST_UNPLUGED)
            {
                remove_errors = SDCARD_REMOVE_ERRORS;
            }
            else if (!_sdcard_probe_device(g_sdcard_mounted_device))
            {
                remove_errors++;
            }
            else
            {
                remove_errors = 0;
            }

            if (remove_errors >= SDCARD_REMOVE_ERRORS)
            {
                if (dfs_unmount(SDCARD_MOUNT_POINT) == RT_EOK)
                {
                    LOG_I("sd card unmount from '%s' success!", SDCARD_MOUNT_POINT);
                }
                else
                {
                    LOG_W("sd card unmount from '%s' failed", SDCARD_MOUNT_POINT);
                }

                g_sdcard_mounted = RT_FALSE;
                g_sdcard_mounted_device = RT_NULL;
                remove_errors = 0;
                g_sdcard_rescan_pending = RT_TRUE;
                rt_hw_sdio_quiet_for(SDCARD_QUIET_MS);
                (void)rt_hw_sdio_force_change();
            }
        }
    }
}

/*
 * Transfer exclusive ownership of the SD block device to a USB Device MSC
 * function.  These product-facing helpers intentionally live beside the
 * hot-plug state machine so it remains the single owner of mount state.
 */
int board_sdcard_export_begin(const char **device_name)
{
#ifdef BSP_USING_SDCARD
    const char *name = "sd";

    if (device_name == RT_NULL || g_sdcard_exported ||
        g_sdcard_transitioning || !g_sdcard_mounted)
    {
        return -RT_EBUSY;
    }

    /* Expose the physical disk so the host can inspect, repartition and
     * format it rather than seeing only one logical volume. */
    if (rt_device_find(name) == RT_NULL)
    {
        return -RT_ENOENT;
    }

    g_sdcard_transitioning = RT_TRUE;
    if (dfs_unmount(SDCARD_MOUNT_POINT) != RT_EOK)
    {
        g_sdcard_transitioning = RT_FALSE;
        return -RT_EBUSY;
    }

    g_sdcard_mounted = RT_FALSE;
    g_sdcard_export_present = RT_TRUE;
    g_sdcard_exported = RT_TRUE;
    g_sdcard_transitioning = RT_FALSE;
    *device_name = name;
    LOG_I("sd card '%s' exported exclusively to USB MSC", name);
    return RT_EOK;
#else
    RT_UNUSED(device_name);
    return -RT_ENOSYS;
#endif
}

int board_sdcard_export_end(void)
{
#ifdef BSP_USING_SDCARD
    rt_bool_t present;

    if (!g_sdcard_exported)
    {
        return RT_EOK;
    }

    g_sdcard_transitioning = RT_TRUE;
    present = g_sdcard_export_present;
    g_sdcard_exported = RT_FALSE;
    g_sdcard_export_present = RT_FALSE;
    g_sdcard_mounted_device = RT_NULL;
    g_sdcard_ejected = RT_FALSE;
    g_sdcard_rescan_pending = RT_TRUE;

    if (present)
    {
        g_sdcard_mounted = _sdcard_mount(&g_sdcard_mounted_device);
        g_sdcard_rescan_pending = !g_sdcard_mounted;
    }
    else
    {
        g_sdcard_mounted = RT_FALSE;
        rt_hw_sdio_quiet_for(SDCARD_QUIET_MS);
        (void)rt_hw_sdio_force_change();
    }
    g_sdcard_transitioning = RT_FALSE;

    LOG_I("USB MSC released sd card; local mount is %s",
          g_sdcard_mounted ? "ready" : "pending");
    return g_sdcard_mounted || !present ? RT_EOK : -RT_ERROR;
#else
    return -RT_ENOSYS;
#endif
}

bool board_sdcard_export_present(void)
{
    return (g_sdcard_exported && g_sdcard_export_present) ? true : false;
}

bool board_sdcard_is_exported(void)
{
    return g_sdcard_exported ? true : false;
}

int board_sdcard_get_info(board_sdcard_info_t *info)
{
#ifdef BSP_USING_SDCARD
    rt_device_t device;
    struct rt_device_blk_geometry geometry;

    if (info == RT_NULL) return -RT_EINVAL;
    rt_memset(info, 0, sizeof(*info));
    rt_strncpy(info->device_name, "sd", sizeof(info->device_name) - 1U);
    info->mounted = g_sdcard_mounted == RT_TRUE;
    info->exported = g_sdcard_exported == RT_TRUE;
    info->transitioning = g_sdcard_transitioning == RT_TRUE;
    if (g_sdcard_mounted_device != RT_NULL)
        rt_strncpy(info->mounted_device, g_sdcard_mounted_device,
                   sizeof(info->mounted_device) - 1U);

    device = rt_device_find("sd");
    if (device == RT_NULL) return -RT_ENOENT;
    rt_memset(&geometry, 0, sizeof(geometry));
    if (rt_device_control(device, RT_DEVICE_CTRL_BLK_GETGEOME,
                          &geometry) != RT_EOK ||
        geometry.bytes_per_sector == 0U || geometry.sector_count == 0U)
        return -RT_EIO;

    info->present = true;
    info->sector_count = geometry.sector_count;
    info->bytes_per_sector = geometry.bytes_per_sector;
    info->erase_block_size = geometry.block_size;
    /* Never race a host write or an ownership transition for metadata. */
    if (!info->exported && !info->transitioning)
        _sdcard_scan_partitions(device, info);
    return RT_EOK;
#else
    if (info != RT_NULL) rt_memset(info, 0, sizeof(*info));
    return -RT_ENOSYS;
#endif
}

int board_sdcard_format_fat(void)
{
#ifdef BSP_USING_SDCARD
    rt_bool_t was_mounted;
    int result;

    if (g_sdcard_exported || g_sdcard_transitioning)
        return -RT_EBUSY;
    if (rt_device_find("sd") == RT_NULL)
        return -RT_ENOENT;

    g_sdcard_transitioning = RT_TRUE;
    was_mounted = g_sdcard_mounted;
    if (was_mounted && dfs_unmount(SDCARD_MOUNT_POINT) != RT_EOK)
    {
        g_sdcard_transitioning = RT_FALSE;
        return -RT_EBUSY;
    }
    g_sdcard_mounted = RT_FALSE;
    g_sdcard_mounted_device = RT_NULL;

    LOG_W("formatting the entire physical SD card 'sd' as one FAT volume");
    result = dfs_mkfs(SDCARD_FS_TYPE, "sd");
    if (result == RT_EOK)
    {
        g_sdcard_ejected = RT_FALSE;
        g_sdcard_mounted = _sdcard_mount(&g_sdcard_mounted_device);
        g_sdcard_rescan_pending = !g_sdcard_mounted;
        if (!g_sdcard_mounted) result = -RT_EIO;
    }
    else if (was_mounted)
    {
        /* If formatting failed before producing a usable filesystem, make a
         * best-effort attempt to restore the previous local mount. */
        g_sdcard_mounted = _sdcard_mount(&g_sdcard_mounted_device);
        g_sdcard_rescan_pending = !g_sdcard_mounted;
    }
    g_sdcard_transitioning = RT_FALSE;
    return result;
#else
    return -RT_ENOSYS;
#endif
}

int board_flash_storage_get_info(board_flash_storage_info_t *info)
{
#if defined(BSP_USING_FLASH) && defined(FEATHERTALK_USING_FLASH_STORAGE)
    rt_device_t device;
    struct rt_device_blk_geometry geometry;

    if (info == RT_NULL) return -RT_EINVAL;
    rt_memset(info, 0, sizeof(*info));
    rt_strncpy(info->device_name, FLASH_DEVICE_NAME,
               sizeof(info->device_name) - 1U);
    info->mounted = g_flash_mounted == RT_TRUE;
    info->exported = g_flash_exported == RT_TRUE;
    info->transitioning = g_flash_transitioning == RT_TRUE;
    device = rt_device_find(FLASH_DEVICE_NAME);
    if (device == RT_NULL) return -RT_ENOENT;
    rt_memset(&geometry, 0, sizeof(geometry));
    if (rt_device_control(device, RT_DEVICE_CTRL_BLK_GETGEOME,
                          &geometry) != RT_EOK)
        return -RT_EIO;
    info->present = true;
    info->sector_count = geometry.sector_count;
    info->bytes_per_sector = geometry.bytes_per_sector;
    info->erase_block_size = geometry.block_size;
    return RT_EOK;
#else
    if (info != RT_NULL) rt_memset(info, 0, sizeof(*info));
    return -RT_ENOSYS;
#endif
}

int board_flash_storage_export_begin(const char **device_name)
{
#if defined(BSP_USING_FLASH) && defined(FEATHERTALK_USING_FLASH_STORAGE)
    if (device_name == RT_NULL || g_flash_exported ||
        g_flash_transitioning || !g_flash_mounted)
        return -RT_EBUSY;
    if (rt_device_find(FLASH_DEVICE_NAME) == RT_NULL)
        return -RT_ENOENT;
    g_flash_transitioning = RT_TRUE;
    if (board_flash_storage_sync() != RT_EOK ||
        dfs_unmount(FLASH_MOUNT_POINT) != RT_EOK)
    {
        g_flash_transitioning = RT_FALSE;
        return -RT_EBUSY;
    }
    g_flash_mounted = RT_FALSE;
    g_flash_exported = RT_TRUE;
    g_flash_transitioning = RT_FALSE;
    *device_name = FLASH_DEVICE_NAME;
    LOG_I("flash storage exported exclusively to USB MSC");
    return RT_EOK;
#else
    RT_UNUSED(device_name);
    return -RT_ENOSYS;
#endif
}

int board_flash_storage_export_end(void)
{
#if defined(BSP_USING_FLASH) && defined(FEATHERTALK_USING_FLASH_STORAGE)
    if (!g_flash_exported) return RT_EOK;
    g_flash_transitioning = RT_TRUE;
    g_flash_exported = RT_FALSE;
    g_flash_mounted = dfs_mount(FLASH_DEVICE_NAME, FLASH_MOUNT_POINT,
                                FLASH_FS_TYPE, 0, 0) == RT_EOK;
    g_flash_transitioning = RT_FALSE;
    LOG_I("USB MSC released flash storage; local mount is %s",
          g_flash_mounted ? "ready" : "failed");
    return g_flash_mounted ? RT_EOK : -RT_EIO;
#else
    return -RT_ENOSYS;
#endif
}

bool board_flash_storage_is_exported(void)
{
    return g_flash_exported ? true : false;
}

int board_flash_storage_format_fat(void)
{
#if defined(BSP_USING_FLASH) && defined(FEATHERTALK_USING_FLASH_STORAGE)
    rt_bool_t was_mounted;
    int result;

    if (g_flash_exported || g_flash_transitioning)
        return -RT_EBUSY;
    if (rt_device_find(FLASH_DEVICE_NAME) == RT_NULL)
        return -RT_ENOENT;
    g_flash_transitioning = RT_TRUE;
    was_mounted = g_flash_mounted;
    if (was_mounted &&
        (board_flash_storage_sync() != RT_EOK ||
         dfs_unmount(FLASH_MOUNT_POINT) != RT_EOK))
    {
        g_flash_transitioning = RT_FALSE;
        return -RT_EBUSY;
    }
    g_flash_mounted = RT_FALSE;
    result = dfs_mkfs(FLASH_FS_TYPE, FLASH_DEVICE_NAME);
    if (result == RT_EOK)
    {
        g_flash_mounted = dfs_mount(FLASH_DEVICE_NAME, FLASH_MOUNT_POINT,
                                    FLASH_FS_TYPE, 0, 0) == RT_EOK;
        if (!g_flash_mounted) result = -RT_EIO;
    }
    else if (was_mounted)
    {
        g_flash_mounted = dfs_mount(FLASH_DEVICE_NAME, FLASH_MOUNT_POINT,
                                    FLASH_FS_TYPE, 0, 0) == RT_EOK;
    }
    g_flash_transitioning = RT_FALSE;
    return result;
#else
    return -RT_ENOSYS;
#endif
}

#ifdef BSP_USING_SDCARD
static int sdcard_umount(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (g_sdcard_mounted == RT_FALSE)
    {
        rt_kprintf("%s is not mounted\n", SDCARD_MOUNT_POINT);
        return -RT_ERROR;
    }

    if (dfs_unmount(SDCARD_MOUNT_POINT) != RT_EOK)
    {
        rt_kprintf("unmount %s failed\n", SDCARD_MOUNT_POINT);
        return -RT_ERROR;
    }

    g_sdcard_mounted = RT_FALSE;
    g_sdcard_mounted_device = RT_NULL;
    g_sdcard_ejected = RT_TRUE;
    g_sdcard_rescan_pending = RT_FALSE;
    rt_hw_sdio_quiet_for(SDCARD_QUIET_MS);

    rt_kprintf("%s unmounted, safe to remove SD card\n", SDCARD_MOUNT_POINT);
    return RT_EOK;
}
MSH_CMD_EXPORT(sdcard_umount, unmount sd card before removing);

static int sdcard_mount(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (g_sdcard_mounted == RT_TRUE)
    {
        rt_kprintf("%s is already mounted\n", SDCARD_MOUNT_POINT);
        return RT_EOK;
    }

    g_sdcard_ejected = RT_FALSE;
    if (_sdcard_mount(&g_sdcard_mounted_device) == RT_FALSE)
    {
        g_sdcard_rescan_pending = RT_TRUE;
        rt_kprintf("mount %s failed\n", SDCARD_MOUNT_POINT);
        return -RT_ERROR;
    }

    g_sdcard_mounted = RT_TRUE;
    g_sdcard_rescan_pending = RT_FALSE;
    return RT_EOK;
}
MSH_CMD_EXPORT(sdcard_mount, mount sd card);

static int sdcard_mkfs(int argc, char **argv)
{
    int result;

    if (argc != 2 || rt_strcmp(argv[1], "ERASE-ALL") != 0)
    {
        rt_kprintf("DANGER: this erases every partition and file on the SD card.\n");
        rt_kprintf("Use: sdcard_mkfs ERASE-ALL\n");
        return -RT_EINVAL;
    }
    result = board_sdcard_format_fat();
    if (result == RT_EOK)
        rt_kprintf("format and remount sd card success\n");
    else
        rt_kprintf("format sd card failed: %d\n", result);
    return result;
}
MSH_CMD_EXPORT(sdcard_mkfs, format entire sd card; requires ERASE-ALL);
#endif

int mnt_init(void)
{
    if (dfs_mount(RT_NULL, "/", "rom", 0, &(romfs_root)) != 0)
    {
        LOG_E("rom mount to '/' failed!");
        return -RT_ERROR;
    }

#ifdef BSP_USING_FLASH
    fal_init();
    /* Mount FAL filesystem to /flash */
    _fal_mount();
#endif

#ifdef BSP_USING_SDCARD
    rt_thread_t tid;

    tid = rt_thread_create("sd_hotplug", sd_hotplug_thread, RT_NULL,
                           2048, RT_THREAD_PRIORITY_MAX - 2, 20);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        LOG_E("create sd_mount thread err!");
    }
#endif

    return RT_EOK;
}
INIT_ENV_EXPORT(mnt_init);

#endif
