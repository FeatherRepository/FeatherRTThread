#include <rtthread.h>

#include <board.h>
#include <feathertalk/smif_guard.h>

#define FEATHERTALK_SMIF_READY_TIMEOUT_MS       2000U
#define FEATHERTALK_SMIF_PARK_TIMEOUT_MS        1000U
#define FEATHERTALK_SMIF_RELEASE_TIMEOUT_MS      100U

__attribute__((section(".feathertalk_xip_guard"), used, aligned(32)))
volatile feathertalk_smif_guard_shared_t g_feathertalk_smif_guard_shared;

static rt_bool_t g_smif_guard_initialized = RT_FALSE;
static rt_uint32_t g_smif_guard_sequence = 0U;
static rt_uint32_t g_smif_guard_active_sequence = 0U;

static volatile feathertalk_smif_guard_shared_t *feathertalk_smif_guard_shared(void)
{
    return &g_feathertalk_smif_guard_shared;
}

static void feathertalk_smif_guard_clean_m55_line(void)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr((volatile void *)&g_feathertalk_smif_guard_shared,
                           FEATHERTALK_SMIF_GUARD_CACHELINE);
#endif
    __DMB();
}

static void feathertalk_smif_guard_invalidate_m33_line(void)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr(
        (volatile void *)((uintptr_t)&g_feathertalk_smif_guard_shared +
                          FEATHERTALK_SMIF_GUARD_CACHELINE),
        FEATHERTALK_SMIF_GUARD_CACHELINE);
#endif
    __DMB();
}

static rt_bool_t feathertalk_smif_guard_wait_word(volatile uint32_t *word,
                                                  uint32_t expected,
                                                  uint32_t timeout_ms)
{
    uint32_t start = rt_tick_get_millisecond();

    while ((rt_tick_get_millisecond() - start) < timeout_ms)
    {
        feathertalk_smif_guard_invalidate_m33_line();
        if (*word == expected)
        {
            return RT_TRUE;
        }
        rt_thread_mdelay(1U);
    }

    feathertalk_smif_guard_invalidate_m33_line();
    return *word == expected;
}

int feathertalk_smif_guard_init(void)
{
    volatile feathertalk_smif_guard_shared_t *shared = feathertalk_smif_guard_shared();
    uint32_t epoch;

    if (g_smif_guard_initialized)
    {
        return RT_EOK;
    }

    /* Invalidate first because the NOLOAD block can contain a previous boot. */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr((volatile void *)shared,
                                FEATHERTALK_SMIF_GUARD_BYTES);
#endif
    epoch = shared->epoch + 1U;
    if (epoch == 0U)
    {
        epoch = 1U;
    }

    shared->magic = 0U;
    __DMB();
    shared->version = FEATHERTALK_SMIF_GUARD_VERSION;
    shared->epoch = epoch;
    shared->request_seq = 0U;
    shared->release_seq = 0U;
    shared->operation = FEATHERTALK_SMIF_OP_NONE;
    shared->address = 0U;
    shared->size = 0U;
    shared->magic = FEATHERTALK_SMIF_GUARD_MAGIC;
    feathertalk_smif_guard_clean_m55_line();
    __SEV();

    g_smif_guard_initialized = RT_TRUE;
    return RT_EOK;
}
INIT_COMPONENT_EXPORT(feathertalk_smif_guard_init);

int feathertalk_smif_guard_acquire(uint32_t operation,
                                  uint32_t address,
                                  uint32_t size)
{
    volatile feathertalk_smif_guard_shared_t *shared = feathertalk_smif_guard_shared();
    uint32_t epoch;
    uint32_t sequence;

    if (operation != FEATHERTALK_SMIF_OP_PROGRAM &&
        operation != FEATHERTALK_SMIF_OP_ERASE)
    {
        return -RT_EINVAL;
    }
    if (!g_smif_guard_initialized && feathertalk_smif_guard_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    epoch = shared->epoch;
    if (!feathertalk_smif_guard_wait_word(&shared->ready_epoch,
                                          epoch,
                                          FEATHERTALK_SMIF_READY_TIMEOUT_MS))
    {
        return -RT_ETIMEOUT;
    }

    /* Do not overlap a new request with M33's return from the previous one. */
    feathertalk_smif_guard_invalidate_m33_line();
    if (shared->parked_seq != 0U)
    {
        return -RT_EBUSY;
    }

    sequence = ++g_smif_guard_sequence;
    if (sequence == 0U)
    {
        sequence = ++g_smif_guard_sequence;
    }

    shared->operation = operation;
    shared->address = address;
    shared->size = size;
    shared->request_seq = sequence;
    feathertalk_smif_guard_clean_m55_line();
    __SEV();

    if (!feathertalk_smif_guard_wait_word(&shared->parked_seq,
                                          sequence,
                                          FEATHERTALK_SMIF_PARK_TIMEOUT_MS))
    {
        /* Cancel before returning so a late M33 poll cannot park forever. */
        shared->release_seq = sequence;
        feathertalk_smif_guard_clean_m55_line();
        __SEV();
        return -RT_ETIMEOUT;
    }

    g_smif_guard_active_sequence = sequence;
    return RT_EOK;
}

void feathertalk_smif_guard_release(int result)
{
    volatile feathertalk_smif_guard_shared_t *shared = feathertalk_smif_guard_shared();
    uint32_t sequence = g_smif_guard_active_sequence;

    (void)result;
    if (sequence == 0U)
    {
        return;
    }

    shared->release_seq = sequence;
    feathertalk_smif_guard_clean_m55_line();
    __SEV();
    (void)feathertalk_smif_guard_wait_word(&shared->completed_seq,
                                           sequence,
                                           FEATHERTALK_SMIF_RELEASE_TIMEOUT_MS);
    g_smif_guard_active_sequence = 0U;
}
