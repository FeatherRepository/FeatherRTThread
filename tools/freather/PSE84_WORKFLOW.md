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

## 5. PyOCD programming route (supplementary, development use)

The PyOCD route supplements OpenOCD for development automation. It required a
set of board-specific workarounds that now live in this directory:

- `cmsis-packs/Infineon.PSE8xxx_DFP.1.1.0-smif-default.pack`: the official DFP
  with the SMIF/SMIF_S flash algorithms flipped to `default="1"`, so pyOCD
  builds the external flash regions (the stock DFP marks them non-default).
- `pyocd_pse84_patch.py`: runtime patches loaded by the flash driver:
  - DP BASEPTR0 address fix and APACC page enumeration for the PSE84 DPv3
    (generic pyOCD discovery misses the core APs),
  - FLM sector-range intersection guard,
  - address-adaptive AHB5-AP HNONSEC switching (secure SRAM vs NS SRAM),
  - flash region sector-size repair for the SMIF_S/RRAM_S regions.
- `pyocd_flash_pse84.py`: argparse driver (connect, program, verify, reset).
- `flash-demo-pyocd.ps1` / `.cmd`: full wrapper. It FIRST runs a short OpenOCD
  pre-warm (`init; reset halt; resume`) and kills it without `shutdown`; the
  KitProg3 firmware brings SWD up with its custom acquire sequence, which
  pyOCD cannot trigger on a freshly powered board (No ACK). Then pyOCD takes
  over the probe.

```powershell
.\tools\freather\flash-demo-pyocd.cmd Edgi_Talk_M33_Template
```

Requirements and known limits:

- pyOCD 0.45.1 lives in `tools\freather\pyocd-lib` (pip-installable
  alternative to the LFS-stored `pyocd\` copy); the bundled CPython in
  `serial-monitor\python` runs it.
- Large DAP block transfers (`DAP_TransferBlock`) wedge the KitProg3
  firmware; the driver enables `cmsis_dap.limit_packets` and disables
  deferred transfers to stay on the safe path. Expect a slower flash
  (minutes, not seconds).
- If a flash session dies abnormally the KitProg3 DAP can hang; recovery is
  a USB cable replug (a libusb device reset does not revive the firmware).
- After programming, the target may already be in RT-Thread deep sleep, so
  the in-pyOCD reset can fail with No ACK. Reset via OpenOCD
  (`init; reset run; shutdown`) or the board reset button instead; the new
  firmware boots normally.
- Production flashing should still use `flash-demo.cmd` (OpenOCD): it is
  faster and does not depend on the patches above.
