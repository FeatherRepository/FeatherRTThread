#ifndef FEATHERTALK_BT_SOURCE_ROUTE_H
#define FEATHERTALK_BT_SOURCE_ROUTE_H

#include <stdint.h>

/* A2DP Source input selection.  The encoder reads exactly one tap:
 * USB host audio or the local media player.  Both run on M55. */
typedef enum
{
    FT_BT_SOURCE_INPUT_USB = 0,
    FT_BT_SOURCE_INPUT_PLAYER = 1
} ft_bt_source_input_t;

void ft_bt_source_input_set(ft_bt_source_input_t input);
ft_bt_source_input_t ft_bt_source_input_get(void);
const char *ft_bt_source_input_name(ft_bt_source_input_t input);

#endif /* FEATHERTALK_BT_SOURCE_ROUTE_H */
