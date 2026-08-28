# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2023-2026, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Configuration script for PSC3 family of microcontrollers.

source [find target/swj-dp.tcl]
source [find mem_helper.tcl]
source [find target/cympn.cfg]
source [find target/infineon/common/common_arm.tcl]
source [find target/infineon/common/common_ifx.tcl]

namespace import arm::*
namespace import ifx::*

###############################################################################
# Target/setup identification
###############################################################################

set CATEGORY cat1b
set_or_global CHIPNAME psc3
set ${CHIPNAME}::TARGET_VARIANT $TARGET_VARIANT

if {![info exists SERIES]} {
	puts "Warn: Do not use this config directly."
	puts "Warn: Need to define the target explicitly:"
	puts "Info: Use: -c \"set SERIES <series_name>; \[set DEVICE <device_name>\]; \[set BOARD <board_name>\]\""
}

# Set default target identification variables, if not set externally.
# Supported variants for CAT1B:
#   SERIES| DEVICE | BOARD
#   ------+--------+---------------
#   psc3  | a0     | n/a (generic)
#   psc3  | p8     | n/a (generic)
#   psc3  | p6     | n/a (generic)
set_or_global SERIES  psc3
set_or_global DEVICE  a0
set_or_global BOARD   generic

puts "***************************************"
puts "** SERIES:   $SERIES"
puts "** DEVICE:   $DEVICE"
puts "** BOARD:    $BOARD"
puts "***************************************"

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
namespace import cat1::*
namespace import [string tolower ${CATEGORY}::*]
namespace import [string tolower ${CATEGORY}::${SERIES}*]
namespace import [string tolower ${CATEGORY}::${SERIES}::${DEVICE}*]
namespace import [string tolower ${CATEGORY}::${SERIES}::${DEVICE}::${BOARD}*]

set _TARGET_SYS          ${CHIPNAME}.sys
set _TARGET_CM33         ${CHIPNAME}.cm33

# Targets present only in psc3 x7/x8 series
set _TARGET_CM33_PPCA_0  ${CHIPNAME}.cm33.ppca0
set _TARGET_CM33_PPCA_1  ${CHIPNAME}.cm33.ppca1

###############################################################################
# Adapter and SWJ settings
###############################################################################

adapter speed $ADAPTER_SPEED
adapter srst delay $ADAPTER_SRST_DELAY
swj_newdap $CHIPNAME cpu -irlen $SWJ_IRLEN -ircapture 0x1 -irmask 0xf -expected-id 0

###############################################################################
# Configure DAP
###############################################################################

dap create $CHIPNAME.dap -chain-position $CHIPNAME.cpu -adiv6 -power-down-on-quit
$CHIPNAME.dap apsel $AP_SEL_SYS
$CHIPNAME.dap apcsw $AP_CSW_SYS $arm::CSW_HPROT0TO3

###############################################################################
# Configure CM33-AP
###############################################################################

if {$ENABLE_CM33} {
	# Create CM33 target
	target create $_TARGET_CM33 cortex_m -dap $CHIPNAME.dap -ap-num $::AP_SEL_CM33

	$CHIPNAME.dap apsel $AP_SEL_CM33
	$CHIPNAME.dap apcsw $AP_CSW_CM33 $AP_CSW_CM33_MASK

	# Configure events, define callbacks
	$_TARGET_CM33 configure -work-area-phys $WORKAREAADDR -work-area-size $WORKAREASIZE
	$_TARGET_CM33 cortex_m reset_config sysresetreq
	$_TARGET_CM33 configure -event reset-start "event_cm33_reset_start"
	$_TARGET_CM33 configure -event halted "read_and_init_secure $_TARGET_CM33; update_flash_size"

	if {$DEVICE != "a0"} {
		# Long boot is possible for psc3 x7/x8 and x6 series for cases
		# when secure images needed to be checked by the bootloader.
		# Standard reinit timeout does not consider such cases.
		$_TARGET_CM33 configure -event reset-assert-post "long_boot_acquire"
	}

	# Setup FLASH WaitStates properly, disable MSPLIM, preinit PPCA for x7/x8 serie
	$_TARGET_CM33 configure -event reset-init "event_reset_init"

	proc event_reset_init {} {
		log_proc_entry
		reg msplim_s 0
		read_and_init_secure $::_TARGET_CM33
		update_flash_size

		if {[info exists ::ENABLE_CM33_PPCA] && $::ENABLE_CM33_PPCA} {
			ppca_acquire
		}
		log_proc_return
	}

	proc event_cm33_reset_start {} {
		log_proc_entry
		if {$::ENABLE_ACQUIRE} {
			acquire_test_mode acquire_and_check
		}
		log_proc_return
	}

	proc event_cm33_reset_deassert_post {} {
		log_proc_entry
		$::_TARGET_CM33 arp_examine
		$::_TARGET_CM33 arp_poll

		if {$::RESET_MODE eq "run"} return
		$::_TARGET_CM33 arp_poll
		$::_TARGET_CM33 arp_halt
		$::_TARGET_CM33 arp_waitstate halted 100
		log_proc_return
	}
}

if {[info exists ENABLE_CM33_PPCA] && $DEVICE == "p8"} {
	if {$ENABLE_CM33_PPCA} {
		# Enable PPCA debug
		target create $_TARGET_CM33_PPCA_0 cortex_m -dap $CHIPNAME.dap -ap-num $AP_SEL_PPCA_0 -defer-examine
		$CHIPNAME.dap apsel $AP_SEL_PPCA_0
		$CHIPNAME.dap apcsw $AP_CSW_PPCA
		$_TARGET_CM33_PPCA_0 cortex_m reset_config sysresetreq
		$_TARGET_CM33_PPCA_0 configure -event reset-assert {}
		$_TARGET_CM33_PPCA_0 configure -work-area-phys $PPCA_WORKAREAADDR -work-area-size $PPCA_WORKAREASIZE

		if {[info exists PPCA_CORE_NUM] && $PPCA_CORE_NUM == 2} {
			target create $_TARGET_CM33_PPCA_1 cortex_m -dap $CHIPNAME.dap -ap-num $AP_SEL_PPCA_1 -defer-examine
			$CHIPNAME.dap apsel $AP_SEL_PPCA_1
			$CHIPNAME.dap apcsw $AP_CSW_PPCA
			$_TARGET_CM33_PPCA_1 cortex_m reset_config sysresetreq
			$_TARGET_CM33_PPCA_1 configure -event reset-assert {}
			$_TARGET_CM33_PPCA_1 configure -work-area-phys $PPCA_WORKAREAADDR -work-area-size $PPCA_WORKAREASIZE
		}
	}
}

###############################################################################
# Configure SYS-AP
###############################################################################

target create $_TARGET_SYS mem_ap -dap $CHIPNAME.dap -ap-num $AP_SEL_SYS
$_TARGET_SYS configure -event examine-end "event_sys_examine_end"

proc event_sys_examine_end {} {
	log_proc_entry
	catch {display_chip_info $::CHIPNAME}
	log_proc_return
}

###############################################################################
# Configure DAP
###############################################################################

$CHIPNAME.dap configure -event init-post "event_dap_init_post"
$CHIPNAME.dap configure -event init-fail "event_dap_init_fail"

proc event_dap_init_post {} {
	log_proc_entry
	if {[using_jtag]} {
		# Power up DAP using DP.CTRL/STAT and clear possible sticky errors
		set dap [$::CHIPNAME.sys cget -dap]
		catch { $dap dpreg 0x4 0x50000032 }
	}

	if {$::ENABLE_ACQUIRE} {
		if {[info exists ::is_in_dap_init] || [adapter name] != "kitprog3" || [using_jtag]} return
		set ::is_in_dap_init 1
		acquire_test_mode check_only
		catch {unset ::is_in_dap_init}
	}
	log_proc_return
}

proc event_dap_init_fail {} {
	log_proc_entry
	if {$::ENABLE_ACQUIRE} {
		if {[info exists ::is_in_dap_init]} return
		set ::is_in_dap_init 1
		acquire_test_mode acquire_and_check
		catch {unset ::is_in_dap_init}
	} else {
		# Take into account long-boot cases of psc3x7/psc3x8
		if {$::DEVICE == "a0"} {
			sleep 500
		} else {
			sleep 5000
		}
	}
	log_proc_return
}

if {[using_jtag]} {
	if {$DEVICE == "a0"} {
		# a0 serie has separate bs jtag tap
		swj_newdap $CHIPNAME bs -irlen $SWJ_IRLEN -expected-id 0
	}
}

###############################################################################
# Misc. configuration
###############################################################################

if {$ENABLE_CM33} {
	define_flash_banks_psc3
}

kitprog3_acquire_config $TIMEOUT_RESET_HANDSHAKE

# Set default Access Port
if {$ENABLE_CM33} {
	set _TARGETNAME $_TARGET_CM33
	targets $_TARGET_CM33
}
