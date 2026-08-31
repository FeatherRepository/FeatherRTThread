/* hal_cpu_rtthread.c - btstack 中断控制契约 (当前版本契约: void 返回)
 * 注: 该 btstack 版本的 hal_cpu.h 为无条件开/关中断语义, 不保存/恢复 PRIMASK;
 *     btstack 在非嵌套临界区中使用, RT-Thread 调度器场景下安全。 */
#include <rtthread.h>
#include <hal_cpu.h>

void hal_cpu_disable_irqs(void)
{
    rt_hw_interrupt_disable();
}

void hal_cpu_enable_irqs(void)
{
    rt_hw_interrupt_enable(0);
}

void hal_cpu_enable_irqs_and_sleep(void)
{
    rt_hw_interrupt_enable(0);
    /* btstack embedded run loop 每轮调用此函数; RT-Thread 下让出 CPU 1ms,
     * 避免高优先级 btloop 线程饿死控制台 (H4 响应延迟 <=2ms, P0 足够) */
    rt_thread_mdelay(1);
}
