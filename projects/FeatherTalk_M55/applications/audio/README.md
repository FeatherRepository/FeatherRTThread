# M55 Audio

Product audio pipelines, codecs, playback policy, and audio-facing services
belong here. Keep reusable hardware drivers in the BSP layer.

## Current board devices

- `sound0` is the default output: PSoC Edge TDM0/I2S TX -> ES8388 DAC ->
  MD8002 bridge amplifier -> onboard mono speaker. The RT-Thread device format
  supports 16/24/48/96 kHz, 16/24-bit and logical mono/stereo; the physical
  I2S link remains two-slot and the analog board path sums both DAC outputs.
- `mic0` is the default input: the two onboard PDM microphones share clock/data
  and are captured as 16 kHz, two-channel, 16-bit audio.
- The AMIC2 analog microphone front end is physically present but has no
  product RT-Thread Audio driver yet. Settings therefore shows it as disabled
  instead of pretending it is a selectable input.

`BSP_USING_AUDIO_PLAY`, `BSP_USING_AUDIO_RECORD` and
`ENABLE_STEREO_INPUT_FEED` are enabled in `FeatherTalk_M55`; both usable devices
are registered during RT-Thread device initialization. `feathertalk_audio.*`
provides status and level controls to the UI, plus the `feather_audio_status`
MSH command.

The Recorder app enumerates those input devices and enables only devices that
are both registered and initialized. A worker thread captures the selected
input without blocking LVGL, displays a live peak meter, and finalizes a PCM
WAV file when the user presses Stop. Files are written below `Recordings` on
the SD card first, with the fixed internal Flash volume as the fallback. See
[`RECORDER_DESIGN_zh.md`](RECORDER_DESIGN_zh.md) for the state machine, file
contract and validation procedure.

The board powers the ES8388 rail before device initialization but keeps the
MD8002 amplifier disabled. The ES8388 driver owns P21.6 and enables the
amplifier only after codec setup, preventing the previous unconditional boot
enable and reducing power-up pops. A `sound0` format change is transactional:
TDM word size, LRCK/BCLK/MCLK and the ES8388 DAC word length, speed mode and
MCLK/Fs ratio must all accept the candidate before it becomes visible. Codec
registers are read back, and a failed update restores the previous complete
path. `feather_i2s_diag` reports both the logical `sound0` format and the live
ES8388 readback.

## Settings behavior

Settings > Audio is a two-level, capability-driven device view. The first
level lists physical output and input devices with a default-device radio
marker and a properties chevron. The onboard speaker opens a dedicated page
for its supported sample rates, sample depths, channel count and output
volume. The dual PDM array opens a separate page that reports its fixed driver
format and exposes input gain. The analog microphone front end remains visible
but unavailable until it has a product driver; it cannot be selected or opened.

Output volume (0-100) and PDM input gain (0-37.5 dB in 0.5 dB steps) are
applied on slider release to avoid repeated I2C writes while dragging. Both
values are stored in the existing per-device A/B preference record and
restored at boot. Format choices are enabled only when the complete candidate
combination is accepted by `feathertalk_audio`; unsupported controls are not
presented as working settings.

## USB Audio

CherryUSB UAC2 is now available as the bidirectional USB Audio device function.
The host playback stream is routed to `sound0`; its stereo USB Terminal exposes
16/24/48/96 kHz and 16/24-bit PCM. The capture stream is routed from `mic0` and
truthfully exposes only the current PDM-driver format: 16 kHz, stereo, 16-bit.
Settings > USB shows both device routes and capability-driven format controls.

Clock `SET_CUR`, streaming alternate settings, volume and mute are reflected
from the host into the device. Device-side output changes reconfigure `sound0`
and deliberately re-enumerate UAC so the host refreshes the endpoint. Host
enumeration probes are kept separate from the effective stream format; a host
format is committed only when real OUT data arrives. See
[`USB_AUDIO_UAC2_zh.md`](USB_AUDIO_UAC2_zh.md) for descriptors, synchronization,
commands, validation and current limits.

Playback content policy and the analog AMIC2 driver remain later stages.
