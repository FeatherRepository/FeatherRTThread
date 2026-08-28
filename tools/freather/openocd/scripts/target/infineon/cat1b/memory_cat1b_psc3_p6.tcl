# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-2026, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Memory and Registers Map for PSC3 x6 family of microcontrollers.

###############################################################################
# Base Memory Map
###############################################################################

# Maximum possible main flash bank size in a PSC3x6 serie
set_or_global MAIN_FLASH_SIZE_MAX       0x80000

###############################################################################
# Device registers
###############################################################################

# FlashBoot version addresses
set_or_global FB_VER_HI_ADDR            0x13402004
set_or_global FB_VER_LO_ADDR            0x13402008
