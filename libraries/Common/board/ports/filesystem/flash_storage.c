#include <rtthread.h>
#include <rtdevice.h>

#if defined(BSP_USING_FLASH) && defined(FEATHERTALK_USING_FLASH_STORAGE)

#include <fal.h>

extern struct fal_flash_dev nor_flash0;

#define FLASH_STORAGE_PARTITION   "filesystem"
#define FLASH_STORAGE_DEVICE      "flash_storage"
#define FLASH_STORAGE_SECTOR_SIZE 512U

typedef struct
{
    struct rt_device parent;
    struct rt_device_blk_geometry geometry;
    const struct fal_partition *partition;
    struct rt_mutex lock;
    rt_uint8_t *cache;
    rt_uint32_t cached_block;
    rt_bool_t cache_valid;
    rt_bool_t cache_dirty;
    rt_bool_t initialized;
} board_flash_block_device_t;

static board_flash_block_device_t s_flash;

static int flash_storage_flush_locked(void)
{
    rt_uint32_t offset;
    int result;

    if (!s_flash.cache_valid || !s_flash.cache_dirty) return RT_EOK;
    offset = s_flash.cached_block * s_flash.geometry.block_size;
    result = fal_partition_erase(s_flash.partition, offset,
                                 s_flash.geometry.block_size);
    if (result != (int)s_flash.geometry.block_size) return -RT_EIO;
    result = fal_partition_write(s_flash.partition, offset, s_flash.cache,
                                 s_flash.geometry.block_size);
    if (result != (int)s_flash.geometry.block_size) return -RT_EIO;
    s_flash.cache_dirty = RT_FALSE;
    return RT_EOK;
}

static int flash_storage_load_locked(rt_uint32_t block)
{
    rt_uint32_t offset;
    int result;

    if (s_flash.cache_valid && s_flash.cached_block == block) return RT_EOK;
    result = flash_storage_flush_locked();
    if (result != RT_EOK) return result;
    offset = block * s_flash.geometry.block_size;
    result = fal_partition_read(s_flash.partition, offset, s_flash.cache,
                                s_flash.geometry.block_size);
    if (result != (int)s_flash.geometry.block_size) return -RT_EIO;
    s_flash.cached_block = block;
    s_flash.cache_valid = RT_TRUE;
    s_flash.cache_dirty = RT_FALSE;
    return RT_EOK;
}

static rt_err_t flash_storage_open(rt_device_t device, rt_uint16_t oflag)
{
    RT_UNUSED(device);
    RT_UNUSED(oflag);
    return RT_EOK;
}

static rt_err_t flash_storage_close(rt_device_t device)
{
    int result;
    RT_UNUSED(device);
    rt_mutex_take(&s_flash.lock, RT_WAITING_FOREVER);
    result = flash_storage_flush_locked();
    rt_mutex_release(&s_flash.lock);
    return result;
}

static rt_ssize_t flash_storage_read(rt_device_t device, rt_off_t position,
                                     void *buffer, rt_size_t count)
{
    rt_uint8_t *target = buffer;
    rt_uint64_t byte_offset;
    rt_uint64_t byte_count;
    rt_uint64_t done = 0U;
    int result = RT_EOK;

    RT_UNUSED(device);
    if (buffer == RT_NULL || position < 0 || count == 0U) return 0;
    byte_offset = (rt_uint64_t)position * FLASH_STORAGE_SECTOR_SIZE;
    byte_count = (rt_uint64_t)count * FLASH_STORAGE_SECTOR_SIZE;
    if (byte_offset + byte_count > s_flash.partition->len) return 0;

    rt_mutex_take(&s_flash.lock, RT_WAITING_FOREVER);
    while (done < byte_count)
    {
        rt_uint64_t absolute = byte_offset + done;
        rt_uint32_t block = (rt_uint32_t)(absolute /
                                          s_flash.geometry.block_size);
        rt_uint32_t in_block = (rt_uint32_t)(absolute %
                                             s_flash.geometry.block_size);
        rt_uint32_t chunk = s_flash.geometry.block_size - in_block;
        if (chunk > byte_count - done) chunk = (rt_uint32_t)(byte_count - done);
        if (s_flash.cache_valid && s_flash.cached_block == block)
            rt_memcpy(target + done, s_flash.cache + in_block, chunk);
        else if (fal_partition_read(s_flash.partition, (long)absolute,
                                    target + done, chunk) != (int)chunk)
        {
            result = -RT_EIO;
            break;
        }
        done += chunk;
    }
    rt_mutex_release(&s_flash.lock);
    return result == RT_EOK ? count : 0;
}

static rt_ssize_t flash_storage_write(rt_device_t device, rt_off_t position,
                                      const void *buffer, rt_size_t count)
{
    const rt_uint8_t *source = buffer;
    rt_uint64_t byte_offset;
    rt_uint64_t byte_count;
    rt_uint64_t done = 0U;
    int result = RT_EOK;

    RT_UNUSED(device);
    if (buffer == RT_NULL || position < 0 || count == 0U) return 0;
    byte_offset = (rt_uint64_t)position * FLASH_STORAGE_SECTOR_SIZE;
    byte_count = (rt_uint64_t)count * FLASH_STORAGE_SECTOR_SIZE;
    if (byte_offset + byte_count > s_flash.partition->len) return 0;

    rt_mutex_take(&s_flash.lock, RT_WAITING_FOREVER);
    while (done < byte_count)
    {
        rt_uint64_t absolute = byte_offset + done;
        rt_uint32_t block = (rt_uint32_t)(absolute /
                                          s_flash.geometry.block_size);
        rt_uint32_t in_block = (rt_uint32_t)(absolute %
                                             s_flash.geometry.block_size);
        rt_uint32_t chunk = s_flash.geometry.block_size - in_block;
        if (chunk > byte_count - done) chunk = (rt_uint32_t)(byte_count - done);

        /* Full erase-block transfers need no read/modify/write cache. */
        if (in_block == 0U && chunk == s_flash.geometry.block_size)
        {
            result = flash_storage_flush_locked();
            if (result != RT_EOK) break;
            if (fal_partition_erase(s_flash.partition, (long)absolute,
                                    chunk) != (int)chunk ||
                fal_partition_write(s_flash.partition, (long)absolute,
                                    source + done, chunk) != (int)chunk)
            {
                result = -RT_EIO;
                break;
            }
            if (s_flash.cache_valid && s_flash.cached_block == block)
                s_flash.cache_valid = RT_FALSE;
        }
        else
        {
            result = flash_storage_load_locked(block);
            if (result != RT_EOK) break;
            if (rt_memcmp(s_flash.cache + in_block,
                          source + done, chunk) != 0)
            {
                rt_memcpy(s_flash.cache + in_block, source + done, chunk);
                s_flash.cache_dirty = RT_TRUE;
            }
        }
        done += chunk;
    }
    rt_mutex_release(&s_flash.lock);
    return result == RT_EOK ? count : 0;
}

static rt_err_t flash_storage_control(rt_device_t device, int command,
                                      void *argument)
{
    int result = RT_EOK;
    RT_UNUSED(device);

    if (command == RT_DEVICE_CTRL_BLK_GETGEOME)
    {
        if (argument == RT_NULL) return -RT_EINVAL;
        rt_memcpy(argument, &s_flash.geometry, sizeof(s_flash.geometry));
    }
    else if (command == RT_DEVICE_CTRL_BLK_SYNC)
    {
        rt_mutex_take(&s_flash.lock, RT_WAITING_FOREVER);
        result = flash_storage_flush_locked();
        rt_mutex_release(&s_flash.lock);
    }
    else if (command == RT_DEVICE_CTRL_BLK_ERASE)
    {
        /* FatFs TRIM is advisory.  Actual erases are performed atomically by
         * the cached read/modify/write path, so ignoring TRIM is safer than
         * invalidating partially occupied NOR erase blocks. */
        result = RT_EOK;
    }
    return result;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops s_flash_ops =
{
    RT_NULL,
    flash_storage_open,
    flash_storage_close,
    flash_storage_read,
    flash_storage_write,
    flash_storage_control
};
#endif

int board_flash_storage_device_init(void)
{
    if (s_flash.initialized) return RT_EOK;
    rt_memset(&s_flash, 0, sizeof(s_flash));
    s_flash.partition = fal_partition_find(FLASH_STORAGE_PARTITION);
    if (s_flash.partition == RT_NULL ||
        s_flash.partition->len < 2U * 64U * 1024U)
        return -RT_ENOENT;
    s_flash.geometry.bytes_per_sector = FLASH_STORAGE_SECTOR_SIZE;
    s_flash.geometry.block_size = nor_flash0.blk_size;
    s_flash.geometry.sector_count =
        s_flash.partition->len / FLASH_STORAGE_SECTOR_SIZE;
    if (s_flash.geometry.block_size == 0U ||
        (s_flash.partition->len % s_flash.geometry.block_size) != 0U)
        return -RT_EINVAL;
    s_flash.cache = rt_malloc_align(s_flash.geometry.block_size, 32U);
    if (s_flash.cache == RT_NULL) return -RT_ENOMEM;
    rt_mutex_init(&s_flash.lock, "ft_flash", RT_IPC_FLAG_PRIO);
    s_flash.parent.type = RT_Device_Class_Block;
#ifdef RT_USING_DEVICE_OPS
    s_flash.parent.ops = &s_flash_ops;
#else
    s_flash.parent.init = RT_NULL;
    s_flash.parent.open = flash_storage_open;
    s_flash.parent.close = flash_storage_close;
    s_flash.parent.read = flash_storage_read;
    s_flash.parent.write = flash_storage_write;
    s_flash.parent.control = flash_storage_control;
#endif
    if (rt_device_register(&s_flash.parent, FLASH_STORAGE_DEVICE,
                           RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE) != RT_EOK)
    {
        rt_free_align(s_flash.cache);
        s_flash.cache = RT_NULL;
        return -RT_ERROR;
    }
    s_flash.initialized = RT_TRUE;
    rt_kprintf("FeatherTalk flash storage: %lu sectors x %u bytes, erase %u bytes\n",
               (unsigned long)s_flash.geometry.sector_count,
               (unsigned int)s_flash.geometry.bytes_per_sector,
               (unsigned int)s_flash.geometry.block_size);
    return RT_EOK;
}

int board_flash_storage_sync(void)
{
    if (!s_flash.initialized) return -RT_ENOSYS;
    return rt_device_control(&s_flash.parent, RT_DEVICE_CTRL_BLK_SYNC, RT_NULL);
}

#else

int board_flash_storage_device_init(void) { return -RT_ENOSYS; }
int board_flash_storage_sync(void) { return -RT_ENOSYS; }

#endif
