#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <feathertalk/version.h>
#include <feathertalk/smif_guard.h>
#include "ipc/feathertalk_ipc.h"
#include "bluetooth/feathertalk_bt_host.h"

int main(void)
{
    rt_kprintf("FeatherTalk M33 %s\r\n", FEATHERTALK_M33_FIRMWARE_VERSION);
    rt_kprintf("IPC ABI %u; this core boots and supervises cortex-m55\r\n",
               FEATHERTALK_IPC_ABI_VERSION);
    rt_kprintf("Console: M33 control/MSH on uart5; M55 diagnostics use uart2\r\n");

    /* The component initializer normally starts this service before main().
     * Keep the explicit idempotent call here so the shared-flash safety guard
     * can never be lost when component-init ordering changes. */
    if (feathertalk_smif_guard_service_start() != RT_EOK)
    {
        rt_kprintf("SMIF XIP guard service failed to start\r\n");
    }
    else
    {
        rt_kprintf("SMIF XIP guard service ready at 0x%08x\r\n",
                   (unsigned int)FEATHERTALK_SMIF_GUARD_M33_ADDR);
    }

    if (feathertalk_ipc_start() != RT_EOK)
    {
        rt_kprintf("FeatherTalk IPC supervisor failed to start\r\n");
    }
    else
    {
        rt_kprintf("Commands: feather_status, feather_ping\r\n");
    }

    if (feathertalk_bt_host_start() != RT_EOK)
    {
        rt_kprintf("FeatherTalk Bluetooth host thread failed to start\r\n");
    }
    else
    {
        rt_kprintf("Bluetooth: bt_status, bt_scan, bt_devices, bt_adv\r\n");
    }
    while (1)
    {
        rt_thread_mdelay(1000);
    }
    return 0;
}
