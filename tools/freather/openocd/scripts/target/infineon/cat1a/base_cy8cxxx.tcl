# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2019-2025, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Common configuration for PSOC 6 family of microcontrollers.
# PSOC 6 is a dual-core device with CM0+ and CM4 cores. Both cores share
# the same Flash/RAM/MMIO address space.

###############################################################################
# Include common misc scripts
###############################################################################

source [find mem_helper.tcl]
source [find target/swj-dp.tcl]
source [find target/infineon/common/common_arm.tcl]
source [find target/infineon/common/common_ifx.tcl]
namespace import arm::*
namespace import ifx::*

set CATEGORY cat1a
set SERIES cy8cxxx
set_or_global CHIPNAME psoc6
set_or_global FLASH_DRIVER_NAME psoc6
set ${CHIPNAME}::TARGET_VARIANT $TARGET_VARIANT

###############################################################################
# Target/setup identification
###############################################################################

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
source [find target/infineon/cat1/func_cat1.tcl]

# Import definitions from included namespaces
namespace import ${CATEGORY}::${SERIES}*
namespace import ${CATEGORY}::*
namespace import cat1::*

global TARGET
set TARGET $CHIPNAME.cpu

###############################################################################
# Adapter and SWJ settings
###############################################################################

set_adapter_options
define_kitprog3_acquire_options ${FLASH_DRIVER_NAME}
swj_newdap $CHIPNAME cpu -irlen $::SWJ_IRLEN -ircapture 0x1 -irmask 0xf

###############################################################################
# Configure DAP
###############################################################################

dap create $CHIPNAME.dap -chain-position $CHIPNAME.cpu -adiv5 -power-down-on-quit

if {[using_jtag]} {
	swj_newdap $CHIPNAME bs -irlen $::PSOC6_JTAG_IRLEN -expected-id 0
	
}

###############################################################################
# Configure CM0-AP
###############################################################################

if { $ENABLE_CM0 } {
	target create ${TARGET}.cm0 cortex_m -dap $CHIPNAME.dap -ap-num 1 -coreid 0
	${TARGET}.cm0 configure -work-area-phys $::WORKAREAADDR -work-area-size $::WORKAREASIZE -work-area-backup 0

	${TARGET}.cm0 cortex_m reset_config sysresetreq
	${TARGET}.cm0 configure -event reset-deassert-post "mxs40_reset_deassert_post $FLASH_DRIVER_NAME ${TARGET}.cm0"
	${TARGET}.cm0 configure -event examine-end " \
		display_info $FLASH_DRIVER_NAME \
		${CHIPNAME}_main_cm0 ${CHIPNAME}_work_cm0"
	${TARGET}.cm0 configure -event examine-fail " \
		display_info $FLASH_DRIVER_NAME \
		${CHIPNAME}_main_cm0 ${CHIPNAME}_work_cm0"
	set _ACQUIRE_TARGET cm0
}

###############################################################################
# Configure CM4-AP
###############################################################################

if { $ENABLE_CM4 } {
	target create ${TARGET}.cm4 cortex_m -dap $CHIPNAME.dap -ap-num 2 -coreid 1
	${TARGET}.cm4 configure -work-area-phys $::WORKAREAADDR -work-area-size $::WORKAREASIZE -work-area-backup 0

	if { $ENABLE_CM0 } {
		# Avoid double-reset on dual-core parts
		${TARGET}.cm4 configure -event reset-assert {}
		targets ${TARGET}.cm0
	} else {
		${TARGET}.cm4 configure -event examine-end " \
			display_info $FLASH_DRIVER_NAME \
			${CHIPNAME}_main_cm4 ${CHIPNAME}_work_cm4"
		${TARGET}.cm4 configure -event examine-fail " \
			display_info $FLASH_DRIVER_NAME \
			${CHIPNAME}_main_cm4 ${CHIPNAME}_work_cm4"
		set _ACQUIRE_TARGET cm4
	}

	${TARGET}.cm4 cortex_m reset_config sysresetreq
	${TARGET}.cm4 configure -event reset-deassert-post "mxs40_reset_deassert_post $FLASH_DRIVER_NAME ${TARGET}.cm4"
}

###############################################################################
# Misc. configuration
###############################################################################

define_flash_banks
gdb_smart_program enable

if { $ENABLE_CM0 && !$ENABLE_CM4} {
	set _TARGETNAME ${TARGET}.cm0
} elseif {$ENABLE_CM4 && !$ENABLE_CM0} {
	set _TARGETNAME ${TARGET}.cm4
} elseif {$ENABLE_CM4 && $ENABLE_CM0} {
	set _TARGETNAME0 ${TARGET}.cm0
	set _TARGETNAME1 ${TARGET}.cm4
}
