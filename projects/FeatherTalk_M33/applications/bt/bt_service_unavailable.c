#include <rtthread.h>

#include "bt_service_api.h"

/* Build-time fallback used only when FEATHERTALK_BT_STACK_BK is selected but
 * its optional source checkout is absent.  Core supervision and the SMIF XIP
 * guard must remain operational because the M55 user filesystem depends on
 * them; Bluetooth stays explicitly unavailable until the real stack exists. */
rt_err_t bt_service_start(void)
{
    return -RT_ENOSYS;
}

int bt_service_enabled(void)
{
    return 0;
}
rt_err_t bt_service_set_enabled(int on) { (void)on; return -RT_ENOSYS; }
int bt_service_busy(void) { return 0; }
int bt_service_target(void) { return 0; }
int bt_service_error(void) { return -RT_ENOSYS; }
int bt_service_connected(void) { return 0; }
