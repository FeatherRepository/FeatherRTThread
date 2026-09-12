# FPGA SPI to I2S external-DAC bridge

Current default (2026-09-13): FEATHERTALK_USING_FPGA_AUDIO_BRIDGE is
disabled in FeatherTalk_M55/rtconfig.h. The driver is retained as an opt-in
prototype. LE Audio playback uses sound0/ES8388. Historical bring-up notes
below do not imply the external DAC path is enabled in this release.

This document defines the M55-side interface for the Tang Nano 1K SPI to I2S
prototype.  The M55 driver is deliberately separate from `sound0`/ES8388:
the on-board codec remains usable, while an application selects the external
DAC by calling `ft_fpga_audio_bridge_write_s24le()`.

## E84 to Tang Nano wiring

Use the E84 RPi 40-pin connector (`CN5`).  These signals are already level
translated to 3.3 V on the E84 baseboard.  They are the SDK's SCB9 (`spi1`)
pins, not the ES8388 I2S pins.

| E84 SoC / SDK | E84 CN5 RPi pin | Signal direction | Tang Nano 1K FPGA pin | FPGA signal |
|---|---:|---|---:|---|
| P15.0, SCB9 SPI CLK | 23, SCLK | E84 -> FPGA | package 24 / P1.9 | `spi_sclk` |
| P15.1, SCB9 SPI MOSI | 19, MOSI | E84 -> FPGA | package 22 / P1.7 | `spi_mosi` |
| P15.3, SCB9 SPI SELECT0 | 24, CE0 | E84 -> FPGA | package 23 / P1.8 | `spi_cs_n` |
| P15.2, SCB9 SPI MISO | 21, MISO | FPGA -> E84 | package 18 / P1.6 | `spi_ready` |
| GND | 6, 9, 14, 20, 25, 30, 34 or 39 | common reference | any GND | GND |

Do not connect the E84 5 V rail to the Tang Nano when the Tang board is USB
powered.  The connection needs a common ground; all signal pins above are
3.3 V logic.  `spi_ready` is intentionally wired onto MISO.  It is not an SPI
reply stream; it is a static flow-control input sampled by the M55 driver.

The same nets are also present on `CN8` (PMOD2): pin 3 MOSI, pin 4 SCLK,
pin 1 MISO/READY and pin 9 SS0/CS#.  CN5 is less ambiguous for the initial
wire harness because its standard RPi labels match the driver documentation.

## Tang Nano to PCM5102A module

The supplied module schematic is a PCM5102A board with a 5 V input LDO and a
five-pin I2S header `P3`.  This bridge uses four-wire I2S and drives the
module `SCK` from the FPGA's `i2s_mclk` output.

| Tang Nano FPGA pin | Output | PCM5102A module connector | Module signal |
|---:|---|---:|---|
| package 16 / P1.4 | `i2s_bclk`, 6.144 MHz | P3.3 | BCK |
| package 27 / P1.2 | `i2s_lrck`, 96 kHz | P3.1 | LRCK |
| package 15 / P1.3 | `i2s_sdout`, 24-bit I2S | P3.2 | DIN |
| P1.16 or P1.17 | common reference | P3.5 | DGND |
| package 17 / P1.5 | `i2s_mclk`, 24.576 MHz average | P3.4 | SCK |

Power the PCM5102A module at its `P1.2 = +5 V`, with `P1.1 = GND`; do not
feed its 5 V input from the Tang Nano 3.3 V rail.  Keep the FPGA, E84 and DAC
grounds common.  The digital I2S inputs on this module are powered from its
on-board 3.3 V rail and match Tang Nano I/O levels.

The Tang assignment above is derived from the bundled Tang Nano 1K schematic
(`gowin-tang-nano-audio-bridge/docs/Sipeed_Tang_Nano_1K_Schematic_6100.pdf`,
page 4).  Do not use FPGA package pin 21: it is LCD FPC `DEN`, not a P1/P2
expansion signal.  The selected P1.2..P1.9 pins are on the board's 3.3 V
`VBANK1`; P1.18/P1.19 are 5 V power pins and must not be used as logic I/O.

Set the module's control jumpers before test: `FMT=GND` (P5.3 to P6, I2S),
`FLT=GND` (P5.4 to P6, normal latency), `DEMP=GND` (P5.1 to P6, off), and
`XSMT=DVDD` (P5.2 to P4, unmuted).  Do not leave the straps floating.

The present 1K prototype derives BCK/LRCK from its 27 MHz board oscillator by
fractional division. It is suitable for functional playback and driver
bring-up, not a final low-jitter HiFi clock architecture. A final DAC board
should use an audio XO / clock generator appropriate to the 44.1 kHz and
48 kHz sample-rate families.

## Wire protocol and flow control

- SPI master: E84 M55 SCB9 (`spi1`), mode 0, MSB-first, 8-bit words, 12 MHz.
- PCM: fixed 96,000 Hz, stereo, signed 24-bit.
- A frame is 48 bits: `left[23:0]` then `right[23:0]`, each MSB first.
- A transaction has `6 * N` bytes where `1 <= N <= 128`; CS# remains low for
  all frames in the burst, then rises only at a 48-bit frame boundary.
- The M55 public API accepts interleaved packed S24_LE frames and reverses
  bytes within each sample before SPI transmission.
- `READY=1` is a credit, not simply a non-full flag: the FPGA guarantees at
  least 128 FIFO entries are free.  The M55 never starts a burst without it.

This driver requires the companion FPGA RTL whose `async_fifo` exports the
free-running audio-domain `r_ready_burst` credit signal and whose
`spi_pcm_slave` maps that signal to `spi_ready`.
Older FPGA images that drive READY as `!fifo_full` must be rebuilt before
continuous audio is tested, otherwise a multi-frame SPI burst can overrun the
last few FIFO slots.

## SDK integration and initial test

`projects/FeatherTalk_M55/rtconfig.h` enables `RT_USING_SPI`, `BSP_USING_SPI`
and `BSP_USING_SPI1`.  The independent driver lives under
`applications/fpga_audio_bridge/` and attaches `fpgai2s` to `spi1` at boot.

With `FEATHERTALK_USING_USB_UAC` and `FEATHERTALK_USING_FPGA_AUDIO_BRIDGE`
enabled, this M55 profile boots directly as a USB Audio Class 2 device.  Its
speaker terminal is intentionally fixed to 96,000 Hz, packed signed 24-bit,
two channels; there is no host-visible 16-bit or alternate sample-rate mode.
USB OUT data is delivered directly to `ft_fpga_audio_bridge_write_s24le()` and
never opens `sound0` or reconfigures ES8388.  The UAC capture terminal remains
the independent `mic0` path at 16 kHz/16-bit stereo.

After loading the matching FPGA image and connecting the wires, use MSH:

```text
msh /> ft_fpga_audio status
msh /> ft_fpga_audio tone 3
msh /> feather_usb status
```

`status` must show `READY=high`.  `tone 3` streams a 1 kHz stereo 24-bit test
tone for three seconds; it is a transport/DAC test, not a mixer or a new
default output selection.  `feather_usb status` must report `function=audio`
and `UAC2 out: 96000 Hz 24-bit 2-ch`; after the host starts playback,
`host->device` and the FPGA bridge frame counters must increase together.

## 2026-09-05 FPGA-UAC bring-up image

- M55 was rebuilt successfully after enabling the direct UAC output path.
  The programmed `projects/FeatherTalk_M55/rtthread.hex` SHA-256 is
  `DE4F1C19DD68FFCBF599F520B47EB2B2F6113742D87BA8A88C7C0E5233C7A521`.
- Infineon OpenOCD/KitProg3 erased and wrote the M55 SMIF XIP image, then
  completed its command normally. M33 secure firmware was intentionally not
  rewritten: the change is wholly in the M55 application image.
- The current Windows host did not have a `VID_FFFF&PID_F502` USB device
  present after programming, so host enumeration and audio playback remain a
  hardware validation item. The KitProg3 debug USB connection is not the E84
  USB device data connection.
