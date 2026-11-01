#ifndef FEATHERTALK_PLAYER_BT_TAP_H
#define FEATHERTALK_PLAYER_BT_TAP_H

#include <stdbool.h>
#include <stdint.h>

/* Local player PCM -> A2DP Source SBC encoder.
 * Producer is the player worker; consumer is ft_senc.  Both run on M55, so
 * this ring only needs IRQ-safe head/tail updates, not cross-core cache work. */
void ft_player_bt_tap_set_active(bool active);
bool ft_player_bt_tap_active(void);
void ft_player_bt_tap_reset(void);
uint32_t ft_player_bt_tap_write(const uint8_t *data, uint32_t length);
uint32_t ft_player_bt_tap_read(uint8_t *data, uint32_t capacity);

#endif
