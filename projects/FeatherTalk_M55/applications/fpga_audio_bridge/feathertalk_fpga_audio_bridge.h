/*
 * FeatherTalk SPI-to-I2S FPGA audio bridge.
 *
 * The bridge is an independent 96 kHz / 24-bit stereo output.  It does not
 * replace sound0 or the ES8388 path: applications choose this API explicitly
 * when an external DAC is connected to the Tang Nano FPGA.
 */
#ifndef FEATHERTALK_FPGA_AUDIO_BRIDGE_H
#define FEATHERTALK_FPGA_AUDIO_BRIDGE_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT_FPGA_AUDIO_SAMPLE_RATE       96000U
#define FT_FPGA_AUDIO_SAMPLE_BITS        24U
#define FT_FPGA_AUDIO_CHANNELS            2U
#define FT_FPGA_AUDIO_FRAME_BYTES         6U
#define FT_FPGA_AUDIO_BURST_FRAMES      128U

typedef struct
{
    rt_bool_t initialized;
    rt_bool_t ready;
    rt_uint32_t spi_hz;
    rt_uint32_t frames_sent;
    rt_uint32_t bursts_sent;
    /* Number of bursts that observed FPGA FIFO backpressure (READY low). */
    rt_uint32_t ready_backpressure_events;
    rt_uint32_t ready_timeouts;
    rt_int32_t last_error;
} ft_fpga_audio_bridge_status_t;

/*
 * Write interleaved signed 24-bit little-endian PCM.
 *
 * Each frame is exactly six bytes: L0, L1, L2, R0, R1, R2.  The driver
 * converts it to the FPGA wire format {left[23:0], right[23:0]}, MSB first.
 * frames may be any positive value; the transport internally uses up to 128
 * frames per READY-qualified SPI burst.
 */
rt_err_t ft_fpga_audio_bridge_write_s24le(const rt_uint8_t *pcm,
                                          rt_size_t frames);

rt_err_t ft_fpga_audio_bridge_get_status(ft_fpga_audio_bridge_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_FPGA_AUDIO_BRIDGE_H */
