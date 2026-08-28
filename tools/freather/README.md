# Feather product host tools

This product-owned directory contains the host-side build, programming, and
serial-console utilities used by the `product/edgi-talk` branch. It is isolated
under `tools/freather` so the SDK's native tools remain unchanged. OpenOCD and
PyOCD were downloaded from their official GitHub release pages; the serial
monitor was migrated from the local ABW development tree.

Large and opaque tool payloads are stored with Git LFS. After cloning or when
switching to this branch for the first time, materialize them with:

```powershell
git lfs install
git lfs pull --include="tools/freather/**"
```

## OpenOCD

- Distribution: Infineon Customized OpenOCD
- Version: 5.19.0.4782
- Release: https://github.com/Infineon/openocd/releases/tag/release-v5.19.0
- Archive SHA-256: `D22CA6E7853B04ECD331E2ABB9413B86F19FCC8B29A6C42CCCDBE6C1ADAA212C`
- Executable: `openocd/bin/openocd.exe`
- Convenience launcher: `openocd.cmd`
- PSE84 target: `openocd/scripts/target/infineon/pse84xgxs2.cfg`
- KitProg3 interface: `openocd/scripts/interface/kitprog3.cfg`
- PSE84 SMIF loader: `openocd/flm/infineon/pse8x6/PSE84_SMIF.FLM`

Example:

```powershell
.\tools\freather\openocd.cmd --version
```

## PyOCD

- Distribution: official self-contained Windows release
- Version: 0.45.1
- Release: https://github.com/pyocd/pyOCD/releases/tag/v0.45.1
- Archive SHA-256: `58C1934839B320648DDFE5CDA29D8B2F10DD579B20A5EFF8FFCEBD872EA46623`
- Executable: `pyocd/pyocd.exe`
- Convenience launcher: `pyocd.cmd`
- PSE84 DFP: https://itools.infineon.com/cmsis_packs/PSE8xxx_DFP/Infineon.PSE8xxx_DFP.1.1.0.pack

Example:

```powershell
.\tools\freather\pyocd.cmd --version
```

PyOCD does not include a built-in PSE84 target in this release. A compatible
CMSIS Device Family Pack is therefore supplied separately under
`cmsis-packs/Infineon.PSE8xxx_DFP.1.1.0.pack` (official Infineon release,
SHA-256 `FF285B5319AC41160F50854CE9145C7DFD8421839F4886A2270EF974C3A97D5A`).
PyOCD can parse the exact `pse846gps2dbzc4a` target, but it cannot currently
perform KitProg3's proprietary PSE84 acquire command or automatically enable
the Pack's non-default external SMIF loader. Infineon OpenOCD remains the safe
PSE84 programming path in this directory.

The original downloaded archives are retained under `archives/`.

## Arm GNU Toolchain

- Version: Arm GNU Toolchain 13.3.Rel1 / GCC 13.3.1
- Target: `arm-none-eabi`
- Host package: Windows i686
- Executable: `arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-gcc.exe`
- Convenience launcher: `arm-none-eabi-gcc.cmd`
- Repository policy: product-owned host tool; keep SDK-native sibling directories unchanged

Version check:

```powershell
.\tools\freather\arm-none-eabi-gcc.cmd --version
```

Point an SDK SCons build at this external toolchain without editing the SDK:

```powershell
$env:RTT_EXEC_PATH = (Resolve-Path `
  '.\tools\freather\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin').Path
```

## Firmware workflow

- External build runtime: CPython 3.14.7 + SCons 4.10.1 in `build-python/`
- Build launcher: `build-demo.cmd`
- Program/verify launcher: `flash-demo.cmd`
- Detailed PSE84 instructions: `PSE84_WORKFLOW.md`

Build and program the M33 template without installing tools into the SDK:

```powershell
.\tools\freather\build-demo.cmd Edgi_Talk_M33_Template
.\tools\freather\flash-demo.cmd Edgi_Talk_M33_Template
```

## Serial monitor

- Runtime: bundled CPython 3.14.7
- Dependency: pyserial 3.5, bundled under `serial-monitor/vendor/`
- Script: `serial-monitor/serial_monitor.py`
- Convenience launcher: `serial-monitor.cmd`
- Default serial settings: 115200 baud, 8 data bits, no parity, 1 stop bit
- Default logs: `serial-monitor/logs/`

Examples:

```powershell
.\tools\freather\serial-monitor.cmd --list
.\tools\freather\serial-monitor.cmd --port COM5
.\tools\freather\serial-monitor.cmd --port COM5 --timestamps
.\tools\freather\serial-monitor.cmd --self-test
```

The launcher uses only the bundled runtime and dependency directory; no system
Python installation is required. See `serial-monitor/README.md` for logging,
transmit, and unattended-monitoring options.

The connected Edgi-Talk board was verified on `COM17` (`04B4:F155`) at 115200
baud: `help`, `version`, and `ps` all returned through the `msh />` prompt.
