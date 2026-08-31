/* hal_time_ms_rtthread.c - btstack 毫秒时基 */
#include <rtthread.h>
#include <hal_time_ms.h>

uint32_t hal_time_ms(void)
{
    return (uint32_t)rt_tick_get_millisecond();
}
