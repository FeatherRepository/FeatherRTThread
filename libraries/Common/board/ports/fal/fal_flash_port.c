/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-01-26     armink       the first version
 */

#include <fal.h>
#include "cycfg_qspi_memslot.h"
#include <string.h>
#include "cy_smif.h"

#define LOG_TAG                "drv.fal_flash"
#include <drv_log.h>

/* Objects for serial memory */
static cy_stc_smif_context_t smif_context;

#define smifMemConfigs smif0MemConfigs
#define MEM_SLOT_NUM                     (0U)
#ifndef FAL_USING_NOR_FLASH_DEV_NAME
#define FAL_USING_NOR_FLASH_DEV_NAME             "norflash0"
#endif

/* Flash device configuration */
#define SMIF_BASE_ADDRESS      0x60000000
#define FLASH_START_ADDRESS    0x60E00000
#define FLASH_SIZE             (2 * 1024 * 1024) /* 2MB */
#define FLASH_SECTOR_SIZE      0x10000 /* 64KB sectors */
#define FLASH_END_ADDRESS      (FLASH_START_ADDRESS + FLASH_SIZE)
#define FLASH_PROGRAM_SIZE     0x100 /* S25FS128S page size */
#define FLASH_READY_POLL_US    50U

#define FEATHERTALK_SMIF_OP_PROGRAM 1U
#define FEATHERTALK_SMIF_OP_ERASE   2U

/* FeatherTalk_M55 supplies strong cross-core hooks.  Native single-core demos
 * retain the SDK behavior through these weak defaults. */
rt_weak int feathertalk_smif_guard_init(void)
{
    return RT_EOK;
}

rt_weak int feathertalk_smif_guard_acquire(uint32_t operation,
                                          uint32_t address,
                                          uint32_t size)
{
    (void)operation;
    (void)address;
    (void)size;
    return RT_EOK;
}

rt_weak void feathertalk_smif_guard_release(int result)
{
    (void)result;
}

static struct rt_mutex smif_lock;

static int init(void);
static int read(long offset, uint8_t *buf, size_t size);
static int write(long offset, const uint8_t *buf, size_t size);
static int erase(long offset, size_t size);

static void smif_invalidate_local_caches_ram(uint32_t address, size_t size)
    __attribute__((section(".cy_sram_code"), noinline));
static void smif_wait_ready_fail_closed_ram(void)
    __attribute__((section(".cy_sram_code"), noinline));
static cy_en_smif_status_t smif_program_page_ram(uint32_t device_offset,
                                                 const uint8_t *buffer,
                                                 size_t size)
    __attribute__((section(".cy_sram_code"), noinline));
static cy_en_smif_status_t smif_erase_sector_ram(uint32_t device_offset,
                                                 size_t size)
    __attribute__((section(".cy_sram_code"), noinline));

struct rt_device *flash_dev;
struct fal_flash_dev nor_flash0 =
{
    .name       = FAL_USING_NOR_FLASH_DEV_NAME,
    .addr       = FLASH_START_ADDRESS,
    .len        = FLASH_SIZE,
    .blk_size   = FLASH_SECTOR_SIZE,
    .ops        = {init, read, write, erase},
    .write_gran = 1
};

static int init(void)
{
    rt_size_t erase_size = FLASH_SECTOR_SIZE;

    if (smifMemConfigs[MEM_SLOT_NUM] && smifMemConfigs[MEM_SLOT_NUM]->deviceCfg)
    {
        erase_size = smifMemConfigs[MEM_SLOT_NUM]->deviceCfg->eraseSize;
    }

    if (erase_size == 0)
    {
        erase_size = FLASH_SECTOR_SIZE;
    }

    nor_flash0.blk_size = erase_size;
    rt_mutex_init(&smif_lock, "smif", RT_IPC_FLAG_PRIO);

    /* Force the PDL ready loop onto its microsecond busy-wait path.  Its
     * zero-delay path may call an RTOS delay implementation from XIP when
     * XIP_MODE is unexpectedly clear, which is forbidden while M33 is
     * parked and the shared flash is busy. */
    Cy_SMIF_SetReadyPollingDelay(FLASH_READY_POLL_US, &smif_context);

    if (feathertalk_smif_guard_init() != RT_EOK)
    {
        LOG_E("cross-core SMIF guard initialization failed");
        return -RT_ERROR;
    }

    /* SMIF already initialized by cybsp_init() */
    LOG_I("FAL flash initialized, erase size=%u", (unsigned int)nor_flash0.blk_size);
    return 0;
}

static int read(long offset, uint8_t *buf, size_t size)
{
    if (offset + size > FLASH_SIZE)
    {
        LOG_E("read out of range! offset=%ld, size=%d", offset, size);
        return -RT_EINVAL;
    }

    LOG_D("FAL read: offset %#lx, size %d", offset, size);
    rt_mutex_take(&smif_lock, RT_WAITING_FOREVER);
    /*
     * Both application cores execute from this SMIF0 device.  Issuing an
     * MMIO read command temporarily takes the device out of its XIP read
     * path and can corrupt instruction fetches performed by the other core.
     * Read the reserved filesystem through the already configured XIP window
     * instead; the mutex still serializes this access with local program and
     * erase operations.
     */
    memcpy(buf, (const void *)(uintptr_t)(FLASH_START_ADDRESS + offset), size);
    rt_mutex_release(&smif_lock);

    return size;
}

static void smif_invalidate_local_caches_ram(uint32_t address, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    {
        const uintptr_t line_size = (uintptr_t)__SCB_DCACHE_LINE_SIZE;
        const uintptr_t start = (uintptr_t)address & ~(line_size - 1U);
        const uintptr_t end = ((uintptr_t)address + size + line_size - 1U) &
                              ~(line_size - 1U);
        SCB_InvalidateDCache_by_Addr((volatile void *)start,
                                    (int32_t)(end - start));
    }
#else
    (void)address;
    (void)size;
#endif

#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    SCB_InvalidateICache();
#endif
    __DSB();
    __ISB();
}

static void smif_wait_ready_fail_closed_ram(void)
{
    /* A PDL timeout means the device may still be programming/erasing.  Never
     * release M33 back to XIP until a status-register read has positively
     * observed WIP clear.  Cy_SMIF_MemIsBusy deliberately reports read errors
     * as busy, so a broken controller/device remains parked instead of
     * returning into poisoned external code; the platform watchdog can then
     * provide the eventual whole-device recovery policy. */
    while (Cy_SMIF_MemIsBusy(SMIF0_CORE,
                             smifMemConfigs[MEM_SLOT_NUM],
                             &smif_context))
    {
        Cy_SysLib_DelayUs(FLASH_READY_POLL_US);
    }
    __DSB();
}

static cy_en_smif_status_t smif_program_page_ram(uint32_t device_offset,
                                                 const uint8_t *buffer,
                                                 size_t size)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t faultmask = __get_FAULTMASK();
    cy_en_smif_status_t result;

    __disable_irq();
    __disable_fault_irq();
    __DSB();
    __ISB();
    smif_context.memReadyPollDelay = FLASH_READY_POLL_US;
    result = Cy_SMIF_MemWrite(SMIF0_CORE,
                              smifMemConfigs[MEM_SLOT_NUM],
                              device_offset,
                              buffer,
                              size,
                              &smif_context);
    smif_wait_ready_fail_closed_ram();
    smif_invalidate_local_caches_ram(SMIF_BASE_ADDRESS + device_offset, size);
    __set_FAULTMASK(faultmask);
    __set_PRIMASK(primask);
    return result;
}

static cy_en_smif_status_t smif_erase_sector_ram(uint32_t device_offset,
                                                 size_t size)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t faultmask = __get_FAULTMASK();
    cy_en_smif_status_t result;

    __disable_irq();
    __disable_fault_irq();
    __DSB();
    __ISB();
    smif_context.memReadyPollDelay = FLASH_READY_POLL_US;
    result = Cy_SMIF_MemEraseSector(SMIF0_CORE,
                                    smifMemConfigs[MEM_SLOT_NUM],
                                    device_offset,
                                    size,
                                    &smif_context);
    smif_wait_ready_fail_closed_ram();
    smif_invalidate_local_caches_ram(SMIF_BASE_ADDRESS + device_offset, size);
    __set_FAULTMASK(faultmask);
    __set_PRIMASK(primask);
    return result;
}

static int write(long offset, const uint8_t *buf, size_t size)
{
    uint8_t page_buffer[FLASH_PROGRAM_SIZE];
    size_t remaining = size;
    size_t written = 0U;
    cy_en_smif_status_t result = CY_SMIF_SUCCESS;

    if (!buf || size == 0)
    {
        LOG_E("Invalid input: buf=%p, size=%d", buf, size);
        return -RT_EINVAL;
    }

    if (offset + size > FLASH_SIZE)
    {
        LOG_E("write out of range! offset=%ld, size=%d", offset, size);
        return -RT_EINVAL;
    }

    LOG_D("FAL write: offset %#lx, size %d", offset, size);
    rt_mutex_take(&smif_lock, RT_WAITING_FOREVER);

    while (remaining != 0U)
    {
        uint32_t device_offset = (uint32_t)offset +
                                 (FLASH_START_ADDRESS - SMIF_BASE_ADDRESS) +
                                 (uint32_t)written;
        size_t page_remaining = FLASH_PROGRAM_SIZE -
                                (device_offset % FLASH_PROGRAM_SIZE);
        size_t chunk = remaining < page_remaining ? remaining : page_remaining;
        int guard_result;

        /* The caller is allowed to pass an XIP-backed source.  Copy it while
         * the flash is still in memory mode, before either core is parked. */
        memcpy(page_buffer, buf + written, chunk);
        guard_result = feathertalk_smif_guard_acquire(
            FEATHERTALK_SMIF_OP_PROGRAM,
            SMIF_BASE_ADDRESS + device_offset,
            (uint32_t)chunk);
        if (guard_result != RT_EOK)
        {
            LOG_E("M33 XIP park failed before write: %d", guard_result);
            result = CY_SMIF_EXCEED_TIMEOUT;
            break;
        }

        result = smif_program_page_ram(device_offset, page_buffer, chunk);
        feathertalk_smif_guard_release((int)result);
        if (result != CY_SMIF_SUCCESS)
        {
            break;
        }

        written += chunk;
        remaining -= chunk;
    }

    rt_mutex_release(&smif_lock);
    if (result != CY_SMIF_SUCCESS)
    {
        LOG_E("Cy_SMIF_MemWrite failed: %d", result);
        return -RT_ERROR;
    }

    return size;
}

static int erase(long offset, size_t size)
{
    size_t erased = 0U;
    cy_en_smif_status_t result = CY_SMIF_SUCCESS;

    if ((offset % (long)nor_flash0.blk_size) != 0 || (size % nor_flash0.blk_size) != 0)
    {
        LOG_E("erase unaligned! offset=%ld, size=%u, blk=%u", offset, (unsigned int)size, (unsigned int)nor_flash0.blk_size);
        return -RT_EINVAL;
    }

    if (offset + size > FLASH_SIZE)
    {
        LOG_E("erase out of range! offset=%ld, size=%d", offset, size);
        return -RT_EINVAL;
    }

    LOG_D("FAL erase: offset %#lx, size %d", offset, size);
    rt_mutex_take(&smif_lock, RT_WAITING_FOREVER);

    while (erased < size)
    {
        uint32_t device_offset = (uint32_t)offset +
                                 (FLASH_START_ADDRESS - SMIF_BASE_ADDRESS) +
                                 (uint32_t)erased;
        size_t chunk = nor_flash0.blk_size;
        int guard_result = feathertalk_smif_guard_acquire(
            FEATHERTALK_SMIF_OP_ERASE,
            SMIF_BASE_ADDRESS + device_offset,
            (uint32_t)chunk);

        if (guard_result != RT_EOK)
        {
            LOG_E("M33 XIP park failed before erase: %d", guard_result);
            result = CY_SMIF_EXCEED_TIMEOUT;
            break;
        }

        result = smif_erase_sector_ram(device_offset, chunk);
        feathertalk_smif_guard_release((int)result);
        if (result != CY_SMIF_SUCCESS)
        {
            break;
        }
        erased += chunk;
    }

    rt_mutex_release(&smif_lock);
    if (result != CY_SMIF_SUCCESS)
    {
        LOG_E("Cy_SMIF_MemEraseSector failed: %d", result);
        return -RT_ERROR;
    }
    return size;
}
