#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/spi.h>
#include <stdlib.h>
#include <string.h>

#include "cy_gpio.h"
#include "cycfg_pins.h"
#include "feathertalk_fpga_audio_bridge.h"

/*
 * E84 SCB9 is physically routed to the 3.3 V RPi/PMOD connectors:
 *   P15.0: SPI9_CLK  -> Tang Nano pin 24 (SCLK)
 *   P15.1: SPI9_MOSI -> Tang Nano pin 22 (MOSI)
 *   P15.2: SPI9_MISO <- Tang Nano pin 18 (READY credit signal)
 *   P15.3: SPI9_SS0  -> Tang Nano pin 23 (CS#, controlled by drv_spi.c)
 */
#define FT_FPGA_SPI_BUS              "spi1"
#define FT_FPGA_SPI_DEVICE           "fpgai2s"
#define FT_FPGA_SPI_HZ               12000000U
#define FT_FPGA_READY_TIMEOUT_MS     30U

static struct rt_spi_device s_spi;
static struct rt_mutex s_lock;
static rt_bool_t s_initialized;
static ft_fpga_audio_bridge_status_t s_status;
CY_ALIGN(4) static rt_uint8_t
    s_wire_buffer[FT_FPGA_AUDIO_BURST_FRAMES * FT_FPGA_AUDIO_FRAME_BYTES];

static rt_bool_t fpga_ready(void)
{
    /*
     * P15.2 remains muxed to SCB9 MISO, but GPIO IN can be read without
     * changing that HSIOM selection. Do not call rt_pin_read(): its port
     * helper reconfigures the pin as GPIO and would disrupt SCB9.
     */
    return Cy_GPIO_Read(CYBSP_SPI_9_MISO_PORT, CYBSP_SPI_9_MISO_PIN) != 0U;
}

static rt_err_t fpga_wait_ready(rt_int32_t timeout_ms)
{
    rt_tick_t start = rt_tick_get();
    rt_tick_t timeout = rt_tick_from_millisecond(timeout_ms);

    if (!fpga_ready())
        s_status.ready_backpressure_events++;

    while (!fpga_ready())
    {
        if ((rt_tick_get() - start) >= timeout)
        {
            s_status.ready_timeouts++;
            return -RT_ETIMEOUT;
        }
        rt_thread_mdelay(1);
    }
    return RT_EOK;
}

static void fpga_pack_s24le(const rt_uint8_t *source, rt_uint8_t *wire,
                            rt_size_t frames)
{
    rt_size_t i;

    for (i = 0U; i < frames; i++)
    {
        const rt_uint8_t *in = source + i * FT_FPGA_AUDIO_FRAME_BYTES;
        rt_uint8_t *out = wire + i * FT_FPGA_AUDIO_FRAME_BYTES;

        /* Little-endian samples in memory -> left/right MSB-first SPI bytes. */
        out[0] = in[2]; out[1] = in[1]; out[2] = in[0];
        out[3] = in[5]; out[4] = in[4]; out[5] = in[3];
    }
}

rt_err_t ft_fpga_audio_bridge_write_s24le(const rt_uint8_t *pcm,
                                          rt_size_t frames)
{
    rt_err_t result = RT_EOK;

    if (pcm == RT_NULL || frames == 0U) return -RT_EINVAL;
    if (!s_initialized) return -RT_ENOSYS;

    result = rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK) return result;

    while (frames != 0U)
    {
        rt_size_t batch = frames;
        rt_size_t bytes;

        if (batch > FT_FPGA_AUDIO_BURST_FRAMES)
            batch = FT_FPGA_AUDIO_BURST_FRAMES;
        bytes = batch * FT_FPGA_AUDIO_FRAME_BYTES;

        /*
         * FPGA READY high guarantees room for this entire <=128-frame burst.
         * One rt_spi_transfer keeps CS# asserted across all 48-bit frames.
         */
        result = fpga_wait_ready(FT_FPGA_READY_TIMEOUT_MS);
        if (result != RT_EOK) break;

        fpga_pack_s24le(pcm, s_wire_buffer, batch);
        if (rt_spi_send(&s_spi, s_wire_buffer, bytes) != bytes)
        {
            result = -RT_ERROR;
            break;
        }

        s_status.frames_sent += (rt_uint32_t)batch;
        s_status.bursts_sent++;
        pcm += bytes;
        frames -= batch;
    }

    s_status.ready = fpga_ready();
    s_status.last_error = result;
    rt_mutex_release(&s_lock);
    return result;
}

rt_err_t ft_fpga_audio_bridge_get_status(ft_fpga_audio_bridge_status_t *status)
{
    if (status == RT_NULL) return -RT_EINVAL;

    *status = s_status;
    status->initialized = s_initialized;
    status->ready = s_initialized && fpga_ready();
    return RT_EOK;
}

static int ft_fpga_audio_bridge_init(void)
{
    struct rt_spi_configuration cfg;
    rt_err_t result;

    rt_memset(&s_status, 0, sizeof(s_status));
    result = rt_spi_bus_attach_device(&s_spi, FT_FPGA_SPI_DEVICE,
                                      FT_FPGA_SPI_BUS, RT_NULL);
    if (result != RT_EOK)
    {
        s_status.last_error = result;
        rt_kprintf("fpga-audio: cannot attach %s to %s (%d)\n",
                   FT_FPGA_SPI_DEVICE, FT_FPGA_SPI_BUS, result);
        return result;
    }

    cfg.data_width = 8U;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = FT_FPGA_SPI_HZ;
    result = rt_spi_configure(&s_spi, &cfg);
    if (result != RT_EOK)
    {
        s_status.last_error = result;
        rt_kprintf("fpga-audio: SPI mode-0 configure failed (%d)\n", result);
        return result;
    }

    rt_mutex_init(&s_lock, "fpga_i2s", RT_IPC_FLAG_PRIO);
    s_initialized = RT_TRUE;
    s_status.initialized = RT_TRUE;
    s_status.ready = fpga_ready();
    s_status.spi_hz = FT_FPGA_SPI_HZ;
    s_status.last_error = RT_EOK;
    rt_kprintf("fpga-audio: %s mode0 %lu Hz, READY=%s\n",
               FT_FPGA_SPI_BUS, (unsigned long)FT_FPGA_SPI_HZ,
               s_status.ready ? "high" : "low");
    return RT_EOK;
}
INIT_APP_EXPORT(ft_fpga_audio_bridge_init);

#ifdef RT_USING_MSH
static void fpga_make_test_tone(rt_uint8_t *pcm, rt_size_t frames,
                                rt_uint32_t *phase)
{
    rt_size_t i;

    for (i = 0U; i < frames; i++)
    {
        rt_uint32_t p = *phase;
        rt_int32_t s = (p < 0x80000000UL) ?
            (rt_int32_t)(p >> 8) - 0x400000 :
            0x400000 - (rt_int32_t)((p - 0x80000000UL) >> 8);
        rt_uint32_t sample = (rt_uint32_t)s & 0x00FFFFFFUL;
        rt_uint8_t *out = pcm + i * FT_FPGA_AUDIO_FRAME_BYTES;

        out[0] = (rt_uint8_t)sample;
        out[1] = (rt_uint8_t)(sample >> 8);
        out[2] = (rt_uint8_t)(sample >> 16);
        out[3] = out[0]; out[4] = out[1]; out[5] = out[2];
        *phase += 0x02AAAAABUL; /* 1 kHz at 96 kHz, 32-bit phase accumulator */
    }
}

static int ft_fpga_audio_bridge_msh(int argc, char **argv)
{
    ft_fpga_audio_bridge_status_t status;

    if (argc == 1 || (argc == 2 && rt_strcmp(argv[1], "status") == 0))
    {
        ft_fpga_audio_bridge_get_status(&status);
        rt_kprintf("fpga-audio: %s, READY=%s, spi=%lu Hz, frames=%lu, bursts=%lu, backpressure=%lu, timeouts=%lu, err=%ld\n",
                   status.initialized ? "ready" : "not initialized",
                   status.ready ? "high" : "low", (unsigned long)status.spi_hz,
                   (unsigned long)status.frames_sent, (unsigned long)status.bursts_sent,
                   (unsigned long)status.ready_backpressure_events,
                   (unsigned long)status.ready_timeouts, (long)status.last_error);
        return status.last_error;
    }

    if (argc == 3 && rt_strcmp(argv[1], "tone") == 0)
    {
        rt_uint32_t phase = 0U;
        rt_uint32_t seconds = (rt_uint32_t)strtoul(argv[2], RT_NULL, 10);
        rt_uint32_t remaining = seconds * FT_FPGA_AUDIO_SAMPLE_RATE;
        rt_uint8_t pcm[FT_FPGA_AUDIO_BURST_FRAMES * FT_FPGA_AUDIO_FRAME_BYTES];
        rt_err_t result = RT_EOK;

        if (seconds == 0U || seconds > 30U) return -RT_EINVAL;
        while (remaining != 0U && result == RT_EOK)
        {
            rt_size_t batch = remaining > FT_FPGA_AUDIO_BURST_FRAMES ?
                FT_FPGA_AUDIO_BURST_FRAMES : remaining;
            fpga_make_test_tone(pcm, batch, &phase);
            result = ft_fpga_audio_bridge_write_s24le(pcm, batch);
            remaining -= batch;
        }
        rt_kprintf("fpga-audio: 1 kHz tone %lu s -> %d\n",
                   (unsigned long)seconds, result);
        return result;
    }

    rt_kprintf("usage: ft_fpga_audio [status|tone <1..30>]\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(ft_fpga_audio_bridge_msh, ft_fpga_audio,
                     show FPGA I2S bridge status or play a 1 kHz test tone.);
#endif
