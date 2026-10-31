#ifndef FT_AIRPLAY_SINK_H
#define FT_AIRPLAY_SINK_H
#include <stddef.h>
#include <stdint.h>
/* Decoded, signed interleaved PCM only. Call from a single playback worker.
 * No implicit format conversion, preemption or network-packet submission. */
int ft_ap_sink_open(uint32_t rate, uint8_t bits, uint8_t channels);
int ft_ap_sink_write(const void *pcm, size_t bytes);
int ft_ap_sink_close(void);
#endif
