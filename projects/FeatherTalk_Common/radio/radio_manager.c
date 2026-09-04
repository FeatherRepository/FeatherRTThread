#include <board.h>
#include <feathertalk/radio_manager.h>
#include <string.h>

/* Hardware facts live here, not in Wi-Fi/Bluetooth services. UART, SDIO,
 * firmware, and protocol state machines remain owned by their own drivers. */
static const struct {
    ft_radio_core_t owner;
    uint8_t port, pin;
    uint32_t drive;
} radios[FT_RADIO_COUNT] = {
    {FT_RADIO_CORE_M55, 11, 6, CY_GPIO_DM_STRONG},
    {FT_RADIO_CORE_M33, 11, 0, CY_GPIO_DM_PULLUP}
};
static ft_radio_core_t local_core;
#define SHARED ((volatile ft_radio_shared_t *)FT_RADIO_SHARED_ADDRESS)

static void publish(volatile void *line)
{
    __DMB();
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1)
    SCB_CleanDCache_by_Addr((uint32_t *)line, 32);
#else
    (void)line;
#endif
    __DSB();
}
static void refresh(volatile void *line)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1)
    SCB_InvalidateDCache_by_Addr((uint32_t *)line, 32);
#else
    (void)line;
#endif
    __DMB();
}
static int valid(ft_radio_id_t id) { return (unsigned)id < FT_RADIO_COUNT; }

void ft_radio_board_boot(void)
{
    /* Called by board boot, never by either radio's start/recovery path.
     * M55 has not been released yet; no other writer exists at this point. */
    for (unsigned id = 0; id < FT_RADIO_COUNT; id++) {
        Cy_GPIO_Pin_FastInit(Cy_GPIO_PortToAddr(radios[id].port), radios[id].pin,
                            radios[id].drive, 0, HSIOM_SEL_GPIO);
    }
    Cy_GPIO_Pin_FastInit(GPIO_PRT7, 2, CY_GPIO_DM_STRONG, 1, HSIOM_SEL_GPIO);
    Cy_SysLib_Delay(200);
    Cy_GPIO_Pin_FastInit(GPIO_PRT16, 3, CY_GPIO_DM_STRONG, 1, HSIOM_SEL_GPIO);
    Cy_SysLib_Delay(20);
    memset((void *)SHARED, 0, sizeof(*SHARED));
    SHARED->version = 1;
    SHARED->supply_on = 1;
    /* Both domains may retain RAM/clock dependencies. Until combo-chip
     * deep-power-down is validated, even zero clients retain the supply.
     * Logical Wi-Fi OFF is WLC_DOWN, not an uncoordinated power cut. */
    SHARED->retain_supply = 1;
    for (unsigned id = 0; id < FT_RADIO_COUNT; id++) {
        SHARED->radio[id].owner = radios[id].owner;
        publish(&SHARED->radio[id]);
    }
    __DMB();
    SHARED->magic = FT_RADIO_SHARED_MAGIC;
    publish(SHARED);
}

void ft_radio_attach(ft_radio_core_t core) { local_core = core; }
int ft_radio_reset_pin(ft_radio_id_t id)
{
    return valid(id) ? (int)GET_PIN(radios[id].port, radios[id].pin) : -1;
}
int ft_radio_owned_here(ft_radio_id_t id)
{
    return valid(id) && radios[id].owner == local_core;
}
static int permitted(ft_radio_id_t id)
{
    if (!ft_radio_owned_here(id)) return -RT_EINVAL;
    refresh(SHARED);
    if (SHARED->magic != FT_RADIO_SHARED_MAGIC || SHARED->version != 1 ||
        !SHARED->supply_on || SHARED->shutting_down) return -RT_EBUSY;
    return RT_EOK;
}

static void drive_reset(ft_radio_id_t id, uint32_t level)
{
    /* P11.0 and P11.6 share a GPIO port. Pin mux/drive-mode RMW is done only
     * at single-core boot; runtime OUT_SET/OUT_CLR writes are atomic so two
     * independent resets cannot overwrite the other pin's configuration. */
    Cy_GPIO_Write(Cy_GPIO_PortToAddr(radios[id].port), radios[id].pin, level);
}

int ft_radio_get_status(ft_radio_id_t id, ft_radio_status_t *status)
{
    if (!valid(id) || !status) return -RT_EINVAL;
    volatile ft_radio_status_t *slot = &SHARED->radio[id];
    for (unsigned retry = 0; retry < 4; retry++) {
        /* Only invalidate a PEER-owned line. Never discard our dirty writes. */
        if (!ft_radio_owned_here(id)) refresh(slot);
        uint32_t before = slot->sequence;
        if (before & 1U) continue;
        memcpy(status, (const void *)slot, sizeof(*status));
        __DMB();
        if (!ft_radio_owned_here(id)) refresh(slot);
        if (before == slot->sequence) return RT_EOK;
    }
    return -RT_EBUSY;
}

/* Single-writer slots + short local critical sections avoid a cross-core
 * lock around GPIO settling delays, and cannot hold up the other radio. */
static void begin(volatile ft_radio_status_t *slot) { slot->sequence++; __DMB(); }
static void end(volatile ft_radio_status_t *slot) { __DMB(); slot->sequence++; publish(slot); }

int ft_radio_acquire(ft_radio_id_t id)
{
    int result = permitted(id);
    if (result != RT_EOK) return result;
    rt_base_t level = rt_hw_interrupt_disable();
    volatile ft_radio_status_t *slot = &SHARED->radio[id];
    if (!slot->claimed) {
        begin(slot);
        slot->claimed = 1; /* one idempotent lease per independently owned domain */
        slot->state = FT_RADIO_POWERED;
        slot->error = 0;
        end(slot);
    }
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

int ft_radio_set_state(ft_radio_id_t id, ft_radio_state_t state, int error)
{
    int result = permitted(id);
    if (result != RT_EOK) return result;
    if (state != FT_RADIO_READY && state != FT_RADIO_QUIESCED && state != FT_RADIO_ERROR)
        return -RT_EINVAL;
    rt_base_t level = rt_hw_interrupt_disable();
    volatile ft_radio_status_t *slot = &SHARED->radio[id];
    if ((!slot->claimed && state != FT_RADIO_ERROR) || slot->state == FT_RADIO_RESETTING)
        result = -RT_EBUSY;
    else {
        begin(slot); slot->state = state; slot->error = error; end(slot);
    }
    rt_hw_interrupt_enable(level);
    return result;
}

int ft_radio_reset(ft_radio_id_t id, uint32_t low_ms, uint32_t settle_ms)
{
    int result = permitted(id);
    if (result != RT_EOK) return result;
    if (!low_ms || low_ms > 1000 || settle_ms > 1000) return -RT_EINVAL;
    rt_base_t level = rt_hw_interrupt_disable();
    volatile ft_radio_status_t *slot = &SHARED->radio[id];
    if (!slot->claimed || slot->state == FT_RADIO_RESETTING) result = -RT_EBUSY;
    else {
        begin(slot); slot->state = FT_RADIO_RESETTING; slot->resets++; end(slot);
    }
    rt_hw_interrupt_enable(level);
    if (result != RT_EOK) return result;
    drive_reset(id, 0);
    rt_thread_mdelay(low_ms);
    if (permitted(id) != RT_EOK) return -RT_EBUSY;
    drive_reset(id, 1);
    rt_thread_mdelay(settle_ms);
    level = rt_hw_interrupt_disable();
    begin(slot); slot->state = FT_RADIO_POWERED; slot->error = 0; end(slot);
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

int ft_radio_release(ft_radio_id_t id)
{
    int result = permitted(id);
    if (result != RT_EOK) return result;
    rt_base_t level = rt_hw_interrupt_disable();
    volatile ft_radio_status_t *slot = &SHARED->radio[id];
    if (slot->state == FT_RADIO_RESETTING) result = -RT_EBUSY;
    else {
        drive_reset(id, 0); /* Only this domain, never P16.3 or P7.2. */
        begin(slot); slot->claimed = 0; slot->state = FT_RADIO_OFF; slot->error = 0; end(slot);
    }
    rt_hw_interrupt_enable(level);
    return result;
}

void ft_radio_system_poweroff(void)
{
    /* Global shutdown intentionally terminates BOTH domains. No runtime
     * radio service is given this operation through its control API. */
    refresh(SHARED);
    SHARED->shutting_down = 1;
    SHARED->supply_on = 0;
    publish(SHARED); /* terminal shutdown is the sole header-writer exception */
    drive_reset(FT_RADIO_WIFI, 0);
    drive_reset(FT_RADIO_BT, 0);
    Cy_GPIO_Pin_FastInit(GPIO_PRT16, 3, CY_GPIO_DM_STRONG, 0, HSIOM_SEL_GPIO);
    Cy_GPIO_Pin_FastInit(GPIO_PRT7, 2, CY_GPIO_DM_STRONG, 0, HSIOM_SEL_GPIO);
}

static void ft_radio(int argc, char **argv)
{
    (void)argc; (void)argv;
    refresh(SHARED);
    rt_kprintf("[radio] version=%u supply=%u retained=%u local=M%u\n",
               SHARED->version, SHARED->supply_on, SHARED->retain_supply, local_core);
    for (unsigned id = 0; id < FT_RADIO_COUNT; id++) {
        ft_radio_status_t state;
        if (ft_radio_get_status((ft_radio_id_t)id, &state) == RT_EOK)
            rt_kprintf("[radio] %s owner=M%u claimed=%u state=%u resets=%u error=%d seq=%u\n",
                       id == FT_RADIO_WIFI ? "wifi" : "bt", state.owner, state.claimed,
                       state.state, state.resets, state.error, state.sequence);
    }
}
MSH_CMD_EXPORT(ft_radio, Read shared radio ownership and independent runtime states);

static void ft_radio_test(void)
{
    ft_radio_id_t own = local_core == FT_RADIO_CORE_M55 ? FT_RADIO_WIFI : FT_RADIO_BT;
    ft_radio_id_t peer = own == FT_RADIO_WIFI ? FT_RADIO_BT : FT_RADIO_WIFI;
    ft_radio_status_t before[2] = {0}, after[2] = {0};
    unsigned checks = 0, failures = 0;
#define CHECK(test) do { checks++; if (!(test)) failures++; } while (0)
    CHECK(ft_radio_get_status(own, &before[own]) == RT_EOK);
    CHECK(ft_radio_get_status(peer, &before[peer]) == RT_EOK);
    /* These must fail BEFORE any GPIO or shared-state write. */
    CHECK(ft_radio_acquire(peer) == -RT_EINVAL);
    CHECK(ft_radio_reset(peer, 2, 10) == -RT_EINVAL);
    CHECK(ft_radio_release(peer) == -RT_EINVAL);
    CHECK(ft_radio_set_state(peer, FT_RADIO_ERROR, -99) == -RT_EINVAL);
    CHECK(ft_radio_reset(own, 0, 10) == -RT_EINVAL);
    CHECK(ft_radio_set_state(own, FT_RADIO_RESETTING, 0) == -RT_EINVAL);
    CHECK(ft_radio_acquire(FT_RADIO_COUNT) == -RT_EINVAL);
    CHECK(ft_radio_get_status(own, &after[own]) == RT_EOK);
    CHECK(ft_radio_get_status(peer, &after[peer]) == RT_EOK);
    CHECK(memcmp(before, after, sizeof(before)) == 0);
    rt_kprintf("[RADIO-TEST] checks=%u failures=%u (no GPIO writes)\n", checks, failures);
#undef CHECK
}
MSH_CMD_EXPORT(ft_radio_test, Reject cross-owner and invalid radio controls without GPIO writes);
