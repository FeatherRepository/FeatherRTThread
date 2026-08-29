# FeatherTalk M33 Product Project

[**中文**](./README_zh.md) | **English**

## Introduction

This product project is derived from **Edgi_Talk_M33_Template** and runs a minimal **RT-Thread** application on the Cortex-M33 core.
It initializes the board, boots the Cortex-M55 core, and remains active as the product control and IPC-supervisor core with optional peripheral demos disabled.

The shared Secure M33 firmware package lives under `libraries/components/infineon-pse84-secure-firmware-latest`, while keeping the Template `.config` minimal.

## Software Description

* The project is developed based on the **Edgi-Talk** platform.
* `SOC_Enable_CM55` is enabled so board initialization starts the M55 core.
* `SOC_Enable_CM33_DeepSleep` is disabled so M33 continues into RT-Thread and the product MSH after starting M55.
* AHT20, LSM6DS3, audio, ADC, RTC, SD card, filesystem, LCD, Wi-Fi, and other optional peripheral demos are disabled.
* External Wi-Fi/audio power control pins are driven low during board initialization.

## Usage Instructions

### Compilation and Download

1. Open the project and complete the compilation.
2. Connect the board’s USB port to the PC using the **onboard debugger (DAP)**.
3. Use the programming tool to flash the generated firmware to the development board.

### Runtime Behavior

* After flashing, power on the board to run the example project.
* M33 initializes RT-Thread, starts M55, runs the IPC supervisor, and keeps the product MSH on UART5.
* The template does not blink LEDs or start peripheral demos automatically.

## Notes

> **⚠️ Note:** This project requires **RT-Thread Studio 2.2.9** or higher.

* The M33 project serial log is not output through the onboard DAP virtual COM port directly. To view `msh />` and demo logs, connect an external USB-to-UART adapter, such as CH340.
The connection position is shown below. Connect the board RX to the UART adapter TX, connect the board TX to the UART adapter RX, and set the host serial baud rate to 115200:

![alt text](figures/m33_uart.png)

* To modify the **graphical configuration** of the project, open the configuration file using the following tool:

```
tools/device-configurator/device-configurator.exe
libs/TARGET_APP_KIT_PSE84_EVAL_EPC2/config/design.modus
```

* After modification, save the configuration and regenerate the code.
* When updating both images, program **FeatherTalk_M55 first** and the signed **FeatherTalk_M33 last**. The final M33 reset initializes the board and starts M55.
* Use `feather_status` and `feather_ping` on the M33 MSH to inspect or probe M55.

## Boot Sequence

The system boot sequence is as follows:

```
+------------------+
|   Secure M33     |
|   (Secure Core)  |
+------------------+
          |
          v
+------------------+
|       M33        |
| (Non-Secure Core)|
+------------------+
          |
          v
+-------------------+
|       M55         |
| (Application Core)|
+-------------------+
```

⚠️ Please strictly follow the boot sequence above when flashing firmware; otherwise, the system may not run properly.

---

* If the M55 application does not run correctly, compile and flash **FeatherTalk_M33** first to ensure proper initialization and core startup.
* To enable the M55 core, configure the **M33 project** as follows:

  ```
  RT-Thread Settings --> Hardware --> select SOC Multi Core Mode --> Enable CM55 Core
  ```
