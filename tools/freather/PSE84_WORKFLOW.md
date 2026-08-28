# PSE84 build, program, and console workflow

All product-owned host tools stay under `tools\freather`. SDK-native tool
directories remain unchanged; only normal ignored build outputs are generated.

## 1. Build

```powershell
.\tools\freather\build-demo.cmd Edgi_Talk_M33_Template
```

The wrapper supplies the external Arm GNU Toolchain 13.3.Rel1 and SCons 4.10.1
to the SDK's native `SConstruct`. For the M33 template, it then builds the
secure firmware, signs/relocates the application with the SDK's bundled
Edge Protect tools, and produces the merged image:

```text
sdk-bsp-psoc_e84-edgi-talk\projects\Edgi_Talk_M33_Template\build\rtthread.hex
```

Do not program the similarly named `rtthread.hex` in the project root. That is
an intermediate application image, not the final signed and merged image.

Use `-Clean` when a clean SCons rebuild is required:

```powershell
.\tools\freather\build-demo.cmd Edgi_Talk_M33_Template -Clean
```

## 2. PyOCD status

The official `Infineon.PSE8xxx_DFP.1.1.0.pack` is stored under
`tools\freather\cmsis-packs`. PyOCD 0.45.1 can parse it and identify the exact
`pse846gps2dbzc4a` target.

Two current limitations prevent safe programming of this board image with
PyOCD:

1. The board's KitProg3 requires an Infineon-specific PSE84 acquire command.
   PyOCD uses the generic CMSIS-DAP probe driver and receives `No ACK` before
   the Pack's normal debug sequences can open the target.
2. The Pack marks `PSE84_SMIF.FLM` as a non-default external flash algorithm.
   PyOCD does not automatically enable non-default external algorithms, while
   the merged image contains SMIF addresses.

A safe diagnostic (with automatic unlock/erase disabled) is available:

```powershell
.\tools\freather\flash-demo.cmd Edgi_Talk_M33_Template -Programmer PyOCD
```

It deliberately stops before programming. `Auto` therefore selects Infineon's
customized OpenOCD for this board.

## 3. Program and verify

```powershell
.\tools\freather\flash-demo.cmd Edgi_Talk_M33_Template
```

The script uses the official Infineon OpenOCD PSE84 target, KitProg3 driver,
PSE84 RRAM/SMIF flash algorithms, and the board-generated `qspi_config.cfg`.
It performs sector erase only for addresses present in the image, programs the
image, verifies it, then resets the M33 to run.

To select the programmer explicitly:

```powershell
.\tools\freather\flash-demo.cmd Edgi_Talk_M33_Template -Programmer OpenOCD
```

## 4. Serial console and MSH

List serial ports:

```powershell
.\tools\freather\serial-monitor.cmd --list
```

Open the current board port at the SDK default of 115200 8N1:

```powershell
.\tools\freather\serial-monitor.cmd --port COM17 --baud 115200 --timestamps
```

For an unattended MSH check:

```powershell
.\tools\freather\serial-monitor.cmd --port COM17 --baud 115200 --send help --send version --no-input --duration 10 --timestamps
```

The M33 template documentation notes that its UART may require the board's
external USB-to-UART connection rather than the KitProg3 virtual serial port.
If COM17 stays silent after reset, verify the M33 TX/RX/GND routing described by
the board/demo documentation.

On the hardware used to validate this workflow, `COM17` (`VID:PID 04B4:F155`)
worked directly: `help` listed the shell commands, `version` reported
RT-Thread 5.0.2, and `ps` listed the live `tshell` and application threads.
