# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-2025, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Subroutines for PSC3 x7/x8 families of microcontrollers.

# Acquire PPCA CM33 cores
proc ppca_acquire {} {
	set ENDLESS_LOOP_INSR 0xE7FEE7FE

	puts "PPCA initialization"

	targets $::_TARGET_SYS

	#########################################
	#       PERI GROUP SELECT               #
	#########################################
	write32_mask $::_TARGET_SYS $::PERI0_GR4_SL_CTL 0x01

	#########################################
	#       PPCA CORE 0 CONFIG              #
	#########################################
	targets $::_TARGET_CM33
	# Set msp and pc to the start of PPCA Core0 data SRAM  from main core
	mww 0x43010000 0x20001ffc
	mww 0x43010004 0x20000009
	mww 0x43020008 $ENDLESS_LOOP_INSR
	targets $::_TARGET_SYS

	#########################################
	#       STARTING PPCA CORE 0            #
	#########################################
	write32_mask $::_TARGET_SYS $::PPCA_CNFG_CNFG_CPU_CTRL 0x01
	write32_mask $::_TARGET_SYS $::PPCA_CNFG_CNFG_RST_CTRL 0x01
	write32 $::_TARGET_SYS $::PPCA_CPUSS_CNFG_MXCM330_CM33_CTL 0x0

	if {[info exists ::PPCA_CORE_NUM] && $::PPCA_CORE_NUM == 2} {
		#########################################
		#       PPCA CORE 1 CONFIG              #
		#########################################
		targets $::_TARGET_CM33
		# Set msp and pc to the start of PPCA Core1 data SRAM from main core
		mww 0x43030000 0x20001ffc
		mww 0x43030004 0x20000009
		mww 0x43040008 $ENDLESS_LOOP_INSR
		targets $::_TARGET_SYS

		#########################################
		#       STARTING PPCA CORE 1            #
		#########################################
		write32_mask $::_TARGET_SYS $::PPCA_CNFG_CNFG_CPU_CTRL 0x02
		write32_mask $::_TARGET_SYS $::PPCA_CNFG_CNFG_RST_CTRL 0x02
		write32 $::_TARGET_SYS $::PPCA_CPUSS_CNFG_MXCM331_CM33_CTL 0x0
	}

	#########################################
	#     Enabling PPCA Debug 600 SWJ       #
	#########################################
	write32 $::_TARGET_SYS [addr_to_sorns $::_TARGET_CM33 $::PPCA_CPUSS_CNFG_AP_CTL] 0x337
	write32 $::_TARGET_CM33 [addr_to_sorns $::_TARGET_CM33 $::CPUSS_AP_CTL] 0x000053f7

	targets $::_TARGET_CM33

	$::_TARGET_CM33_PPCA_0 arp_examine
	$::_TARGET_CM33_PPCA_0 arp_poll
	$::_TARGET_CM33_PPCA_0 arp_poll
	$::_TARGET_CM33_PPCA_0 arp_halt

	if {[info exists ::PPCA_CORE_NUM] && $::PPCA_CORE_NUM == 2} {
		$::_TARGET_CM33_PPCA_1 arp_examine
		$::_TARGET_CM33_PPCA_1 arp_poll
		$::_TARGET_CM33_PPCA_1 arp_poll
		$::_TARGET_CM33_PPCA_1 arp_halt
	}
}

# Attach to PPCA target
proc attach_ppca_core {ppca_tgt} {
	set result 0
	read_and_init_secure $::_TARGET_CM33
	targets $::_TARGET_SYS

	# Set power to PPCA cores
	set addr [addr_to_sorns $::_TARGET_CM33 $::PERI0_GR4_SL_CTL] 
	if {![catch {mrw $addr} gr4_sl_ctl]} {
		if {($gr4_sl_ctl & 0x01) == 0} {
			if {[catch {mww $addr [expr {$gr4_sl_ctl|0x01}]}]} {
				puts stderr "** Fail powering PPCA"
				return 0
			}
		}
	} else {
		ppca_acquire
		$ppca_tgt arp_waitstate halted 100
		return 1
	}

	# Clear CPU_WAIT bit
	set addr 0x00
	if {$ppca_tgt==$::_TARGET_CM33_PPCA_0} {
		set addr [addr_to_sorns $::_TARGET_CM33 $::PPCA_CPUSS_CNFG_MXCM330_CM33_CTL]
	} else {
		set addr [addr_to_sorns $::_TARGET_CM33 $::PPCA_CPUSS_CNFG_MXCM331_CM33_CTL]
	}

	set cm33_ctl  [mrw $addr]
	if {$cm33_ctl==0x10} {
		# PPCA core preconfig needed, PPCA core is not runned
		ppca_acquire
		$ppca_tgt arp_waitstate halted 100
		return 1
	}

	# PPCA core runs, just attach to it
	catch {mww $addr 0x00}

	# Enabling PPCA Debug 600 SWJ 
	set addr [addr_to_sorns $::_TARGET_CM33 $::PPCA_CPUSS_CNFG_AP_CTL]
	catch {$::_TARGET_SYS mww $addr 0x337}

	$ppca_tgt arp_examine
	$ppca_tgt arp_poll
	$ppca_tgt arp_poll
	$ppca_tgt arp_halt
	$ppca_tgt arp_waitstate halted 100
}

# Launch PPCA<x> CM33 core debug. Supported targets:
# 'PPCA_0' - _TARGET_CM33_PPCA_0
# 'PPCA_1' - _TARGET_CM33_PPCA_1
# ppca_slot - optional argument. Set PPCA image offset if default is not used
# Supported modes: 'attach', 'single_debug'
proc reset_halt {target {mode attach} {ppca_slot "default"} } {
	set target [string toupper $target]
	set ppca_tgt [set ::_TARGET_CM33_${target}]

	if {$mode=="attach"} {
		attach_ppca_core $ppca_tgt
		return 0
	}

	# Mode 'single_debug'
	puts "** Trying to acquire [format %s $ppca_tgt] in single debug mode"

	if {[$::_TARGET_CM33 curstate] != "running"} {
		puts "Resume main core for [format %s $ppca_tgt] initialization and launching"

		# Try to acquire using VectorCatch after Main CPU configures PPCA core
		set dhcsr_val [ expr { $::arm::DHCSR_DBGKEY_VAL | 0x00000003}] ; # C_DEBUGEN | C_HALT

		write32 $ppca_tgt $::arm::DHCSR $dhcsr_val
		write32 $ppca_tgt $::arm::DEMCR $::arm::DEMCR_VC_CORERESET_VAL

		targets $::_TARGET_CM33
		resume
		#TODO: Can we avoid dummy sleeping?
		sleep 350
	}

	catch {$ppca_tgt arp_waitstate halted 500}


	set dhcsr_val 0
	targets $ppca_tgt
	if {![catch {mrw $::arm::DHCSR} dhcsr_val]} {
		set halt_evt [ expr {$dhcsr_val & $::arm::DHCSR_S_HALT}]
		if {!$halt_evt} {
			echo "[format %s $ppca_tgt]: No halt occured ([format 0x%08X $halt_evt])"
		}
	}

	write32 $ppca_tgt $::arm::DEMCR $::arm::DEMCR_TRCENA

	$ppca_tgt arp_examine
	$ppca_tgt arp_poll
	$ppca_tgt arp_poll
}
