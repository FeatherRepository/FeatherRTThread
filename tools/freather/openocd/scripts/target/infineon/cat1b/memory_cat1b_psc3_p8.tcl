# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-2026, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Memory and Registers Map for PSC3 x7/x8 families of microcontrollers.

###############################################################################
# Base Memory Map
###############################################################################

# Maximum possible main flash bank size in the PSC3 x7/x8 series
set_or_global MAIN_FLASH_SIZE_MAX     0x80000

# Default offsets for PPCA cores images linkage in MTB
set_or_global FLASH_SLOT_ADDR_PPCA_0  0x22030000
set_or_global FLASH_SLOT_ADDR_PPCA_1  0x22038000

# PPCA SRAM memory regions
set_or_global SRAM_PPCA_NS_BASE         0x24010000
set_or_global SRAM_PPCA_S_BASE          0x34010000

###############################################################################
# Device registers
###############################################################################

# FlashBoot version addresses
set_or_global FB_VER_HI_ADDR            0x13402404
set_or_global FB_VER_LO_ADDR            0x13402408

###############################################################################
# PPCA - Programmable Power Control Accelerator 
###############################################################################

set_or_global PERI0_GR4_SL_CTL                      0x52004110
set_or_global PPCA_CNFG_CNFG_CPU_CTRL               0x53000110
set_or_global PPCA_CNFG_CNFG_RST_CTRL               0x53000114
set_or_global PPCA_CPUSS_CNFG_MXCM330_CM33_CTL      0x53080000
set_or_global PPCA_CPUSS_CNFG_MXCM331_CM33_CTL      0x53090000
set_or_global PPCA_CPUSS_CNFG_AP_CTL                0x530F0000
set_or_global CPUSS_AP_CTL                          0x521C1000

# PPCA IP has 5 SRAM areas, each having certain purpose described in a table:
###############################################################################
# Description      |   CM33/SYS    |  PPCA Core0   |  PPCA Core1   |   Size   #
#                  |  Memory map   |  Memory map   |  Memory map   |          #
#-----------------------------------------------------------------------------#
# PPCA Core0 code  |  0x4301_0000  |  0x0000_0000  |          -    |  0x8000  #
# PPCA Core0 data  |  0x4302_0000  |  0x2000_0000  |          -    |  0x4000  #
# PPCA Core1 code  |  0x4303_0000  |        -      |  0x0000_0000  |  0x8000  #
# PPCA Core1 data  |  0x4304_0000  |        -      |  0x2000_0000  |  0x4000  #
# PPCA shared mem  |  0x4305_0000  |  0x2000_4000  |  0x2000_4000  |  0x4000  #
###############################################################################

# Main CPU memory map
set_or_global SRAM0_PPCA_MAIN_ADDR 0x43010000
set_or_global SRAM1_PPCA_MAIN_ADDR 0x43020000
set_or_global SRAM2_PPCA_MAIN_ADDR 0x43030000
set_or_global SRAM3_PPCA_MAIN_ADDR 0x43040000
set_or_global SRAM4_PPCA_MAIN_ADDR 0x43050000

set_or_global SRAM0_PPCA_SIZE 0x8000
set_or_global SRAM1_PPCA_SIZE 0x4000
set_or_global SRAM2_PPCA_SIZE 0x8000
set_or_global SRAM3_PPCA_SIZE 0x4000
set_or_global SRAM4_PPCA_SIZE 0x4000

# PPCA CORE0 CPU memory map
set_or_global  SRAM0_PPCA_CODE_CORE0_ADDR  0x00000000
set_or_global  SRAM1_PPCA_DATA_CORE0_ADDR  0x20000000
set_or_global  SRAM4_PPCA_SHMEM_CORE0_ADDR 0x20040000

set_or_global  SRAM0_PPCA_CODE_CORE0_SIZE  $SRAM0_PPCA_SIZE
set_or_global  SRAM1_PPCA_DATA_CORE0_SIZE  $SRAM1_PPCA_SIZE
set_or_global  SRAM4_PPCA_SHMEM_CORE0_SIZE $SRAM4_PPCA_SIZE

# PPCA CORE1 CPU memory map
set_or_global  SRAM2_PPCA_CODE_CORE1_ADDR  0x00000000
set_or_global  SRAM3_PPCA_DATA_CORE1_ADDR  0x20000000
set_or_global  SRAM4_PPCA_SHMEM_CORE1_ADDR 0x20040000

set_or_global  SRAM2_PPCA_CODE_CORE1_SIZE  $SRAM2_PPCA_SIZE
set_or_global  SRAM3_PPCA_DATA_CORE1_SIZE  $SRAM3_PPCA_SIZE
set_or_global  SRAM4_PPCA_SHMEM_CORE1_SIZE $SRAM4_PPCA_SIZE
