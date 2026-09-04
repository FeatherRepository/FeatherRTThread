#include <rtthread.h>
#include <feathertalk/radio_manager.h>

/* WHD's runtime platform binding. This board owns WLAN on M55 and Bluetooth
 * on M33, regardless of whether either independent host is built/enabled. */
int whd_platform_prepare_radio(rt_base_t configured_reset_pin)
{
    int result;
    if (configured_reset_pin != ft_radio_reset_pin(FT_RADIO_WIFI)) return -RT_EINVAL;
    result = ft_radio_acquire(FT_RADIO_WIFI);
    if (result == RT_EOK) result = ft_radio_reset(FT_RADIO_WIFI, 2, 10);
    return result;
}
rt_bool_t whd_platform_same_core_bt(void)
{
    return ft_radio_owned_here(FT_RADIO_BT) ? RT_TRUE : RT_FALSE;
}
void whd_platform_radio_result(int result)
{
    ft_radio_set_state(FT_RADIO_WIFI, result == RT_EOK ? FT_RADIO_READY : FT_RADIO_ERROR, result);
}
