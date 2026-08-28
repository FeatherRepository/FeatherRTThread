# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-2026, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Base configuration script for CAT1D category of microcontrollers.

###############################################################################
# Include common misc scripts
###############################################################################

source [find mem_helper.tcl]
source [find target/swj-dp.tcl]
source [find target/cympn.cfg]
source [find target/infineon/common/common_arm.tcl]
source [find target/infineon/common/common_ifx.tcl]
namespace import arm::*
namespace import ifx::*

###############################################################################
# Target/setup identification
###############################################################################

set CATEGORY cat1d
set CHIPNAME $CATEGORY

if {![info exists SERIES]} {
	puts "Warn : Do not use this config directly."
	puts "Warn : Need to define the target explicitly:"
	puts "Info : Use: -c \"set SERIES <series_name>; \[set DEVICE <device_name>\]; \[set BOARD <board_name>\]\""
}

# Source the cascading chain of config files for the given prefix type.
# Loads all matching files for the setup:
#   memory_[CATEGORY[_SERIES[_DEVICE[_BOARD]]]].<cfg|tcl>
#   config_[CATEGORY[_SERIES[_DEVICE[_BOARD]]]].<cfg|tcl>
#   func_[CATEGORY[_SERIES[_DEVICE[_BOARD]]]].<cfg|tcl>
# Low-level variables and procedures override higher-level definitions, so
# files are sourced bottom->top (BOARD -> DEVICE -> SERIES -> CATEGORY).
# Wildcard "any" can be used for any component in the file name.
source_cfg_chain memory
source_cfg_chain config
source_cfg_chain func
source [find [string tolower target/infineon/cat1/func_cat1.tcl]]

# Import definitions from included namespaces
namespace import [string tolower ${CATEGORY}::${SERIES}::${DEVICE}::${BOARD}*]
namespace import [string tolower ${CATEGORY}::${SERIES}::${DEVICE}*]
namespace import [string tolower ${CATEGORY}::${SERIES}*]
namespace import [string tolower ${CATEGORY}::*]
namespace import cat1::*

###############################################################################
# Configuration parameters (common for M0SEC devices within CAT1d category)
###############################################################################

set_or_global ENABLE_CM0                1
set_or_global ENABLE_CM33               0
set_or_global ENABLE_CM55               0
set_or_global ENABLE_ACQUIRE            0
set_or_global SWJ_IRLEN                 4
set_or_global WORKAREAADDR              $SRAM_M0_S_BASE
set_or_global WORKAREASIZE              $SRAM_M0_SIZE

###############################################################################
# Adapter and SWJ settings
###############################################################################

echo "transport: [transport select]"
echo "adapter name: [adapter name]"
adapter speed [expr { [using_jtag]? $::ADAPTER_SPEED_JTAG : $::ADAPTER_SPEED_SWD }]
adapter srst delay $::ADAPTER_SRST_DELAY
swj_newdap $CHIPNAME cpu -irlen $::SWJ_IRLEN -ircapture 0x1 -irmask 0xf -expected-id 0

###############################################################################
# Reset configuration
###############################################################################

# Set the reset configuration for M0SEC. Neither SYSRESETREQ nor VECTRESET work M0SEC.
reset_config srst_only srst_gates_jtag

###############################################################################
# Configure DAP and M0SEC core
###############################################################################

dap create $CHIPNAME.dap -chain-position $CHIPNAME.cpu -adiv5 -power-down-on-quit
target create $CHIPNAME.m0sec cortex_m -dap $CHIPNAME.dap
$CHIPNAME.m0sec configure -work-area-phys $::WORKAREAADDR -work-area-size $::WORKAREASIZE -work-area-backup 1
$CHIPNAME.m0sec cortex_m reset_config sysresetreq
$CHIPNAME.m0sec configure -event reset-deassert-post "event_m0sec_reset_deassert_post"

proc event_m0sec_reset_deassert_post { } {
	log_proc_entry
	set tgt [target current]
	catch { $tgt arp_examine }
	catch { $tgt arp_poll }
	if {$::RESET_MODE ne "run"} {

		catch { $tgt arp_poll }
		if {[$tgt curstate] == "running"} {
			echo "Info : \[$tgt\] Ran after reset and before halt..."
			$tgt arp_halt
			$tgt arp_waitstate halted 1000
		}

		# Better to keep the caches disabled during debug. Ref. CDT_005546-27
		echo "Info : \[$tgt\] Disabling L1-Data-SRAM cache..."
		mww $::M0SECCPUSS_SRAM_CTL 0x0C
	}
	log_proc_return
}

###############################################################################
# Flash banks
###############################################################################

if {[info exists ::RRAM_MAIN_BASE] && [info exists ::RRAM_MAIN_SIZE]} {
	flash bank $::CHIPNAME.main_ns   cmsis_flash [addr_to_ns $::RRAM_MAIN_BASE] $::RRAM_MAIN_SIZE 4 4 $::CHIPNAME.m0sec $::RRAM_FLASHLOADER 1024
	flash bank $::CHIPNAME.main_s    virtual     [addr_to_s  $::RRAM_MAIN_BASE] $::RRAM_MAIN_SIZE 4 4 $::CHIPNAME.m0sec $::CHIPNAME.main_ns
}
if {[info exists ::RRAM_PROT_BASE] && [info exists ::RRAM_PROT_SIZE]} {
	flash bank $::CHIPNAME.prot_ns   cmsis_flash [addr_to_ns $::RRAM_PROT_BASE] $::RRAM_PROT_SIZE 4 4 $::CHIPNAME.m0sec $::RRAM_FLASHLOADER 1024
	flash bank $::CHIPNAME.prot_s    virtual     [addr_to_s  $::RRAM_PROT_BASE] $::RRAM_PROT_SIZE 4 4 $::CHIPNAME.m0sec $::CHIPNAME.prot_ns
}
if {[info exists ::RRAM_PROT_P_BASE] && [info exists ::RRAM_PROT_P_SIZE]} {
	flash bank $::CHIPNAME.prot_p_ns cmsis_flash [addr_to_ns $::RRAM_PROT_P_BASE] $::RRAM_PROT_P_SIZE 4 4 $::CHIPNAME.m0sec $::RRAM_FLASHLOADER 1024
	flash bank $::CHIPNAME.prot_p_s  virtual     [addr_to_s  $::RRAM_PROT_P_BASE] $::RRAM_PROT_P_SIZE 4 4 $::CHIPNAME.m0sec $::CHIPNAME.prot_p_ns
}

log_debug "--- Config file processed ---"
