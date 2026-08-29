#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <feathertalk/version.h>

int main(void)
{
    rt_kprintf("FeatherTalk M33 %s\r\n", FEATHERTALK_M33_FIRMWARE_VERSION);
    rt_kprintf("IPC ABI %u; this core boots and supervises cortex-m55\r\n",
               FEATHERTALK_IPC_ABI_VERSION);
    while (1)
    {
        rt_thread_mdelay(1000);
    }
    return 0;
}
