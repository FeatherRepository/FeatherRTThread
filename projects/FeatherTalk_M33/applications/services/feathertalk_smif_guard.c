#include <rtthread.h>

#include <board.h>
#include <feathertalk/smif_guard.h>

#define FEATHERTALK_SMIF_GUARD_THREAD_STACK   1024U
#define FEATHERTALK_SMIF_GUARD_THREAD_PRIO    3U
#define FEATHERTALK_SMIF_GUARD_POLL_TICKS     1U

static rt_thread_t g_smif_guard_thread = RT_NULL;

static volatile feathertalk_smif_guard_shared_t *feathertalk_smif_guard_shared(void)
{
    return (volatile feathertalk_smif_guard_shared_t *)
           (uintptr_t)FEATHERTALK_SMIF_GUARD_M33_ADDR;
}

__attribute__((section(".cy_sram_code"), noinline))
static void feathertalk_smif_guard_invalidate_xip_cache_ram(void)
{
    /* PSE84's M33 XIP path uses the external ICACHE0 block; the Cortex-M33
     * CMSIS SCB cache helpers are not applicable (__ICACHE_PRESENT == 0).
     * CMD.INV invalidates the cache, prefetch buffer and LRU state.  Wait for
     * any previous cache command before issuing it and for its self-clearing
     * completion before returning from SRAM to external code. */
    const uint32_t busy_mask = ICACHE_CMD_INV_Msk | ICACHE_CMD_BUFF_INV_Msk;

    __DSB();
    while ((ICACHE0->CMD & busy_mask) != 0U)
    {
        __NOP();
    }

    ICACHE0->CMD = ICACHE_CMD_INV_Msk;
    __DSB();
    while ((ICACHE0->CMD & ICACHE_CMD_INV_Msk) != 0U)
    {
        __NOP();
    }
    __DSB();
    __ISB();
}

/*
 * The acknowledgement is written only after this function is executing from
 * internal SRAM and all maskable/fault exceptions have been stopped.  From
 * parked_seq until release_seq no instruction or data access may leave SRAM.
 */
__attribute__((section(".cy_sram_code"), noinline))
static void feathertalk_smif_guard_park_ram(uint32_t epoch, uint32_t sequence)
{
    volatile feathertalk_smif_guard_shared_t *shared =
        (volatile feathertalk_smif_guard_shared_t *)(uintptr_t)FEATHERTALK_SMIF_GUARD_M33_ADDR;
    uint32_t primask = __get_PRIMASK();
    uint32_t faultmask = __get_FAULTMASK();

    __disable_irq();
    __disable_fault_irq();
    __DSB();
    __ISB();

    /* Recheck after entering SRAM so a timed-out request can never be acked. */
    if (shared->magic == FEATHERTALK_SMIF_GUARD_MAGIC &&
        shared->version == FEATHERTALK_SMIF_GUARD_VERSION &&
        shared->epoch == epoch &&
        shared->request_seq == sequence &&
        shared->release_seq != sequence)
    {
        shared->parked_seq = sequence;
        __DMB();
        __SEV();

        while (shared->release_seq != sequence)
        {
            /* Escape hatch: if M55 rebooted or re-initialized the guard while
             * M33 is parked, the request belongs to a dead epoch and release
             * will never come (re-init zeroes release_seq).  Abandon the park
             * instead of spinning forever with all interrupts disabled. */
            if ((shared->epoch != epoch) ||
                (shared->magic != FEATHERTALK_SMIF_GUARD_MAGIC) ||
                (shared->version != FEATHERTALK_SMIF_GUARD_VERSION))
            {
                feathertalk_smif_guard_invalidate_xip_cache_ram();
                __DMB();
                shared->parked_seq = 0U;
                shared->rejected_seq = sequence;
                __DMB();
                __SEV();
                break;
            }
            /* Volatile shared-SRAM load only.  WFE is deliberately avoided:
             * a missed event must not strand M33 while M55 waits. */
            __NOP();
        }

        if (shared->release_seq == sequence)
        {
            feathertalk_smif_guard_invalidate_xip_cache_ram();
            __DMB();
            shared->completed_seq = sequence;
            shared->parked_seq = 0U;
            __DMB();
            __SEV();
        }
    }
    else
    {
        shared->rejected_seq = sequence;
        __DMB();
    }

    __DSB();
    __ISB();
    __set_FAULTMASK(faultmask);
    __set_PRIMASK(primask);
}

static void feathertalk_smif_guard_thread_entry(void *parameter)
{
    volatile feathertalk_smif_guard_shared_t *shared = feathertalk_smif_guard_shared();
    uint32_t active_epoch = 0U;

    (void)parameter;

    while (1)
    {
        uint32_t magic = shared->magic;
        uint32_t version = shared->version;
        uint32_t epoch = shared->epoch;

        __DMB();
        if (magic == FEATHERTALK_SMIF_GUARD_MAGIC &&
            version == FEATHERTALK_SMIF_GUARD_VERSION &&
            epoch != 0U)
        {
            uint32_t request;
            uint32_t release;

            if (active_epoch != epoch)
            {
                shared->parked_seq = 0U;
                shared->completed_seq = 0U;
                shared->rejected_seq = 0U;
                shared->ready_epoch = epoch;
                __DMB();
                __SEV();
                active_epoch = epoch;
            }

            request = shared->request_seq;
            release = shared->release_seq;
            __DMB();
            if (request != 0U && request != release &&
                shared->parked_seq != request &&
                shared->completed_seq != request)
            {
                feathertalk_smif_guard_park_ram(epoch, request);
            }
        }
        else
        {
            active_epoch = 0U;
        }

        rt_thread_delay(FEATHERTALK_SMIF_GUARD_POLL_TICKS);
    }
}

int feathertalk_smif_guard_service_start(void)
{
    if (g_smif_guard_thread != RT_NULL)
    {
        return RT_EOK;
    }

    g_smif_guard_thread = rt_thread_create("ft_xip",
                                           feathertalk_smif_guard_thread_entry,
                                           RT_NULL,
                                           FEATHERTALK_SMIF_GUARD_THREAD_STACK,
                                           FEATHERTALK_SMIF_GUARD_THREAD_PRIO,
                                           1U);
    if (g_smif_guard_thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    rt_thread_startup(g_smif_guard_thread);
    return RT_EOK;
}
INIT_ENV_EXPORT(feathertalk_smif_guard_service_start);
