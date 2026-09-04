#ifndef WHD_RADIO_PLATFORM_H
#define WHD_RADIO_PLATFORM_H
#include <rtthread.h>
/* Runtime board hooks; defaults preserve the standalone SDK demo behavior. */
int whd_platform_prepare_radio(rt_base_t configured_reset_pin);
int whd_bsp_radio_result(void);
rt_bool_t whd_platform_same_core_bt(void);
void whd_platform_radio_result(int result);
#endif
