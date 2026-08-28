# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-2026, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Default configuration file for PSC3 x6 family of microcontrollers.

###############################################################################
# Timings
###############################################################################

# Boot Complete - absoulte timeout for bootcode completion 
# Boot time depends on the LCS, warm/cold boot, number of applications
# in flash (up to 3 for P6 series), type of encryption and other factors,
# excluding listen windows (<100ms)
set_or_global TIMEOUT_BOOT_COMPLETE    [expr {[string equal -nocase $BOARD "PSVP"] ? 15000 : 2000}]

###############################################################################
# DAP and TAPs settings
###############################################################################

set_or_global SWJ_IRLEN                8

# KitProg3 acquisition sequence:  |- Target: 0xFE == Custom acquisition sequence for DAP Acquire (0x85) CMD
#                                 |   |- Set Acquire Parameters CMD (0x91)
#                                 |   |  Select DAP handshake type  (0x01)
#                                 |   |  JTAG to Dormant to SWD     (0x04)
#                                 |   |
#                                 |   |      |-Set Acquire Parameters CMD (0x91)
#                                 |   |      | Set Acquire Timeout        (0x00)
#                                 |   |      | Timeout is seconds         (0x14 - 20 sec)
#                                 |   |      |
#                                 |   |      |      |- SWD sequence with                          TST_MODE_REQ address
#                                 |   |      |      |                                             |         TST_MODE value
#                                 |   |      |      |                                             |         |
set_or_global KP3_ACQUIRE_TM_CMD "254 910104 910014 85FE000108A5A5A950000000B1F0000D00A30B0000028B52200400BB80000000BD"
