/* SPDX-License-Identifier: Apache-2.0
 * RT-Thread H4 block adapter: explicit close and fresh completion flags on
 * every open. The generic embedded adapter leaves close-device as a TODO.
 * All callbacks into H4 run on btloop, never on the UART ISR. */
#include <rtthread.h>
#include <hal_uart_dma.h>
#include "btstack_uart_block.h"
#include "btstack_run_loop.h"

extern void bt_uart_shutdown(void);
extern void hal_uart_dma_rtthread_start_rx_thread(void);
extern int hal_uart_dma_rtthread_init_err(void);
extern int hal_uart_dma_set_flowcontrol(int flowcontrol);
static const btstack_uart_config_t *config;
static btstack_data_source_t source;
static volatile int received, sent;
static int opened;
static void (*receive_done)(void), (*send_done)(void);

static void rx_irq(void) { received = 1; btstack_run_loop_poll_data_sources_from_irq(); }
static void tx_irq(void) { sent = 1; btstack_run_loop_poll_data_sources_from_irq(); }
static void poll_source(btstack_data_source_t *ds, btstack_data_source_callback_type_t type)
{
    (void)ds; (void)type;
    if (sent) { sent = 0; if (opened && send_done) send_done(); }
    if (received) { received = 0; if (opened && receive_done) receive_done(); }
}
static int uart_init(const btstack_uart_config_t *value) { config = value; return 0; }
static int uart_open(void)
{
    if (opened || !config) return -1;
    hal_uart_dma_init();
    if (hal_uart_dma_rtthread_init_err() || hal_uart_dma_set_baud(config->baudrate)) return -1;
    received = sent = 0;
    hal_uart_dma_set_block_received(rx_irq);
    hal_uart_dma_set_block_sent(tx_irq);
    opened = 1;
    btstack_run_loop_set_data_source_handler(&source, poll_source);
    btstack_run_loop_enable_data_source_callbacks(&source, DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_add_data_source(&source);
    hal_uart_dma_rtthread_start_rx_thread();
    return 0;
}
static int uart_close(void)
{
    bt_uart_shutdown();
    if (opened) {
        btstack_run_loop_disable_data_source_callbacks(&source, DATA_SOURCE_CALLBACK_POLL);
        btstack_run_loop_remove_data_source(&source);
    }
    opened = received = sent = 0;
    return 0;
}
static void set_rx(void (*cb)(void)) { receive_done = cb; }
static void set_tx(void (*cb)(void)) { send_done = cb; }
static int parity(int value) { (void)value; return 0; }
static int sleep_modes(void) { return 0; }
static void sleep_set(btstack_uart_sleep_mode_t value) { (void)value; }
static void wake_set(void (*cb)(void)) { (void)cb; }
const btstack_uart_block_t *btstack_uart_block_embedded_instance(void)
{
    static const btstack_uart_block_t driver = {
        .init = uart_init, .open = uart_open, .close = uart_close,
        .set_block_received = set_rx, .set_block_sent = set_tx,
        .set_baudrate = hal_uart_dma_set_baud, .set_parity = parity,
        .set_flowcontrol = hal_uart_dma_set_flowcontrol,
        .receive_block = hal_uart_dma_receive_block, .send_block = hal_uart_dma_send_block,
        .get_supported_sleep_modes = sleep_modes, .set_sleep = sleep_set,
        .set_wakeup_handler = wake_set
    };
    return &driver;
}
