/* Volatile scheduling A/B probe. No credentials or radio state are changed. */
#include <rtthread.h>
#include <string.h>

#ifdef FEATHERTALK_USING_WIFI
#include <lwip/tcpip.h>
#include <lwip/priv/tcp_priv.h>
/* Inspect metadata on the owning TCP/IP thread; never read packet payloads. */
static void wifi_tcp_snapshot(void *argument)
{
    struct tcp_pcb *pcb;
    unsigned count = 0;
    (void)argument;
    for (pcb = tcp_active_pcbs; pcb; pcb = pcb->next) {
        unsigned ooo = 0;
        if (pcb->local_port != 5001 && pcb->remote_port != 5001) continue;
#if TCP_QUEUE_OOSEQ
        struct tcp_seg *seg;
        for (seg = pcb->ooseq; seg; seg = seg->next) ooo += seg->len;
#endif
        ++count;
        rt_kprintf("[wifi-tcp] state=%u local=%u remote=%u wnd=%u ann=%u ooo=%u refused=%u retrans=%u\n",
                   (unsigned)pcb->state, pcb->local_port, pcb->remote_port,
                   (unsigned)pcb->rcv_wnd, (unsigned)pcb->rcv_ann_wnd, ooo,
                   pcb->refused_data ? pcb->refused_data->tot_len : 0, pcb->nrtx);
    }
    rt_kprintf("[wifi-tcp] active-iperf=%u\n", count);
}
static void ft_wifi_tcp(void)
{
    err_t result = tcpip_try_callback(wifi_tcp_snapshot, RT_NULL);
    if (result != ERR_OK) rt_kprintf("[wifi-tcp] queue failed=%d\n", result);
}
MSH_CMD_EXPORT(ft_wifi_tcp, Snapshot iperf TCP metadata without packet contents);

static void ft_wifi_sched(int argc, char **argv)
{
    rt_uint8_t whd_priority, sdio_priority;
    rt_thread_t whd, sdio;
    extern volatile rt_uint32_t ft_sdhc_poll_calls, ft_sdhc_poll_delays, ft_sdhc_sema_calls;
    if (argc == 1) {
        rt_kprintf("[wifi-sdhc] poll=%u delays=%u semaphore=%u\n",
                   ft_sdhc_poll_calls, ft_sdhc_poll_delays, ft_sdhc_sema_calls);
        return;
    }
    if (argc != 2) goto usage;
    if (!strcmp(argv[1], "product")) {
        whd_priority = 25; sdio_priority = 7;
    } else if (!strcmp(argv[1], "sdk")) {
        whd_priority = 25; sdio_priority = 0;
    } else if (!strcmp(argv[1], "balanced")) {
        /* Keep audio workers (12/13) above WHD, UI (21) below it. */
        whd_priority = 14; sdio_priority = 7;
    } else goto usage;
    rt_enter_critical();
    whd = rt_thread_find("WHD");
    sdio = rt_thread_find("sdio_irq");
    if (whd && sdio) {
        rt_err_t a = rt_thread_control(whd, RT_THREAD_CTRL_CHANGE_PRIORITY, &whd_priority);
        rt_err_t b = rt_thread_control(sdio, RT_THREAD_CTRL_CHANGE_PRIORITY, &sdio_priority);
        rt_exit_critical();
        rt_kprintf("[wifi-sched] WHD=%u SDIO=%u result=%d/%d (volatile)\n",
                   whd_priority, sdio_priority, a, b);
    } else {
        rt_exit_critical();
        rt_kprintf("[wifi-sched] Wi-Fi threads unavailable; unchanged\n");
    }
    return;
usage:
    rt_kprintf("ft_wifi_sched product|sdk|balanced (temporary A/B profile)\n");
}
MSH_CMD_EXPORT(ft_wifi_sched, Temporary WiFi scheduling A/B profile);
#endif
