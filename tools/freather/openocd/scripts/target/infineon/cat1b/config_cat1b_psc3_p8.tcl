# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-2026, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Default configuration file for PSC3 x7/x8 families of microcontrollers.

# Note: expected change in bootcodes for x7/x8: https://jiracc.intra.infineon.com/browse/MXSV2BOOT-5229
# to separate DEAD and CORRUPTED codes

###############################################################################
# CPU cores availability for the debugger and the acquisition methods
###############################################################################


# Programmable Power Control Accelerator (PPCA) – 2 M33 additional cores 
# for current and voltage controls
set_or_global ENABLE_CM33_PPCA          0

###############################################################################
# DAP and TAPs settings
###############################################################################

set_or_global AP_SEL_PPCA_0            0xF0006000
set_or_global AP_SEL_PPCA_1            0xF0008000

# AP->CSW: Control/Status Word register
set_or_global AP_CSW_PPCA              [expr {$arm::CSW_HPROT0_DATAINST   | \
                                              $arm::CSW_HPROT1_PRIVILEGED | \
                                              $arm::CSW_HPROT3_CACHEABLE  | \
                                              $arm::CSW_HPROT4_LOOKUP     | \
                                              $arm::CSW_HNONSEC}] ; # 0x5B000000


###############################################################################
# PPCA working area
###############################################################################

set_or_global PPCA_WORKAREAADDR              0x20000000
set_or_global PPCA_WORKAREASIZE              0x200

###############################################################################
# Timings
###############################################################################

# Boot Complete - absoulte timeout for bootcode completion 
# Boot time depends on the LCS, warm/cold boot, number of applications
# in flash (up to 5 for P8 series), type of encryption and other factors,
# excluding listen windows (<100ms)
set_or_global TIMEOUT_BOOT_COMPLETE    [expr {[string equal -nocase $BOARD "PSVP"] ? 15000 : 3500}]

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
