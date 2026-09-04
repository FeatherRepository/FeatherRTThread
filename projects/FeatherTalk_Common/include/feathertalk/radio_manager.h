#ifndef FEATHERTALK_RADIO_MANAGER_H
#define FEATHERTALK_RADIO_MANAGER_H
#include <stdint.h>

/* Runtime board-resource contract, independent of either host-stack build. */
typedef enum { FT_RADIO_WIFI, FT_RADIO_BT, FT_RADIO_COUNT } ft_radio_id_t;
typedef enum { FT_RADIO_CORE_M33 = 33, FT_RADIO_CORE_M55 = 55 } ft_radio_core_t;
typedef enum {
    FT_RADIO_OFF, FT_RADIO_POWERED, FT_RADIO_RESETTING,
    FT_RADIO_READY, FT_RADIO_QUIESCED, FT_RADIO_ERROR
} ft_radio_state_t;
typedef struct {
    uint32_t sequence, owner, claimed, state;
    uint32_t resets;
    int32_t error;
    uint32_t reserved[2];
} ft_radio_status_t;
typedef struct {
    uint32_t magic, version, supply_on, retain_supply;
    uint32_t shutting_down, reserved[3];
    ft_radio_status_t radio[FT_RADIO_COUNT];
    uint32_t padding[8];
} ft_radio_shared_t;

/* Reserved in BOTH product linker scripts, immediately before XIP guard.
 * One writer per cache line: M33 board header/BT, M55 WLAN. */
#define FT_RADIO_SHARED_ADDRESS 0x240FFF40UL
#define FT_RADIO_SHARED_BYTES 128U
#define FT_RADIO_SHARED_MAGIC 0x46545244UL
_Static_assert(sizeof(ft_radio_status_t) == 32, "radio slot must be one cache line");
_Static_assert(sizeof(ft_radio_shared_t) == FT_RADIO_SHARED_BYTES, "radio shared ABI");

/* First-stage M33 board boot, BEFORE releasing M55, irrespective of BT support. */
void ft_radio_board_boot(void);
void ft_radio_attach(ft_radio_core_t core);
int ft_radio_acquire(ft_radio_id_t id);
int ft_radio_reset(ft_radio_id_t id, uint32_t low_ms, uint32_t settle_ms);
int ft_radio_release(ft_radio_id_t id);
int ft_radio_set_state(ft_radio_id_t id, ft_radio_state_t state, int error);
int ft_radio_get_status(ft_radio_id_t id, ft_radio_status_t *status);
int ft_radio_owned_here(ft_radio_id_t id);
int ft_radio_reset_pin(ft_radio_id_t id);
/* Terminal, whole-device shutdown only. Never called by a radio off/error path. */
void ft_radio_system_poweroff(void);
#endif
