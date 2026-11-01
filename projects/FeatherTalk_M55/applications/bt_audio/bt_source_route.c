#include <rtthread.h>

#include "bt_source_route.h"

static volatile ft_bt_source_input_t s_input = FT_BT_SOURCE_INPUT_USB;

void ft_bt_source_input_set(ft_bt_source_input_t input)
{
    if (input != FT_BT_SOURCE_INPUT_USB &&
        input != FT_BT_SOURCE_INPUT_PLAYER)
        return;
    s_input = input;
}

ft_bt_source_input_t ft_bt_source_input_get(void)
{
    return s_input;
}

const char *ft_bt_source_input_name(ft_bt_source_input_t input)
{
    return input == FT_BT_SOURCE_INPUT_PLAYER ? "player" : "usb";
}
