#ifndef FEATHERTALK_USB_UAC_H
#define FEATHERTALK_USB_UAC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool active;
    bool connected;
    bool configured;
    bool output_streaming;
    bool input_streaming;
    bool format_pending;
    uint32_t output_sample_rate;
    uint8_t output_sample_bits;
    uint8_t output_channels;
    uint32_t input_sample_rate;
    uint8_t input_sample_bits;
    uint8_t input_channels;
    uint32_t host_update_count;
    uint32_t device_update_count;
    uint32_t sync_generation;
    uint64_t host_to_device_bytes;
    uint64_t device_to_host_bytes;
    uint32_t output_overruns;
    uint32_t input_underruns;
    uint32_t output_callback_count;
    uint32_t output_ring_used;
    uint64_t output_ring_read_bytes;
    uint32_t output_worker_wakeups;
    uint32_t output_sound_open_count;
    uint32_t output_sound_write_calls;
    uint64_t output_sound_write_bytes;
    uint8_t output_worker_state;
    uint32_t output_ring_min;      /* M5 排障: 流期间 ring 低水位 (0=见底) */
    uint32_t output_gap_min_us;    /* M5 排障: USB OUT 相邻包最小间隔 */
    uint32_t output_gap_max_us;    /* M5 排障: 最大间隔 (成堆抖动证据) */
    uint32_t output_gap_over2ms;   /* M5 排障: 间隔 >2ms 的次数 */
    int last_error;
} ft_usb_uac_status_t;

int ft_usb_uac_start(void);
int ft_usb_uac_stop(void);
void ft_usb_uac_get_status(ft_usb_uac_status_t *status);
int ft_usb_uac_set_output_format(uint32_t sample_rate, uint8_t sample_bits,
                                 uint8_t channels, bool reconnect_host);
bool ft_usb_uac_output_format_supported(uint32_t sample_rate,
                                        uint8_t sample_bits,
                                        uint8_t channels);
bool ft_usb_uac_input_format_supported(uint32_t sample_rate,
                                       uint8_t sample_bits,
                                       uint8_t channels);

/* M5: A3 契约 §2 tap —— BT Source 模式消费 UAC OUT (host->device) PCM,
 * 返回实际读出字节数 (48k/16/2ch 帧对齐); 互斥由 ft_audio_claim_output 仲裁 */
uint32_t ft_usb_uac_output_read(uint8_t *data, uint32_t capacity);

/* M5: BT Source 激活时本地播放让位 (不再上 sound0, 防双消费同一 ring) */
void ft_usb_uac_set_bt_tap(rt_bool_t active);

#endif /* FEATHERTALK_USB_UAC_H */
