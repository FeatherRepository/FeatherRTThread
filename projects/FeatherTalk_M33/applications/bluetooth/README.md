# FeatherTalk CYW55500A1 Bluetooth

This module brings up the CYW55500A1 controller on the PSoC Edge E84 board,
downloads the board-specific PatchRAM image, starts the AIROC host stack, and
provides BLE scan/advertising diagnostics through the M33 msh console.

## Repository-contained dependency layout

The build only reads Bluetooth files below this directory:

```text
vendor/infineon/hci_uart/               Apache-2.0 integration source
third_party/infineon/btstack/           pinned official Infineon submodule
third_party/infineon/bt-fw-ifx-cyw55500a1/
                                        pinned official firmware submodule
```

It does not search `D:\Develop\Edgi-Talk\CYW5551x`, use a host-specific
absolute path, or include one C source file from another C source file.
The firmware source is copied by SCons into the ignored project `build` tree
before compilation. The vendored integration sources use relative VariantDir
paths as well, so every generated object stays in the ignored project `build`
tree and neither repository-owned source nor an official submodule is dirtied.

After cloning the SDK, initialize the official dependencies:

```powershell
git submodule update --init --recursive
```

The build fails with an explicit missing-file list if either submodule has not
been initialized. The official assets remain under their Infineon EULAs; the
Apache-2.0 HCI-UART sources retain their upstream headers and license text.

Pinned inputs:

| Asset | Revision |
| --- | --- |
| Infineon `btstack-integration` imported source | `a9a0e7f9dd356c3bcd832e2626f9a53525fb87ec` |
| Infineon `btstack` submodule | `3d4617f296ccb1b271abe033fc5a855087faf75f` |
| Infineon `bt-fw-ifx-cyw55500a1` submodule | `c8f0c55d63d62fc028b93472b42066d624d171a0` |

The board firmware component is:

```text
COMPONENT_wlbga_iPA_sLNA_ANT0_LHL_XTAL_IN
```

## Build and board validation

From the SDK root:

```powershell
.\tools\freather\build-demo.ps1 -Project FeatherTalk_M33 -Jobs 16
.\tools\freather\flash-demo.ps1 -Project FeatherTalk_M33 -Programmer OpenOCD -AdapterKHz 1000
.\tools\freather\serial-monitor.cmd --port COM4 --baud 115200
```

The controller downloads at 3 Mbit/s and changes to the 115200-bit/s feature
baud after PatchRAM launch. A successful boot logs the local Bluetooth address
and `AIROC host stack ready`.

2026-08-31 board validation on the PSoC Edge E84 completed the full chain:

- the repository-contained CYW55500A1 PatchRAM image downloaded and launched;
- the host reached `ready` with `error=0` and address `9C:C7:D3:E1:BC:53`;
- startup active scan received 18 reports and retained 11 unique devices;
- a second runtime scan completed with 19 reports / 11 unique devices;
- `bt_adv off` and `bt_adv on` both returned success and changed `bt_status`.

Available M33 console commands:

| Command | Purpose |
| --- | --- |
| `bt_status` | controller/host state and cached address |
| `bt_scan` | start an active BLE scan |
| `bt_scan_stop` | stop the current scan |
| `bt_devices` | list unique scan results |
| `bt_adv on|off` | enable or disable non-connectable advertising |

## Local integration fixes

The RT-Thread adaptations are applied directly to the vendored source so they
are visible in ordinary diffs and cannot disappear with a developer-machine
dependency:

- RT-Thread interrupt enter/leave bookkeeping around the BT UART ISR;
- nesting-safe PRIMASK restoration for integration critical sections;
- guaranteed interrupt restoration when the BT TX heap is exhausted;
- low-frequency host-timer arm/dispatch diagnostics executed outside the
  512-byte RT-Thread soft-timer callback stack;
- RT-Thread CY-RTOS abstraction for tasks, queues, semaphores, timers, time,
  mutexes, and memory.
- disabled SMP is treated as a valid build configuration instead of returning
  the misleading generic `WICED_ERROR (0x28)` from the platform wrapper.

Pairing/SMP, key persistence, GATT services, and production recovery policy are
separate follow-up stages.

The current build deliberately sets `USE_AIROC_STACK_SMP=0` and disables the
SMP client/server and local-key generator. Therefore scan and non-connectable
advertising are operational, while pairing, encrypted links, bonding and key
persistence are not yet claimed. Enabling SMP requires the identity/link-key
request/update persistence callbacks and an explicit pairing I/O policy; merely
changing the macro is not sufficient.
