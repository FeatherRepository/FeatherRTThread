# FeatherTalk — A Dual-Core Smart Audio Terminal on PSoC Edge E84

[**中文**](./README_zh.md) | **English**

> A buildable, programmable, and diagnosable integrated demo that brings together the Cortex-M55 / Cortex-M33 cores, LVGL + VG-Lite, Bluetooth audio, USB, Wi-Fi, local keyword spotting, and NPU benchmarks on RT-Thread.

![Edgi-Talk](docs/figures/board_large.jpg)

## Submission materials

| Material | Location in this README | Status |
| --- | --- | --- |
| Project overview | Section 1 | Ready |
| Feature introduction | Section 2 | Ready |
| Hardware framework | Section 3 | Ready |
| Software framework | Section 4 | Ready |
| Open-source software code | This repository; layout in Section 5 | Submission link/archive pending |
| Demo video | Section 10 (suggested script) | To be recorded |

This repository contains two layers:

1. **The Edgi-Talk BSP/SDK**: RT-Thread board support, drivers, and standalone peripheral examples for the PSoC Edge E84.
2. **The FeatherTalk product application**: a dual-core integrated application built on the same SDK, covering UI, audio, USB, Wi-Fi, Classic Bluetooth, LE Audio, Auracast, local keyword spotting, NPU benchmarks, and an AirPlay2 research prototype.

The product branch is `product/edgi-talk`. FeatherTalk is not another single-core example: it consists of the paired **`FeatherTalk_M33` and `FeatherTalk_M55` projects and their shared modules**.

---

## 1. Project overview

### 1.1 Background

An edge smart-audio device must handle a touch UI, playback and recording, Bluetooth links, USB audio, a filesystem, wireless networking, and local AI at the same time. The PSoC Edge E84 provides Cortex-M55 and Cortex-M33 cores, an Ethos-U55 NPU, a GCNano Ultra V GPU, the DC8000Nano display controller, and a rich peripheral set. Official material, however, focuses mainly on individual peripherals or middleware rather than a complete product-shaped integration.

FeatherTalk is not another single-peripheral or single-stack demo; it addresses three engineering questions:

- How should the two cores divide work so that real-time audio, radio stacks, and a complex UI do not starve each other?
- How can display, audio, USB, storage, and wireless share limited SRAM, HyperRAM, DMA, and bus bandwidth?
- How can all of this live in one firmware image with a repeatable build, programming, and diagnostic flow?

### 1.2 Positioning

**A runnable dual-core integrated demo and an architecture-validation platform for productization.**

- For reviewers: a complete UI, audio, Bluetooth, USB, Wi-Fi, KWS, and NPU benchmark demo.
- For developers: reference implementations for dual-core boot, IPC, audio arbitration, radio resource management, and tooling.
- For future products: explicit boundaries so experimental work (AirPlay2, FPGA/Smart I/O audio bridge, custom baseboards) is not mistaken for production delivery.

### 1.3 Main innovations

1. **Dual-core RT-Thread productization**: M33 owns startup, Bluetooth, radio resources, and cross-core services; M55 owns UI, audio, USB, storage, and Wi-Fi, coordinated through a versioned IPC and shared SRAM.
2. **LVGL 9.2 + VG-Lite frame batching**: LVGL draw tasks are collected per frame and encoded into a VG-Lite command stream; two RGB565 framebuffers serve both rendering and DC scanout.
3. **Cross-core audio data plane**: bidirectional shared-SRAM rings with doorbells carry SBC/LC3 frames, format, and state; `sound0` arbitrates local playback, UAC, A2DP Sink, LE Audio, and AirPlay2 through an owner interface.
4. **Selectable A2DP Source input**: the encoder reads either the USB host UAC tap or the local-player tap, no longer fixed to USB. Local playback to Bluetooth uses a separate `player_bt_tap` and does not take `sound0`.
5. **Radio coexistence**: Wi-Fi and Bluetooth have independent claims, state, reset, and error counters. A normal feature stop does not power down the other feature or their shared supply.
6. **Shared-Flash guard**: while M55 erases the user Flash volume, a cross-core guard prevents M33 from executing stale cached code from the same SMIF device.
7. **Complete toolchain**: dual-core build, signing/merge, OpenOCD program/verify, serial tools, UI/GPU benchmarks, and radio/audio tests are reproducible.

### 1.4 Main outcomes

- One bootable, interactive, programmable M33/M55 firmware set.
- 21 UI pages and 32 repeatable benchmark scenes covering home, search, settings, media, recorder, files, gallery, and Bluetooth.
- A local music player with WAV/MP3, 3D cover flow, album art, and speaker/Bluetooth output selection.
- Classic Bluetooth A2DP Sink/Source, LE Audio unicast, Auracast broadcast source, pairing persistence, and scan/connect UI.
- Bidirectional USB UAC, USB MSC, and USB-to-Bluetooth forwarding.
- Wi-Fi scan/connect, TCP diagnostics, and an AirPlay2 research prototype.
- A local “Xiaomai” keyword-spotting path and Ethos-U55 bandwidth benchmarks.
- Repeatable build, programming, serial, and test scripts.

---

## 2. Feature introduction

### 2.1 Desktop and system interaction

- 480×800 touch display with adaptive Chinese/English UI.
- Home screen, app list, notification shade, global search, settings, and multi-level subpages.
- Persistent theme color, wallpaper, time format, timezone, language, and brightness preferences.
- System page reports dual-core, memory, storage, display link, radio, and IPC status.
- Shell diagnostics and benchmarks: `feather_ui_scene`, `feather_ui_status`, `feather_ui_bench`.

### 2.2 Music player

- Select a folder on SD card or internal Flash; direct WAV/MP3 children become the playlist.
- MP3 uses minimp3; WAV supports 8/16/24/32-bit PCM and common sample rates.
- 3D Cover Flow: enlarged center artwork, perspective side covers, reflections, and gradient masks, drawn through VG-Lite 2.5D acceleration.
- Album-art sidecar files; play/pause, previous/next, progress, volume, and folder loop.
- Output selection: **onboard speaker** or **Bluetooth headphones**. A2DP Source input selection: **USB audio** or **local player**.
- Media page and Bluetooth settings stay in sync; switching to Bluetooth without a connected headset shows an explicit prompt.
- Bluetooth output uses a dedicated 32 KB `player_bt_tap` ring; PCM is resampled to 48 kHz/16-bit/stereo before SBC encoding.

### 2.3 Recorder, files, and gallery

- PDM digital microphone recording to WAV on SD card or internal Flash.
- File manager for `/sdcard` and `/flash`: enter folders, copy/paste, rename, delete, and format.
- Gallery preview of images on SD/Flash, full-screen view, and set-as-wallpaper.
- Storage page shows capacity, mount state, and format actions with explicit destructive warnings.

### 2.4 Bluetooth audio

- Classic Bluetooth: discoverability, SSP pairing, link-key persistence, reconnect, and AVRCP.
- A2DP Sink: the board acts as a speaker for phone streaming.
- A2DP Source: the board sends the selected source (local files or USB audio) to Bluetooth headphones.
- Scan results use a fixed-height scrollable list with name, MAC, and connection marker; remote names are resolved after inquiry.
- LE Audio: BAP Unicast Server, dual CIS, LC3, PACS/ASCS/CAS/TMAP/VCS, and LE bond persistence.
- Auracast: BIG/BIS broadcast source, periodic advertising, start/stop, and unicast arbitration.
- A2DP role, LE Audio role, and audio transport (dual / Classic-only / LE-only) are switchable in the UI with explicit mutual-exclusion rules.

### 2.5 USB audio and storage

- USB UAC speaker: host audio enters M55 and leaves through `sound0` to the onboard codec/amplifier.
- USB UAC microphone: onboard PDM microphone audio goes upstream to the USB host.
- Dynamic sample-rate/bit-depth/channel negotiation and host/device clock-drift correction.
- USB MSC exposes the SD card or internal Flash user volume to a PC.
- USB-to-Bluetooth forwarding can send UAC audio directly to an A2DP Source headset.

### 2.6 Wi-Fi and AirPlay2

- CYW5551x Wi-Fi over SDIO on M55; WHD + lwIP provide scan, connect, and TCP/IP.
- Wi-Fi and Bluetooth start/stop independently with separate claims and error counters.
- AirPlay2 research prototype: mDNS, RTSP/HTTP, HAP transient pairing, session/RTP encryption, ALAC/AAC-LC, type96/type103, and `sound0` output.
- Without an AP, AirPlay2 waits and does not claim the audio device. Real-iPhone continuous playback and PTP synchronization remain to be accepted.

### 2.7 Local keyword spotting and NPU

- Self-trained “Xiaomai” wake-word model, PDM microphone, sliding-window inference, autostart, M33 event integration, and green LED feedback.
- Capture, training, validation tools, and a two-stage verifier; models continue to evolve, so recall/false-accept rates are accepted per model version.
- Ethos-U55 raw-register command streams, HyperRAM/Flash weight-read bandwidth benchmarks, and model-generation tools. This is a data-path evaluation, not a claim of completed local LLM inference.

### 2.8 Development and diagnostics

- M55 product shell: `feather_m55_status`, `bt_status`, `ft_wifi`, `ft_airplay`, `feather_ui_status`, `ft_audio_stats`, `kws`, and more.
- Audio/Bluetooth commands: `feather_player`, `feather_bt_source`, `ft_sbc_enc_stats`.
- M33 UART5 diagnostic console: `feather_status`, `feather_ping`, and lower-level Bluetooth diagnostics.
- UI scene switching, GPU/refresh/scanout/heap statistics, and a 60-frame full-screen benchmark.
- Dual-core build, signed merge, OpenOCD program/verify, serial monitor, and host-side tests.

### 2.9 Feature status

| Area | Developed functionality | Current status and limits |
| --- | --- | --- |
| Dual-core foundation | Boot flow, versioned IPC, heartbeat, cross-core audio ring, shared-Flash and radio resource management | Integrated; current image passes startup and basic IPC checks |
| Product UI | Bilingual Shell, gestures, local playback, recorder, files, gallery, storage, settings, and Bluetooth pages | Integrated; UI, GPU, and navigation checks pass, with full interaction regression still required per release |
| GPU and display | VG-Lite batching, dual-framebuffer direct scanout, backlight control, and benchmarks | Dedicated board results exist; current checks show no overflow or scanout timeout |
| Local audio and USB | Local playback with speaker/Bluetooth output selection, selectable A2DP Source input, recording, bidirectional UAC, rate/clock calibration, USB MSC, and USB-to-Bluetooth forwarding | Developed with dedicated test records; still needs external USB-host and listening regression |
| Classic Bluetooth | BTstack, discoverability, SSP pairing, link-key persistence, A2DP Sink/Source, source selection, role switching, reconnect, and AVRCP | USB forwarding has end-to-end audio records; local-player A2DP Source has encode, negotiation, and output-switch paths, with real-headphone retest pending |
| LE Audio | BAP Unicast Server, dual CIS, LC3, PACS/ASCS/CAS/TMAP/VCS, and LE bond persistence | Real phone connection and audio demonstrated; long-term continuity, more phones, and full compliance are not claimed |
| Auracast | BIG/BIS broadcast source, periodic advertising, lifecycle, and unicast arbitration | BIG/BIS data path is implemented; two control start/stop cycles pass, with receiver listening still required |
| Wi-Fi and coexistence | Dual-band scan/connect, independent radio lifecycle, performance work, TCP and memory diagnostics | Dual-band short tests and performance records exist; current image passes a real scan, while long-term throughput and audio concurrency remain |
| AirPlay2 | mDNS, RTSP/HTTP, HAP transient pairing, session/RTP encryption, ALAC/AAC-LC, type96/type103, and `sound0` output | Research prototype; protocol and simulated network-audio tests pass, while real iPhone playback, PTP sync, and long-duration acceptance are incomplete |
| Local keyword spotting | “Xiaomai” model, PDM microphone, sliding-window inference, autostart, M33 events, LED, capture/training/validation tools, and a two-stage verifier | End-to-end board path demonstrated; models still evolve, so recall and false-accept rates must be accepted per version |
| Ethos-U55 NPU | Raw-register command streams, HyperRAM/Flash weight-read bandwidth benchmarks, and model-generation tools | Research and measurement tooling; not a completed local-LLM inference feature |
| Build and diagnostics | Dual-core build, signed merge, OpenOCD program/verify, serial tools, PyOCD auxiliary path, and UI/radio/audio tests | Integrated and used for the current build and board checks |
| Custom hardware | Core expansion architecture, pin allocation, USB+PMU daughterboards, and design constraints | Requirements/design stage only; not evidence of fabrication or validation |
| SPI-to-I2S | FPGA audio-bridge design material and analysis of the SoC SPI + Smart I/O route | Not included in the integrated firmware; Smart I/O driver development has not started |

---

## 3. Hardware framework

### 3.1 Board composition

The current validation platform is the Edgi-Talk PSoC Edge E84 board in a core-board + baseboard form factor:

- **Edgi-Talk_Core**: PSoC Edge E84 (BGA-220), QSPI NOR, HyperRAM, PDM/analog audio front end, 1.8 V power, and board-to-board connectors.
- **Baseboard**: BQ25606 charger, ES8388 codec, MD8002 amplifier, CYW5551x Wi-Fi/BT module, MIPI-DSI display, SD card, CAN, Raspberry Pi 40-pin expansion, and KitProg3 debugger.
- Core board and baseboard connect through **2× DF40C 80-pin** board-to-board connectors.
- Debug uses the onboard KitProg3 (SWD/JTAG and debug UART); the product serial console defaults to 115200 8N1.

### 3.2 SoC resources

| Block | Hardware capability | Role in FeatherTalk |
| --- | --- | --- |
| Cortex-M55 | Up to 400 MHz; Helium/MVE, DSP, FPU; 32 KiB I-cache, 32 KiB D-cache; 256 KiB ITCM, 256 KiB DTCM | RT-Thread, LVGL, application, display, files, USB, and audio master |
| Cortex-M33 | Up to 200 MHz; DSP, FPU, TrustZone; 16 KiB I-cache | Secure boot, Bluetooth host, radio management, M55 startup, and cross-core services |
| Ethos-U55 | Up to 400 MHz; 128 MAC/cycle; 51.2 GOPS INT8 peak | Present in hardware; currently used for NPU bandwidth benchmarks, not a business model |
| NNLite | 4 MAC/cycle, associated with the M33 clock domain | Not used by the current product |
| GPU | GCNano Ultra V with VG-Lite 3.0 interface | LVGL 2.5D drawing, cover flow, graphics composition |
| Display controller | DC8000Nano + MIPI-DSI | Dual-framebuffer direct scanout to a 480×800 panel |

The full device part number is `PSE846GPS2DBZC4A` (PSoC Edge E84 Consumer, EPC2, BGA-220).

### 3.3 Memory and storage

| Resource | Capacity | Purpose |
| --- | ---: | --- |
| Boot ROM | 64 KiB | Fixed boot code |
| RRAM | 512 KiB | Secure boot, lifecycle, and user NVM |
| M55 ITCM / DTCM | 256 KiB / 256 KiB | Low-latency code, data, and stack |
| M33 subsystem SRAM | 1 MiB | M33 private code/data and cross-core reserved areas |
| SoCMEM | 5 MiB | Shared memory, GPU/display, audio rings, and heap, split by the linker script |
| QSPI NOR | 128 Mbit (16 MiB) | M33/M55 XIP, signed slots, and the trailing 2 MiB `/flash` user volume |
| HyperRAM | 128 Mbit (16 MiB) | M33/M55 private regions and a 12 MiB shared region |
| microSD | Onboard SDHC1, 4-bit | FAT filesystem mounted at `/sdcard` |

### 3.4 Peripherals and interfaces

| Resource | Board implementation |
| --- | --- |
| LCD | 480×800 RGB565, MIPI-DSI 2 data lanes |
| Touch | Panel I2C + INT + RESET, ST7102/ST7123 path |
| Backlight | AP3019A-class boost + PWM18, currently 5 kHz |
| Codec | ES8388 analog input/output |
| Amplifier/speaker | MD8002-class 2 W AB amplifier + onboard/external speaker |
| Digital microphone | 2× PDM MEMS |
| Wi-Fi/Bluetooth | CYW5551x combo; Wi-Fi over SDIO, Bluetooth over UART + Wake/Reset |
| USB | USB 2.0 HS/FS PHY; product uses UAC + MSC |
| SD | SDHC1, 4-bit, card detect |
| CAN | TJA1042-class transceiver; product driver not enabled |
| Sensors | AHT20 temperature/humidity, LSM6DS3TR-C 6-axis IMU |
| Expansion | Raspberry Pi 40-pin, PMOD, 3.3 V level shifting |
| Debug | Onboard KitProg3, SWD/JTAG, trace, debug UART |

> BSP and schematic material may use `CYW55512`, `CYW55513`, or other CYW5551x names. Firmware selection must follow the actual module marking, chip ID, and the validated CYW55500A1 firmware combination.

### 3.5 Hardware data paths

```text
                        +---------------------------------------------+
                        |          PSoC Edge E84 (BGA-220)            |
                        |  Cortex-M55 400 MHz | Cortex-M33 200 MHz  |
                        |  Ethos-U55 | GCNano Ultra V | DC8000Nano   |
                        +----+-------------+-------------+------------+
                             |             |             |
                        MIPI-DSI       I2S/TDM       SDIO / UART
                             |             |             |
                       480x800 LCD    ES8388 Codec   CYW5551x Wi-Fi/BT
                                       + MD8002 amp
                             |             |             |
                        Touch I2C      Speaker       Wi-Fi / Classic BT
                                                       LE Audio / Auracast

                        QSPI NOR 16 MiB  +  HyperRAM 16 MiB  +  microSD
```

- **Display**: LVGL renders to an RGB565 framebuffer → DC8000Nano reads it → MIPI-DSI → 480×800 panel.
- **Audio output**: local files/UAC/A2DP Sink/LE Audio/AirPlay2 → `sound0` → I2S/TDM → ES8388 → MD8002 → speaker.
- **Audio capture**: PDM microphone → M55 recorder/UAC microphone → USB host or WAV file.
- **Bluetooth audio**: M33 BTstack ↔ HCI UART ↔ CYW5551x; A2DP/LE Audio frames cross to M55 through shared-SRAM rings.
- **Wi-Fi**: M55 WHD + lwIP ↔ SDIO ↔ CYW5551x.

### 3.6 Custom hardware (design stage)

`docs/board/PSOC-Edge-E84/` also contains Core expansion architecture, pin allocation, USB+PMU daughterboard, and custom-baseboard constraints. These are design inputs for later hardware iterations and **do not mean the custom boards have been fabricated, tested, or produced**.

---

## 4. Software framework

### 4.1 Software layers

```text
+-----------------------------------------------------------------------+
| Application   FeatherTalk Shell / Media / Settings / Files / Gallery / |
|               Recorder / Bluetooth UI / System pages                  |
+-----------------------------------------------------------------------+
| Services      audio arbitration / USB UAC+MSC / storage / Wi-Fi /     |
|               Bluetooth services / KWS / AirPlay2 / preferences       |
+-----------------------------------------------------------------------+
| Middleware    LVGL 9.2 + VG-Lite / RT-Thread 5.0.2 / BTstack /        |
|               lwIP + WHD / minimp3 / FAAD2 / ALAC / mbedTLS           |
+-----------------------------------------------------------------------+
| Drivers       LCD/DSI / I2S/TDM / PDM / SDIO / SDHC / USB / IPC /     |
|               GPIO / PWM / I2C / ADC                                   |
+-----------------------------------------------------------------------+
| Hardware      PSoC Edge E84 + CYW5551x + ES8388 + MD8002 + LCD + SD   |
+-----------------------------------------------------------------------+
```

### 4.2 Core responsibilities

| Area | M33 control core | M55 application core |
| --- | --- | --- |
| System | Board initialization, M55 startup, version and status supervision | Product applications, UI, and primary business services |
| Wireless | BTstack, Classic Bluetooth, BLE, LE Audio, Auracast, HCI UART | WHD, lwIP, Wi-Fi scan/connect, AirPlay2 |
| Audio | A2DP/ISO transport and cross-core audio production/consumption | SBC/LC3/AAC/ALAC processing, playback, recording, and `sound0` output |
| User features | Bluetooth state and control commands | LVGL, USB UAC/MSC, filesystems, gallery, settings, and persistence |
| Shared radio | Bluetooth owner in Radio Manager | Wi-Fi owner in Radio Manager |

### 4.3 Boot chain and images

```text
Secure M33
  Secure boot, signed images, and boot policy
        |
        v
M33 Non-Secure / RT-Thread
  Board init -> start M55 -> Bluetooth Host / Radio Manager / IPC
        |
        +---------------- IPC Pipe + shared SRAM ----------------+
        |                                                      |
        v                                                      v
M55 / RT-Thread                                      CYW5551x radio module
  LVGL / Wi-Fi / USB / storage / audio / AirPlay2     Wi-Fi: M55 WHD + SDIO
                                                     Bluetooth: M33 BTstack + HCI UART
```

When updating both cores, **program M55 first, then the signed M33 merged image containing Secure M33**. The final M33 reset completes the boot chain and releases M55.

### 4.4 IPC and shared memory

- **IPC ABI**: fixed-width messages, HELLO/HEARTBEAT, system state, quick controls, audio events, and Bluetooth Source snapshots. Incompatible changes require an ABI version increment.
- **Cross-core audio rings**: `FT_ALINK` (M33 → M55) and `FT_ALINK2` (M55 → M33) in shared SRAM; producer/consumer maintain `wr`/`rd` with cache clean/invalidate and doorbells.
- **Bluetooth Source snapshot**: M33 publishes scan results, connection state, peer address, and names with a seqlock; M55 UI is read-only.
- **Radio Manager**: Wi-Fi and Bluetooth hold separate claims; start, stop, reset, and error counters do not overwrite each other.

### 4.5 Audio architecture

**Output arbitration**: local speaker playback, UAC, A2DP Sink, LE Audio, and AirPlay2 claim `sound0` through an owner interface, preventing unrestricted simultaneous output.

```text
Local file ──► player ──┬─► sound0 ──► I2S/TDM ──► ES8388 ──► speaker
                        └─► player_bt_tap ──► SBC encode ──► ring2 ──► M33 ──► A2DP headset

USB host ──► UAC ring ─┬─► sound0 ──► speaker
                       └─► UAC tap ──► SBC encode ──► ring2 ──► M33 ──► A2DP headset

Phone/tablet ──► M33 A2DP Sink ──► ring1 ──► SBC decode ──► sound0 ──► speaker
Phone/tablet ──► M33 LE Audio ──► ring1 ──► LC3 decode ──► sound0 ──► speaker
AirPlay2 ──► network/RTP ──► AAC/ALAC decode ──► sound0 ──► speaker
```

**A2DP Source input selection**:

- The encoder reads either the UAC tap or the `player_bt_tap`, never both.
- The Media page “Bluetooth” output and the Bluetooth page “Source: Local” selector stay in sync; “Speaker” selects USB audio.
- USB forwarding only mutes the UAC worker's local `sound0` path; the encoder itself does not hold `sound0`.
- The encoder is fixed at 48 kHz / Joint Stereo / 16 blocks / 8 subbands / Loudness, with bitpool following the headset negotiation.

### 4.6 Display and graphics pipeline

The current product UI is the LVGL 9.2 FeatherTalk Shell on M55:

- LVGL draw tasks are collected per frame and encoded into a VG-Lite command stream, normally approaching one GPU submission per frame.
- Two RGB565 framebuffers serve both LVGL rendering and DC scanout, avoiding an additional full-frame copy.
- VG-Lite, refresh, and font hot code is placed in M55 ITCM; cold pages and business code remain in external XIP.
- GPU, refresh, scanout, heap, and page-state diagnostics are available; `feather_ui_bench` runs a 60-frame full-screen benchmark.

`libraries/FeatherUI` and `applications/gpu_ui` remain native-GPU experiments and comparison paths; they are not the default product Shell.

### 4.7 Wireless and protocol stacks

- **Classic Bluetooth / BLE / LE Audio / Auracast**: BTstack product integration on M33; HCI over UART to CYW5551x.
- **Wi-Fi**: WHD + lwIP on M55 over SDIO to the same module.
- **Coexistence**: independent Wi-Fi/Bluetooth claims, reset, and error counters; A2DP, LE Audio unicast, and Auracast are mutually exclusive by audio path.
- **Pairing persistence**: Classic link keys and LE bonds are stored in user Flash and restored after reboot.

### 4.8 Storage and filesystems

- `/flash`: trailing 2 MiB of QSPI NOR, persistent user volume.
- `/sdcard`: microSD with FAT filesystem and hot-plug detection.
- File operations: browse, copy/paste, rename, delete, and format.
- USB MSC exposes either volume to a PC; a cross-core guard protects M33 XIP during M55 erase/program.

### 4.9 Local keyword spotting and AI

- PDM capture → sliding-window features → “Xiaomai” model inference → M33 event/LED.
- Capture, training, validation tools, and a two-stage verifier; models and thresholds are replaceable per version.
- Ethos-U55 benchmarks use raw-register command streams to measure HyperRAM/Flash weight-read and NPU data-path bandwidth.

### 4.10 Build and tooling

- `tools/freather/build-demo.cmd`: dual-core build, signing, and merge.
- `tools/freather/flash-feathertalk.cmd`: M55 → M33 program, verify, and reset.
- `tools/freather/serial-monitor.cmd`: serial monitor and scripted commands.
- `tools/freather/tests/`: AirPlay2, LC3, UI navigation, radio lifecycle, Wi-Fi TCP, and audio tests.
- `tools/freather/benchmark-ui-scenes.py`: 21 pages / 32 scenes of UI and GPU benchmark.

---

## 5. Repository layout

| Directory | Purpose |
| --- | --- |
| `projects/Edgi_Talk_*` | Official standalone examples for drivers, IPC, USB, LVGL, Wi-Fi, DEEPCRAFT, and more |
| `projects/FeatherTalk_M33` | Control-core firmware: Bluetooth, board control, M55 startup, and IPC |
| `projects/FeatherTalk_M55` | Application-core firmware: UI, Wi-Fi, USB, storage, audio, and AirPlay2 |
| `projects/FeatherTalk_Common` | Shared IPC ABI, version information, audio rings, and runtime radio resource manager |
| `bluetooth/port_rtthread` | Product BTstack integration, Classic Bluetooth, and LE Audio services on M33 |
| `bluetooth/reference/btstack` | BTstack submodule |
| `libraries/HAL_Drivers` | Board drivers including I2S, IPC, LCD, PDM, and SPI |
| `libraries/FeatherUI` | Native-GPU UI experiments and shared components; the product UI uses the M55 LVGL Shell |
| `tools/freather` | Product build, signing, programming, serial, test, and diagnostic tools |
| `docs` | Board design, wireless, audio, AirPlay2, and performance records |
| `rt-thread` | RT-Thread kernel and components |

Official examples remain usable as standalone SDK projects. FeatherTalk-specific configuration, build logic, and application code are primarily isolated in `FeatherTalk*`, `bluetooth/port_rtthread`, and `tools/freather`.

---

## 6. Development status and progress

Status baseline: **2026-11-01, `product/edgi-talk`**.

The project is at **“one integrated dual-core demo baseline completed; real-device compatibility and long-duration validation in progress”**. This is not a claim of product certification for every feature.

### 6.1 Completed development stages

1. Established dual-core execution, IPC, build/program tooling, and product diagnostics.
2. Built the LVGL product Shell, storage, gallery, playback, recorder, and optimized GPU path.
3. Implemented Classic Bluetooth pairing, A2DP Sink, A2DP Source, and pairing persistence.
4. Implemented LE Audio unicast, the LC3 pipeline, VCS, and an Auracast broadcast source.
5. Added independent Wi-Fi/Bluetooth resource management, dual-mode audio-route control, and Wi-Fi performance work.
6. Ported AirPlay2 protocol, encryption, decoding, and output adaptation into a testable research prototype.
7. Built the local KWS data path, training and board-validation tools, and Ethos-U55 weight-bandwidth benchmarks.
8. Integrated these modules into one M33/M55 firmware set and completed build, signing, programming, and basic board checks.

### 6.2 Latest round (2026-11-01)

- Added speaker/Bluetooth output selection to the media player and a dedicated `player_bt_tap` path from local files to A2DP Source.
- Added the scan-result list, connected-device information, and “Source: USB/Local” selector to Bluetooth settings.
- Aligned the A2DP Source capability set with the M55 encoder and made SBC bitpool follow the headset negotiation, addressing a possible sink stall when max bitpool differs.
- Added Classic remote-name resolution and corrected MAC address byte order.
- M33/M55 builds pass, OpenOCD program/verify passes, and media/Bluetooth pages plus the serial-equivalent playback/source-switch paths were regressed.
- Real-headphone A2DP listening and long-duration playback still need retesting; the current environment lacks a stable discoverable Classic headset.

### 6.3 Board checks for the AirPlay2 integrated image before the KWS merge

- Both M55 and M33 images were programmed and verified with OpenOCD.
- IPC traffic is live with zero application-side errors in the checked session.
- The UI and GPU path initialize without observed overflow or scanout timeout.
- Bluetooth is available, Wi-Fi is ready, and a real Wi-Fi scan succeeds.
- Two Auracast control start/stop cycles complete, and the cross-core audio ring reports ready with zero errors.
- AirPlay2 starts in an enabled state; without an AP connection it waits for network availability and does not claim the audio device.

These are integration smoke checks, not substitutes for the external-device acceptance items below.

### 6.4 Next priorities

1. Regression-test A2DP, LE Audio, volume control, reconnect, and long-duration playback with real phones/headphones.
2. Validate AirPlay2 pairing, continuous music, PTP synchronization, and reconnect with real iOS/macOS senders.
3. Regression-test USB UAC/MSC, local playback, recording, gallery, and storage persistence.
4. Measure Bluetooth-audio continuity, Wi-Fi throughput, packet loss, and stability with Wi-Fi connected and both radios active.
5. Freeze a release configuration and complete an explicit acceptance checklist; FPGA/Smart I/O experiments do not block the current integrated-demo baseline.

---

## 7. Build, program, and run

### 7.1 Prerequisites

The recommended Windows PowerShell flow uses the repository-owned `tools/freather` toolchain:

- Arm GNU Toolchain 13.3.Rel1
- SCons 4.10.1 and bundled Python
- Infineon Customized OpenOCD
- KitProg3 debugger and a 115200 8N1 serial console

After cloning:

```powershell
git lfs install
git lfs pull --include="tools/freather/**"
git submodule update --init --recursive
```

The official standalone examples also support RT-Thread Studio 2.2.9 or later. The command-line flow below is recommended for FeatherTalk.

### 7.2 Build the integrated demo

From the repository root:

```powershell
.\tools\freather\build-demo.cmd FeatherTalk_M55 -Clean
.\tools\freather\build-demo.cmd FeatherTalk_M33 -Clean
```

Final programming images:

```text
projects/FeatherTalk_M55/rtthread.hex
projects/FeatherTalk_M33/build/rtthread.hex
```

An intermediate M33 HEX elsewhere in the project is not a substitute for the signed, relocated, merged image under `build/`.

### 7.3 Program the integrated demo

```powershell
.\tools\freather\flash-feathertalk.cmd
```

The script programs and verifies **M55 first, then M33**, and finally resets the board. This firmware operation should not be interpreted as formatting the trailing user-data volume.

### 7.4 Serial checks

```powershell
.\tools\freather\serial-monitor.cmd --list
.\tools\freather\serial-monitor.cmd --port COM17 --baud 115200
```

Common M55 product-shell commands:

```text
feather_m55_status
bt_status
ft_wifi
ft_wifi scan
ft_airplay
feather_ui_status
ft_audio_stats
kws info
kws testevt
bt_bcast_start
bt_bcast_stop
```

Audio/Bluetooth diagnostics:

```text
feather_bt_source status
feather_bt_source input usb|player
feather_bt_source scan
feather_player status
feather_player scan
feather_player output speaker|bt
feather_player play <index>
ft_sbc_enc_stats
```

The M33 UART5 diagnostic console provides `feather_status`, `feather_ping`, and lower-level Bluetooth diagnostics. Wi-Fi credentials are not saved, and AirPlay2 does not autonomously join an AP. AirPlay2 pairing also requires the random-seed provisioning procedure described in its porting document.

### 7.5 Quick tour

1. Boot to the desktop, swipe through the app list, notification shade, and settings.
2. Open **Media**, choose an SD-card folder, play WAV/MP3, and observe the 3D cover flow, album art, and progress.
3. Open **Settings > Bluetooth**, select A2DP SOURCE, scan and connect a headset; choose **Bluetooth** on the Media page or switch the Bluetooth source to **Local**.
4. Connect a USB host and verify UAC speaker/microphone, USB MSC, and USB-to-Bluetooth forwarding.
5. Open **Settings > Wi-Fi**, scan/connect an AP, and use `ft_wifi`/`ft_airplay` to inspect network and AirPlay2 state.
6. Say “Xiaomai” to verify local wake-word detection; run `feather_ui_bench` for the GPU/refresh benchmark.

---

## 8. Tests and validation

### 8.1 Host-side tests

These tests use the bundled Python and exercise production C/C++ sources or their adapters:

```powershell
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_protocol.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_crypto.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_alac.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_aac.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_lc3_watermark.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/check-ui-navigation-map.py
```

See `docs/LE_AUDIO_REPAIR_20260913.md` for the LE Audio wire-test build command. Radio-lifecycle, Wi-Fi TCP, and AirPlay2 board scripts are under `tools/freather` and `tools/freather/tests`; read their arguments and hardware prerequisites before running them.

### 8.2 Board checks

| Type | Content | Entry point |
| --- | --- | --- |
| Build/program | M33/M55 dual-core build, signed merge, OpenOCD write and verify | `build-demo.cmd`, `flash-feathertalk.cmd` |
| Boot/IPC | Dual-core boot, IPC heartbeat, error counters, shared memory | `feather_m55_status`, `bt_status` |
| UI/GPU | 21 pages, 32 scenes, 60-frame benchmark, scanout/heap stats | `feather_ui_scene`, `feather_ui_bench`, `benchmark-ui-scenes.py` |
| Audio | Local playback, pause/resume/stop, output switch, SBC stats | `feather_player`, `ft_sbc_enc_stats` |
| Bluetooth | Scan, pairing, A2DP Sink/Source, LE Audio, Auracast | `feather_bt_source`, `bt_bcast_start/stop` |
| Wireless | Wi-Fi scan/connect, TCP, Bluetooth lifecycle | `ft_wifi`, `test-wifi-tcp.py`, `test-radio-lifecycle.py` |
| Storage | SD/Flash mount, file operations, format, USB MSC | UI files/storage pages |
| KWS/NPU | Wake-word path, event integration, NPU bandwidth | `kws`, `npu_bench` |

Host unit tests, simulated network-audio tests, and startup checks do not replace acceptance with real senders, headphones, USB hosts, or long-duration playback.

---

## 9. Open-source software and third-party components

FeatherTalk builds on several open-source projects and vendor SDKs. The table is a component inventory for submission; the license terms of each upstream project govern.

| Component | Purpose | License/notes |
| --- | --- | --- |
| RT-Thread | RTOS, driver framework, FinSH | Apache-2.0 |
| LVGL | Product UI, widgets, animation | MIT |
| BTstack | Classic Bluetooth, BLE, LE Audio, A2DP/AVDTP/AVRCP | BlueKitchen BTstack license; review before commercial use |
| lwIP | TCP/IP stack | BSD-3-Clause |
| WHD | CYW5551x Wi-Fi driver | Infineon license |
| minimp3 | MP3 decoding | CC0 |
| FAAD2 | AAC decoding | GPL-2.0-or-later |
| ALAC | Apple Lossless decoding | Apache-2.0 (per upstream) |
| AirPlay2 reference | Protocol, crypto, RTP, ALAC/AAC | Referenced ESP code includes a Non-Commercial License |
| Infineon PDL/HAL | PSoC Edge peripheral drivers | Infineon license |

The AirPlay2 directory preserves its upstream notices. FAAD2's GPL terms and the AirPlay2 reference code's non-commercial terms must be reviewed separately before distribution or commercial use; a successful build is not a commercial license grant.

---

## 10. Demo video (to be recorded)

Suggested 3–5 minute script covering the main submission capabilities:

1. **0:00–0:30 Boot and desktop**: dual-core startup, home, app list, notification shade, settings.
2. **0:30–1:30 Music player**: SD-card tracks, 3D cover flow, album art, play/pause/next, speaker output.
3. **1:30–2:30 Bluetooth headphones**: SOURCE role, scan list, name/MAC, connect, USB/local source switch, local-file playback to the headset.
4. **2:30–3:00 USB**: UAC speaker/microphone, USB MSC, USB-to-Bluetooth forwarding.
5. **3:00–3:45 Wireless**: Wi-Fi scan/connect, TCP status, AirPlay2 research prototype.
6. **3:45–4:15 LE Audio/Auracast**: unicast connection, broadcast start/stop.
7. **4:15–5:00 KWS/NPU**: wake word, event integration, NPU bandwidth benchmark, UI/GPU benchmark data.

After recording, place the link or file in this section and note resolution, duration, and demo environment.

---

## 11. Documentation index

- [2026 Embedded Competition article](docs/FeatherTalk_发布稿_2026嵌入式大赛.md)
- [Dual-core build and board workflow](projects/FeatherTalk_Common/BRINGUP_zh.md)
- [Wi-Fi / Bluetooth runtime resource management](projects/FeatherTalk_Common/RADIO_COEXISTENCE_zh.md)
- [M55 display and GPU architecture](projects/FeatherTalk_M55/README_zh.md)
- [LE Audio repair and validation record](docs/LE_AUDIO_REPAIR_20260913.md)
- [Wi-Fi performance alignment](docs/WIFI_PERFORMANCE_ALIGNMENT_20260913.md)
- [AirPlay2 porting record](docs/AIRPLAY2_PORTING_zh.md)
- [KWS board-validation guide](projects/FeatherTalk_M55/KWS_VERIFY_zh.md)
- [Ethos-U55 NPU bandwidth benchmark](projects/FeatherTalk_M55/applications/npu_bench/README.md)
- [Edgi-Talk Core requirements and expansion architecture](docs/board/PSOC-Edge-E84/Edgi-Talk_Core_系统需求与架构.md)
- [Edgi-Talk schematic analysis](docs/board/PSOC-Edge-E84/Edgi-Talk_原理图分析.md)
- [PSE84 SoC/GPU/display and memory layout](docs/board/PSOC-Edge-E84/PSE84_SOC_GPU2D_zh.md)
- [FPGA SPI-I2S audio-bridge design](docs/FPGA_SPI_I2S_AUDIO_BRIDGE.md)
- [PSE84 build, programming, and serial workflow](tools/freather/PSE84_WORKFLOW.md)

Several documents preserve chronological debugging and interim results. Read their latest sections together with current code and the status table above; historical open items are not automatically the current state.

---

## 12. Licensing and scope

- The official SDK, RT-Thread, BTstack, and third-party components retain their respective licenses.
- AirPlay2 third-party notices are preserved: the referenced ESP code includes a **Non-Commercial License**, and FAAD2 uses **GPL-2.0-or-later**. Review these terms before commercial use or distribution; a successful build is not a commercial license grant.
- AirPlay2 is a research prototype, not an Apple certification claim. It does not claim persistent HomeKit pairing, classic RSA/FairPlay, or unimplemented codec capabilities.
- Hardware architecture and daughterboard documents are design inputs, not evidence that custom hardware has been fabricated, validated, or released to production.
- Real-headphone A2DP Source playback and long-duration stability, as well as full LE Audio/Auracast/AirPlay2 compatibility, remain to be accepted per Section 6.4.
