# Edgi-Talk PSoC Edge E84 SDK and FeatherTalk

[**中文**](./README_zh.md) | **English**

This repository contains two layers:

1. **The Edgi-Talk BSP/SDK**: RT-Thread board support, drivers, and standalone peripheral examples for the PSoC Edge E84.
2. **The FeatherTalk product application**: a dual-core integrated application built on the same SDK, combining LVGL, audio, USB, Wi-Fi, Classic Bluetooth, LE Audio, Auracast, local keyword spotting, NPU benchmarks, and an AirPlay2 research prototype.

The product branch is `product/edgi-talk`. The FeatherTalk integrated demo is not another single-core example: it consists of the paired **`FeatherTalk_M33` and `FeatherTalk_M55` projects and their shared modules**.

![Edgi-Talk](docs/figures/board_large.jpg)

## 1. Project architecture

### 1.1 SDK and product boundaries

| Directory | Purpose |
| --- | --- |
| `projects/Edgi_Talk_*` | Official standalone examples for drivers, IPC, USB, LVGL, Wi-Fi, DEEPCRAFT, and more |
| `projects/FeatherTalk_M33` | Control-core firmware: Bluetooth, board control, M55 startup, and IPC |
| `projects/FeatherTalk_M55` | Application-core firmware: UI, Wi-Fi, USB, storage, audio, and AirPlay2 |
| `projects/FeatherTalk_Common` | Shared IPC ABI, version information, and runtime radio resource manager |
| `bluetooth/port_rtthread` | Product BTstack integration, Classic Bluetooth, and LE Audio services on M33 |
| `bluetooth/reference/btstack` | BTstack submodule |
| `libraries/HAL_Drivers` | Board drivers including I2S, IPC, LCD, PDM, and SPI |
| `libraries/FeatherUI` | Native-GPU UI experiments and shared components; the product UI uses the M55 LVGL Shell |
| `tools/freather` | Product build, signing, programming, serial, test, and diagnostic tools |
| `docs` | Board design, wireless, audio, AirPlay2, and performance records |
| `rt-thread` | RT-Thread kernel and components |

The official examples remain usable as standalone SDK projects. FeatherTalk-specific configuration, build logic, and application code are primarily isolated in the directories listed above.

### 1.2 Boot chain and firmware images

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

When updating both cores, **program M55 first, then program the signed M33 merged image containing Secure M33**. The final M33 reset completes the boot chain and releases M55.

## 2. FeatherTalk runtime architecture

### 2.1 Core responsibilities

| Area | M33 control core | M55 application core |
| --- | --- | --- |
| System | Board initialization, M55 startup, version and status supervision | Product applications, UI, and primary business services |
| Wireless | BTstack, Classic Bluetooth, BLE, LE Audio, Auracast, HCI UART | WHD, lwIP, Wi-Fi scan/connect, AirPlay2 |
| Audio | A2DP/ISO transport and cross-core audio production/consumption | SBC/LC3/AAC/ALAC processing, playback, recording, and `sound0` output |
| User features | Bluetooth state and control commands | LVGL, USB UAC/MSC, filesystems, gallery, settings, and persistence |
| Shared radio | Bluetooth owner in Radio Manager | Wi-Fi owner in Radio Manager |

### 2.2 IPC and audio paths

- **IPC ABI**: fixed-width messages, HELLO/HEARTBEAT, system state, quick controls, and audio events. Incompatible changes require an ABI version increment.
- **Cross-core audio link**: a shared-SRAM ring with doorbell notifications carries Bluetooth audio frames, formats, and state.
- **Audio output arbitration**: local playback, UAC, A2DP, LE Audio, and AirPlay2 claim `sound0` through an owner interface; unrestricted simultaneous playback is not a design goal.
- **Radio resource management**: Wi-Fi and Bluetooth have independent claims, states, reset counters, and errors. A normal feature stop does not power down the other feature or their shared supply.
- **Shared Flash guard**: before M55 erases the user Flash volume, a cross-core guard prevents M33 from continuing to execute stale cached code from the same SMIF device.

### 2.3 Display pipeline

The current product UI is the LVGL 9.2 FeatherTalk Shell on M55:

- Adaptive Chinese/English pages, navigation, settings, files, gallery, playback, recording, and Bluetooth controls.
- LVGL draw tasks are collected per frame and encoded into a VG-Lite command stream, normally approaching one GPU submission per frame.
- Two RGB565 framebuffers serve both LVGL rendering and DC scanout, avoiding an additional full-frame copy.
- GPU, refresh, scanout, heap, and page-state diagnostics are available through the shell.

`libraries/FeatherUI` and `applications/gpu_ui` remain native-GPU experiments and comparison paths; they are not the default product Shell.

## 3. Features and development work

| Area | Developed functionality | Current status and limits |
| --- | --- | --- |
| Dual-core foundation | Boot flow, versioned IPC, heartbeat, cross-core audio ring, shared-Flash and radio resource management | Integrated; current image passes basic startup and IPC checks |
| Product UI | Bilingual Shell, gestures, local playback, recorder, files, gallery, storage, settings, and Bluetooth pages | Integrated; UI, GPU, and navigation checks pass, with full interaction regression still required per release |
| GPU and display | VG-Lite batching, dual-framebuffer direct scanout, backlight control, and benchmarks | Dedicated board results exist; current basic checks show no overflow or scanout timeout |
| Local audio and USB | Local playback, recording, bidirectional UAC, dynamic rate/clock calibration, USB MSC, and USB-to-Bluetooth forwarding | Developed with dedicated test records; current version still needs external USB-host and listening regression |
| Classic Bluetooth | BTstack, discoverability, SSP pairing, link-key persistence, A2DP Sink/Source, role switching, reconnect, and AVRCP fixes | End-to-end playback and forwarding have been demonstrated; broader phone compatibility and long-duration tests remain |
| LE Audio | BAP Unicast Server, dual CIS, LC3, PACS/ASCS/CAS/TMAP/VCS, and LE bond persistence | Real phone connection and audio have been demonstrated; long-term continuity, more phones, and full compliance are not claimed |
| Auracast | BIG/BIS broadcast source, periodic advertising, lifecycle, and unicast arbitration | BIG/BIS data path is implemented; current integrated image passes two control start/stop cycles, with receiver listening tests still required |
| Wi-Fi and coexistence | Dual-band scan/connect, independent radio lifecycle, performance optimization, TCP and memory diagnostics | Dual-band short tests and performance records exist; current image passes a real scan, while long-term throughput and audio concurrency remain |
| AirPlay2 | mDNS, RTSP/HTTP, HAP transient pairing, session/RTP encryption, ALAC/AAC-LC, type96/type103, and `sound0` output | Research prototype; protocol and simulated network-audio tests pass, while real iPhone playback, PTP synchronization, and long-duration acceptance are incomplete |
| Local keyword spotting | Self-trained “Xiaomai” model, PDM microphone, sliding-window inference, autostart, M33 event integration, LED feedback, capture/training/validation tools, and a two-stage verifier | The end-to-end board path has been demonstrated; models are still evolving, so real-speaker recall and false-accept rates must be accepted per model version |
| Ethos-U55 NPU | Raw-register command streams, HyperRAM/Flash weight-read bandwidth benchmarks, and model-generation tools | Research and measurement tooling; not a completed local LLM inference feature |
| Build and diagnostics | Dual-core build, signed merge, OpenOCD program/verify, serial tools, supplementary PyOCD path, and UI/radio/audio tests | Integrated and used for the current build and board checks |
| Custom hardware | Core expansion architecture, pin allocation, USB+PMU boards, and design constraints | Requirements/design stage only; not evidence of production or hardware validation |
| SPI-to-I2S | FPGA audio-bridge design material and analysis of the SoC SPI + Smart I/O route | Not included in the integrated firmware; Smart I/O driver development has not started |

## 4. Development status and progress

Status baseline: **2026-10-31, `product/edgi-talk`**.

The project is at **“one integrated dual-core demo baseline completed; real-device compatibility and long-duration validation in progress”**. This is not a claim of product certification for every feature.

### 4.1 Completed development stages

1. Established dual-core execution, IPC, build/program tooling, and product diagnostics.
2. Built the LVGL product Shell, storage, gallery, playback, recorder, and optimized GPU path.
3. Implemented Classic Bluetooth pairing, A2DP Sink, A2DP Source, and pairing persistence.
4. Implemented LE Audio unicast, the LC3 pipeline, VCS, and an Auracast broadcast source.
5. Added independent Wi-Fi/Bluetooth resource management, dual-mode audio-route control, and Wi-Fi performance work.
6. Ported AirPlay2 protocol, encryption, decoding, and output adaptation into a testable research prototype.
7. Built the local KWS data path, training and board-validation tools, and Ethos-U55 weight-bandwidth benchmarks.
8. Integrated these modules into one M33/M55 firmware set and completed build, signing, programming, and basic board checks.

### 4.2 Board checks for the AirPlay2 integrated image before the KWS merge

- Both M55 and M33 images were programmed and verified with OpenOCD.
- IPC traffic is live with zero application-side errors in the checked session.
- The UI and GPU path initialize without observed overflow or scanout timeout.
- Bluetooth is available, Wi-Fi is ready, and a real Wi-Fi scan succeeds.
- Two Auracast control start/stop cycles complete, and the cross-core audio ring reports ready with zero errors.
- AirPlay2 starts in an enabled state; without an AP connection it waits for network availability and does not claim the audio device.

These are integration smoke checks, not substitutes for the external-device acceptance items below.

### 4.3 Next priorities

1. Regression-test A2DP, LE Audio, volume control, reconnect, and long-duration playback with real phones.
2. Validate AirPlay2 pairing, continuous music, PTP synchronization, and reconnect with real iOS/macOS senders.
3. Regression-test USB UAC/MSC, local playback, recording, gallery, and storage persistence.
4. Measure Bluetooth-audio continuity, Wi-Fi throughput, packet loss, and stability with Wi-Fi connected and both radios active.
5. Freeze a release configuration and complete an explicit acceptance checklist; FPGA/Smart I/O experiments do not block the current integrated-demo baseline.

## 5. Build, program, and run

### 5.1 Prerequisites

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

### 5.2 Build the integrated demo

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

### 5.3 Program the integrated demo

```powershell
.\tools\freather\flash-feathertalk.cmd
```

The script programs and verifies **M55 first, then M33**, and finally resets the board. This firmware operation should not be interpreted as formatting the trailing user-data volume.

### 5.4 Serial checks

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

The M33 UART5 diagnostic console provides `feather_status`, `feather_ping`, and lower-level Bluetooth diagnostics. Wi-Fi credentials are not saved, and AirPlay2 does not autonomously join an AP. AirPlay2 pairing also requires the random-seed provisioning procedure described in its porting document.

## 6. Tests and validation

These host tests use the bundled Python and exercise production C/C++ sources or their adapters:

```powershell
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_protocol.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_crypto.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_alac.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_aac.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_lc3_watermark.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/check-ui-navigation-map.py
```

See `docs/LE_AUDIO_REPAIR_20260913.md` for the LE Audio wire-test build command. Radio-lifecycle, Wi-Fi TCP, and AirPlay2 board scripts are under `tools/freather` and `tools/freather/tests`; read their arguments and hardware prerequisites before running them.

Host unit tests, simulated network-audio tests, and startup checks do not replace acceptance with real phones, headphones, USB hosts, or long-duration playback.

## 7. Documentation index

- [Dual-core build and board workflow](projects/FeatherTalk_Common/BRINGUP_zh.md)
- [Wi-Fi / Bluetooth runtime resource management](projects/FeatherTalk_Common/RADIO_COEXISTENCE_zh.md)
- [M55 display and GPU architecture](projects/FeatherTalk_M55/README_zh.md)
- [LE Audio repair and validation record](docs/LE_AUDIO_REPAIR_20260913.md)
- [Wi-Fi performance alignment](docs/WIFI_PERFORMANCE_ALIGNMENT_20260913.md)
- [AirPlay2 porting record](docs/AIRPLAY2_PORTING_zh.md)
- [KWS board-validation guide](projects/FeatherTalk_M55/KWS_VERIFY_zh.md)
- [Ethos-U55 NPU bandwidth benchmark](projects/FeatherTalk_M55/applications/npu_bench/README.md)
- [Edgi-Talk Core requirements and expansion architecture](docs/board/PSOC-Edge-E84/Edgi-Talk_Core_系统需求与架构.md)
- [FPGA SPI-I2S audio-bridge design](docs/FPGA_SPI_I2S_AUDIO_BRIDGE.md)
- [PSE84 build, programming, and serial workflow](tools/freather/PSE84_WORKFLOW.md)

Several documents preserve chronological debugging and interim results. Read their latest sections together with current code and the status table above; historical open items are not automatically the current state.

## 8. Licensing and scope

- The official SDK, RT-Thread, BTstack, and third-party components retain their respective licenses.
- AirPlay2 third-party notices are preserved: the referenced ESP code includes a **Non-Commercial License**, and FAAD2 uses **GPL-2.0-or-later**. Review these terms before commercial use or distribution; a successful build is not a commercial license grant.
- AirPlay2 is a research prototype, not an Apple certification claim. It does not claim persistent HomeKit pairing, classic RSA/FairPlay, or unimplemented codec capabilities.
- Hardware architecture and daughterboard documents are design inputs, not evidence that custom hardware has been fabricated, validated, or released to production.
