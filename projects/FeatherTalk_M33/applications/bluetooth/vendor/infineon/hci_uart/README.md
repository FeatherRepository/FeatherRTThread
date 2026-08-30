# Infineon HCI-UART integration source

This directory vendors the Apache-2.0 files required from Infineon's
`btstack-integration` repository so the FeatherTalk build never compiles a C
file outside this repository.

- Upstream: `https://github.com/Infineon/btstack-integration.git`
- Upstream commit: `a9a0e7f9dd356c3bcd832e2626f9a53525fb87ec`
- Upstream release: `7.0.2.3477`
- Component: `COMPONENT_HCI-UART`

FeatherTalk changes are intentionally kept directly in the local source:

1. `platform_hal_next_wrapper.c` enters/leaves the RT-Thread interrupt context
   around the HAL UART interrupt dispatcher.
2. `cybt_platform_task.c` restores interrupts before returning an exhausted
   TX heap allocation.
3. `cybt_platform_freertos.c` keeps the original PRIMASK state across nested
   integration-layer critical sections.
4. Low-frequency host timer diagnostics are emitted from the HCI_RX task, not
   the 512-byte RT-Thread soft-timer thread.

All imported source and header files retain their upstream copyright and
SPDX-License-Identifier headers. `LICENSE.Apache-2.0.txt` contains the full
license text.
