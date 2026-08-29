#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <feathertalk/version.h>
#include "ipc/feathertalk_ipc.h"

int main(void)
{
    rt_kprintf("FeatherTalk M33 %s\r\n", FEATHERTALK_M33_FIRMWARE_VERSION);
    rt_kprintf("IPC ABI %u; this core boots and supervises cortex-m55\r\n",
               FEATHERTALK_IPC_ABI_VERSION);
    rt_kprintf("Console: M33 control/MSH on uart5; M55 diagnostics use uart2\r\n");

    if (feathertalk_ipc_start() != RT_EOK)
    {
        rt_kprintf("FeatherTalk IPC supervisor failed to start\r\n");
    }
    else
    {
        rt_kprintf("Commands: feather_status, feather_ping\r\n");
    }
    while (1)
    {
        rt_thread_mdelay(1000);
    }
    return 0;
}
