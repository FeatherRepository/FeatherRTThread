# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-2026, Infineon Technologies AG, or an affiliate of
# Infineon Technologies AG. All rights reserved.

# Subroutines for CAT1B category of microcontrollers.
# The definitions can be overwritten from the family-specific scripts.

namespace eval cat1b {

	namespace import ::arm::*
	namespace import ::ifx::*
	namespace import ::cat1::*

	proc check_debug_token_type {rqst_type} {
		log_proc_entry
		global DEBUG_CERTIFICATE_RQST
		set ret 1

		if {[string toupper $rqst_type] == "OEM"} {
			set DEBUG_CERTIFICATE_RQST $::OEM_DBG_RQST
		} elseif {[string toupper $rqst_type] == "PROT_FW"} {
			set DEBUG_CERTIFICATE_RQST $::PROT_FW_DBG_RQST
		} elseif {$rqst_type != $::OEM_DBG_RQST && $rqst_type != $::PROT_FW_DBG_RQST} {
			puts stderr "** Invalid debug certificate type: '$rqst_type'"
			puts stderr "** Please apply 'OEM' or 'PROT_FW'"
			set ret 0
		}

		log_proc_return $ret
		return $ret
	}

	proc send_debug_certificate {{reset_type "soft"}} {
		log_proc_entry

		global ACQUIRE_TIMEOUT
		global TIMEOUT_RESET_HANDSHAKE
		global WFA_TIMEOUT
		global DEBUG_CERTIFICATE
		set CERTIFICATE_ADDR  0x34004000

		# Hardware registers
		global BOOT_DLM_CTL
		global BOOT_DLM_CTL_2
		global BOOT_DLM_STATUS
		global RES_SOFT_CTL

		# Registers constants
		global RES_SOFT_CTL_RESET_RQST
		global DEBUG_CERTIFICATE_RQST
		global WFA_MASK

		# returns of BOOT_DLM_STATUS
		set CYBOOT_WFA_POLLING             0x0D500080
		set DEBUG_CER_VERIFICATION_SUCCESS 0x0D500084
		set CYBOOT_DEBUG_TOKEN_FAILED      0x0D500085

		set old_target [target current]
		targets $::_TARGET_SYS
		select_current_ap

		mww $BOOT_DLM_CTL $DEBUG_CERTIFICATE_RQST
		# Updating BOOT_DLM_CTL_2 with token start address
		# is optional for generic PSC3 
		# but mandatory for x7/x8 series with PPCA IP
		mww $BOOT_DLM_CTL_2 $CERTIFICATE_ADDR

		if {$reset_type eq "sysresetreq"} {
			# Arm vector catch before reset so core halts at reset vector
			set dhcsr_val [expr {$::arm::DHCSR_DBGKEY_VAL | 0x00000003}] ;# C_DEBUGEN | C_HALT
			catch { write32_adiv6 $::CHIPNAME.dap $::AP_SEL_CM33 $::arm::DHCSR $dhcsr_val }
			catch { write32_adiv6 $::CHIPNAME.dap $::AP_SEL_CM33 $::arm::DEMCR $::arm::DEMCR_VC_CORERESET_VAL }
			echo "issue sysresetreq reset..."
			push_log_settings
			catch { write32_adiv6 $::CHIPNAME.dap $::AP_SEL_CM33 $::arm::AIRCR $::arm::AIRCR_SYSRESETREQ_VAL }
			if {[using_jtag]} {
				dap_handshake
			}
		} else {
			echo "issue software reset..."
			push_log_settings
			catch { mww $RES_SOFT_CTL $RES_SOFT_CTL_RESET_RQST }
		}

		echo "Waiting for a WFA bit"
		set wfa_set 0
		set t_end [expr {[clock milliseconds] + $WFA_TIMEOUT}]
		while {[clock milliseconds] < $t_end} {
			if [catch {mrw $BOOT_DLM_CTL} ctl_status] continue
			if { ($ctl_status & $WFA_MASK) != 0} {
				set wfa_set 1
				break
			}
		}
		pop_log_settings

		if {!$wfa_set} {
			puts stderr "**FAIL: WFA bit was not set"
			log_proc_return 1
			return 1
		}

		# Set random value to check if BOOT_DLM_STATUS changed
		catch {mww $BOOT_DLM_STATUS 0x11223344}

		# Loading debug certificate
		puts "Programming debug token: $DEBUG_CERTIFICATE to $CERTIFICATE_ADDR"
		load_image $DEBUG_CERTIFICATE $CERTIFICATE_ADDR
		mww $BOOT_DLM_CTL_2 $CERTIFICATE_ADDR

		catch {mww $BOOT_DLM_CTL $DEBUG_CERTIFICATE_RQST}

		# Sleep for a time of DAP reconnecting
		sleep [expr {$TIMEOUT_RESET_HANDSHAKE}]

		# Wait until BOOT_DLM_STATUS is changed to get status of token verification
		set status $CYBOOT_WFA_POLLING; set t_end [expr {[clock milliseconds] + $WFA_TIMEOUT}] 
		while { [clock milliseconds] < $t_end } {
			sleep 50
			if [catch {read32_adiv6 $::CHIPNAME.dap $::AP_SEL_SYS $BOOT_DLM_STATUS} status] continue
			if {$status == $DEBUG_CER_VERIFICATION_SUCCESS || $status == $CYBOOT_DEBUG_TOKEN_FAILED} break
		}

		if {$status == $DEBUG_CER_VERIFICATION_SUCCESS} {
			echo "** Debug certificate accepted"
		} else {
			puts stderr [format "** Debug certificate declined - \[0x%08X: %s\]" $status [get_boot_status_str $status]]
		}

		set cm33_open 1
		#check if cm33 ap is enabled
		if { [is_ap_open_adiv6 $::CHIPNAME.dap $::AP_SEL_CM33] == 0 } {
			puts stderr "** CM33 AP was not enabled"
			set cm33_open 0
		} else {
			echo "CM33 AP enabled"
		}

		if {$cm33_open} {
			$::_TARGET_CM33 arp_examine
			$::_TARGET_CM33 arp_poll
			$::_TARGET_CM33 arp_poll
		}

		if {!$cm33_open} {
			error {}
		}

		targets $old_target
		select_current_ap

		log_proc_return 0
		return 0
	}

	proc send_debug_certificate_ns {} {
		log_proc_entry

		global ACQUIRE_TIMEOUT
		global TIMEOUT_RESET_HANDSHAKE
		global WFA_TIMEOUT
		global DEBUG_CERTIFICATE
		set CERTIFICATE_ADDR  0x34004000

		# Hardware registers
		global BOOT_DLM_CTL
		global BOOT_DLM_CTL_2
		global BOOT_DLM_STATUS
		global RES_SOFT_CTL

		# Registers constants
		global RES_SOFT_CTL_RESET_RQST
		global DEBUG_CERTIFICATE_RQST
		global WFA_MASK

		# returns of BOOT_DLM_STATUS
		set CYBOOT_WFA_POLLING             0x0D500080
		set DEBUG_CER_VERIFICATION_SUCCESS 0x0D500084
		set CYBOOT_DEBUG_TOKEN_FAILED      0x0D500085

		set old_target [target current]
		targets $::_TARGET_SYS
		select_current_ap

		mww $BOOT_DLM_CTL $DEBUG_CERTIFICATE_RQST
		# Updating BOOT_DLM_CTL_2 with token start address
		# is optional for generic PSC3
		# but mandatory for x7/x8 series with PPCA IP
		mww $BOOT_DLM_CTL_2 $CERTIFICATE_ADDR

		echo "issue software reset..."
		push_log_settings
		catch { mww $RES_SOFT_CTL $RES_SOFT_CTL_RESET_RQST }

		echo "Waiting for a WFA bit"
		set wfa_set 0
		set t_end [expr {[clock milliseconds] + $WFA_TIMEOUT}]
		while {[clock milliseconds] < $t_end} {
			if [catch {mrw $BOOT_DLM_CTL} ctl_status] continue
			if { ($ctl_status & $WFA_MASK) != 0} {
				set wfa_set 1
				break
			}
		}
		pop_log_settings

		if {!$wfa_set} {
			puts stderr "**FAIL: WFA bit was not set"
			log_proc_return 1
			return 1
		}

		# Set random value to check if BOOT_DLM_STATUS changed
		catch {mww $BOOT_DLM_STATUS 0x11223344}

		# Loading debug certificate
		puts "Programming debug token: $DEBUG_CERTIFICATE to $CERTIFICATE_ADDR"
		load_image $DEBUG_CERTIFICATE $CERTIFICATE_ADDR
		mww $BOOT_DLM_CTL_2 $CERTIFICATE_ADDR

		catch {mww $BOOT_DLM_CTL $DEBUG_CERTIFICATE_RQST}

		# Sleep for a time of DAP reconnecting
		sleep [expr {$TIMEOUT_RESET_HANDSHAKE}]

		# Wait until BOOT_DLM_STATUS is changed to get status of token verification
		set status $CYBOOT_WFA_POLLING; set t_end [expr {[clock milliseconds] + $WFA_TIMEOUT}]
		while { [clock milliseconds] < $t_end } {
			sleep 50
			if [catch {read32_adiv6 $::CHIPNAME.dap $::AP_SEL_SYS $BOOT_DLM_STATUS} status] continue
			if {$status == $DEBUG_CER_VERIFICATION_SUCCESS || $status == $CYBOOT_DEBUG_TOKEN_FAILED} break
		}

		if {$status == $DEBUG_CER_VERIFICATION_SUCCESS} {
			echo "** Debug certificate accepted"
		} else {
			puts stderr [format "** Debug certificate declined - \[0x%08X: %s\]" $status [get_boot_status_str $status]]
		}

		# Check debug policy - sets NS CSW if NS_DEBUG_ONLY, examines CM33
		set ns_policy [lindex [debug_policy] 1]
		if {$ns_policy != "allowed"} {
			puts stderr "** Non-Secure Invasive debug was not enabled" 
		}

		# poll to sync state after examine
		$::_TARGET_CM33 arp_poll
		$::_TARGET_CM33 arp_poll

		# Setup Vector Catch: DHCSR write with C_DEBUGEN | C_HALT halts the core,
		# DEMCR with VC_CORERESET arms vector catch for subsequent resets
		set dhcsr_val [expr {$::arm::DHCSR_DBGKEY_VAL | 0x00000003}] ;# C_DEBUGEN | C_HALT
		write32 $::_TARGET_CM33 $::arm::DHCSR $dhcsr_val
		write32 $::_TARGET_CM33 $::arm::DEMCR $::arm::DEMCR_VC_CORERESET_VAL

		targets $old_target
		select_current_ap

		log_proc_return 0
		return 0
	}

	proc get_LCS {} {
		log_proc_entry

		set BOOTROW_ADDR_SECURE     0x52610180
		set BOOTROW_ADDR_NONSECURE  0x42610180
		set lcsStr "UNKNOWN"

		# Try reading both S and NS aliases to get LCS
		if { [catch {read32_adiv6 $::CHIPNAME.dap $::AP_SEL_SYS $BOOTROW_ADDR_SECURE} lcs]} {
			if { [catch {read32_adiv6 $::CHIPNAME.dap $::AP_SEL_SYS $BOOTROW_ADDR_NONSECURE} lcs]} {
				log_proc_return $lcsStr
				return $lcsStr
			}
		}

		switch [format 0x%x [expr {$lcs & 0xFFFF}]] {
			0x0    { set lcsStr  "VIRGIN" }
			0x29   { set lcsStr  "SORT" }
			0xe9   { set lcsStr  "PROVISIONED" }
			0xc029 { set lcsStr  "NORMAL" }
			0xcc29 { set lcsStr  "NORMAL_NO_SECURE" }
			0xc0e9 { set lcsStr  "DEVELOPMENT"}
			0xc3e9 { set lcsStr  "PRODUCTION" }
			0xf029 { set lcsStr  "RMA_NORMAL" }
			0xfc29 { set lcsStr  "RMA_NORMAL_NO_SECURE" }
			0xf3e9 { set lcsStr  "RMA_SECURE" }
		}

		log_proc_return $lcsStr
		return $lcsStr
	}

	# Gets boot status string out from code
	# Note: don't change function name, dependence on the psc3 driver
	proc get_boot_status_str {status_code} {
		log_proc_entry
		set status_str "None/Unknown"
		foreach val $::boot_status_codes {
			if { $status_code == [lindex $val 1] } { set status_str [lindex $val 0]; break }
		}
		log_proc_return $status_str
		return $status_str
	}

	# Detects and displays the chip info (Silicon ID, Boot version, Life Cycle Stage, etc.)
	proc display_chip_info {chipname {force no} } {
		log_proc_entry
		global AP_SEL_SYS

		# Run info command only once for each particular chip, unless forced
		global ${chipname}::info_runned
		if { [info exists ${chipname}::info_runned] && $force != "force" } {
			log_proc_return
			return
		}
		set ${chipname}::info_runned 1
		push_log_settings

		set SIID_ADDR               0x13400000
		set FAMILY_ADDR             0x13400004
		set ROM_BOOT_VERSION_ADDR   0x1080FFF8
		set ROM_BOOT_BUILD_ADDR     0x1080FFFC
		set BOOT_STATUS_ADDR        0x52200418

		# LCS
		set lcs_str [get_LCS]
		if { $lcs_str != "VIRGIN" &&  $lcs_str != "SORT" &&  $lcs_str != "PROVISIONED"} {
			set SIID_ADDR           0x03400000
			set FAMILY_ADDR         0x03400004
		}

		# Silicon ID, Family, Revision
		set si_id ""
		catch {
			set dev_id [read32_adiv6 $::CHIPNAME.dap $AP_SEL_SYS $SIID_ADDR]
			set si_id [expr {($dev_id & 0xFFFF0000) >> 16 }]
			set si_id [format %X $si_id] ; # Convert to DB style	
			set ::si_id_gl $si_id        ; # Global Silicon ID to get flash size from db

			set si_rev [expr {($dev_id & 0x0000FF00) >> 8 }]
			set CH_REV_MAJOR [expr {($si_rev >> 4) & 0x0F }]
			set CH_REV_MINOR [expr {$si_rev & 0x0F }]
			set CH_REV_MAJOR [expr {$CH_REV_MAJOR == 0 ? 0x3F : $CH_REV_MAJOR + 0x40}]
			set CH_REV_MINOR [expr {$CH_REV_MINOR == 0 ? 0x3F : $CH_REV_MINOR - 1}]

			set si_family [read32_adiv6 $::CHIPNAME.dap $AP_SEL_SYS $FAMILY_ADDR]
			set si_family [expr {$si_family & 0x0000FFFF }]

			echo "***************************************"
			set info_pattern "** Silicon: 0x%s, Family: 0x%03X, Rev.: 0x%02X (%c%X)"
			echo [format $info_pattern $si_id $si_family $si_rev $CH_REV_MAJOR $CH_REV_MINOR]
		}
		detect_device_or_terminate $si_id

		# ROM boot
		catch {
			set rom_boot_ver [read32_adiv6 $::CHIPNAME.dap $AP_SEL_SYS $ROM_BOOT_VERSION_ADDR]
			set rom_boot_build [read32_adiv6 $::CHIPNAME.dap $AP_SEL_SYS $ROM_BOOT_BUILD_ADDR]
			set v_major [expr {($rom_boot_ver & 0x00FF0000) >> 16 }]
			set v_minor [expr {($rom_boot_ver & 0x0000FF00) >> 8 }]
			set v_patch [expr {$rom_boot_ver & 0x000000FF}]
			echo [format "** ROM Boot version: %d.%d.%d.%d" $v_major $v_minor $v_patch $rom_boot_build]
		}

		# Flash boot
		catch {
			set version [read32_adiv6 $::CHIPNAME.dap $AP_SEL_SYS $::FB_VER_HI_ADDR]
			set build [read32_adiv6 $::CHIPNAME.dap $AP_SEL_SYS $::FB_VER_LO_ADDR]
			set fb_v_major [expr {($version & 0x00FF0000) >> 16 }]
			set fb_v_minor [expr {($version & 0x0000FF00) >> 8 }]
			set fb_v_patch [expr {$version & 0x000000FF}]
			echo [format "** Flash Boot version: %d.%d.%d.%d" $fb_v_major $fb_v_minor $fb_v_patch $build]
		}

		# Flash bank mode
		catch {
			if {$::ENABLE_CM33} {
				set flash_mode [read32_adiv6 $::CHIPNAME.dap $::AP_SEL_CM33 $::FLASHC_FLASH_CTL]
				set flash_mode [expr {$flash_mode & $::FLASHC_FLASH_CTL_BANK}]
				set flash_mode [expr {$flash_mode ? "dual" : "single"}]
				echo [format "** Flash Bank Mode : %s bank mode" $flash_mode]
				set ::FLASH_BANK_MODE $flash_mode
			}
		}

		catch {
			set boot_code [read32_adiv6 $::CHIPNAME.dap $::AP_SEL_SYS $BOOT_STATUS_ADDR]
			echo "** Boot Status : [get_boot_status_str $boot_code]"
		}

		echo "** Life Cycle: $lcs_str"
		echo "***************************************"
		pop_log_settings
		log_proc_return
	}

	# Function called to increase standard 1 second timeout of dap init 
	# up to TIMEOUT_BOOT_COMPLETE after sysresetreq in order to acquire
	# x6/x7/x8 targets with multiple secure images or an enhanced encryption.
	proc long_boot_acquire {} {
		push_log_settings
		set ::is_in_dap_init 1

		set srst_state [adapter deassert ]
		if {$srst_state == "srst asserted"} {
			adapter deassert srst
		}

		set t_end [expr {[ms] + $::TIMEOUT_BOOT_COMPLETE}]
		while {[ms] < $t_end} {
			if {![catch {dap init}]} {
				break
			}
		}
		pop_log_settings
		unset ::is_in_dap_init
	}
	
	proc acquire_test_mode {mode} {
		log_proc_entry
		global BOOT_STATUS_ADDR
		global BOOT_STATUS_LISTWND
		global ACQUIRE_TIMEOUT
		set boot_code 0
		set result 1
		set rst_type_num 0

		targets $::_TARGET_SYS
		catch {$::_TARGET_SYS arp_examine}

		if {$mode == "acquire_and_check"} {
			# Preset status register
			push_log_settings
			if { [catch {$::_TARGET_SYS mww $::BOOT_STATUS_ADDR 0}] != 0 } {
				catch {$::_TARGET_SYS mww [addr_to_ns $::BOOT_STATUS_ADDR] 0}
			}
			pop_log_settings

			if {[adapter name] == "kitprog3" && ![using_jtag]} {
				# Standard acquisition flow using kitprog3 and swd
				# TODO: How to check Secure or Non-Secure address should be used here?
				set result [expr {![catch {kitprog3 acquire_psoc}]}]
			} else {
				# Try to Soft Acquire using XRES if implemented
				while {$rst_type_num != $::SOFT_TM_RESET_NUM} {
					set boot_status_upd $boot_code
					set rst_type_in_use [lindex $::SOFT_TM_RESET_TYPES $rst_type_num]
					set result [acquire_in_soft_mode $rst_type_in_use]

					if [catch {set boot_status_upd [$::_TARGET_SYS read_memory $BOOT_STATUS_ADDR 32 1]}] {
						if [catch {set boot_status_upd [$::_TARGET_SYS read_memory [addr_to_ns $BOOT_STATUS_ADDR] 32 1]}] {
							echo [format "Warn : Can't read boot status @0x%08X" $BOOT_STATUS_ADDR]
						}
					}

					if {!$boot_status_upd} {
						puts stderr "**Reset Failed: $rst_type_in_use"
						incr rst_type_num
					} else {
						# Boot status changed, reset and boot occured
						break
					}
				}
			}
			set mode "check_only"
		}

		if {$mode == "check_only" && $result == 1} {
			sleep [scan [adapter srst delay] "adapter srst delay: %d"]

			$::_TARGET_SYS arp_examine

			# Poll for acknowledge code
			set result 0
			set t_end [expr {[clock milliseconds] + $ACQUIRE_TIMEOUT}]
			set boot_code_last $boot_code

			while {[clock milliseconds] < $t_end} {
				if {[catch { set boot_code [ read32_adiv6 $::CHIPNAME.dap $::AP_SEL_SYS $BOOT_STATUS_ADDR]}] == 0} {
					if {$boot_code_last != $boot_code} {
						echo [format "Info : Boot status \[0x%08X: %s\]" $BOOT_STATUS_ADDR [get_boot_status_str $boot_code]]
						set boot_code_last $boot_code
					}
					if {$boot_code == $BOOT_STATUS_LISTWND} {
						set result 1
						break
					}
				} else {
					sleep 100
				}
			}
			if {$boot_code_last == 0} {
				echo [format "Warn : Couldn't read boot status @0x%08X" $BOOT_STATUS_ADDR]
			}
		}

		if {$result} {
			echo "** Target acquired in Test Mode"
		} else {
			puts stderr "** Acquisition in Test Mode FAILED!"
		}

		if {$::NS_DEBUG_ONLY} {
			catch {acquire_in_wfa_mode_ns}
		} else {
			catch {acquire_in_wfa_mode}
		}

		if {$::ENABLE_CM33} {
			targets $::_TARGET_CM33
		}

		log_proc_return
	}

	proc acquire_in_soft_mode {res_type} {
		log_proc_entry

		echo "** Attempting to soft-acquire chip in Test Mode using reset $res_type..."
		global TIMEOUT_RESET_HANDSHAKE
		global TIMEOUT_BOOT_COMPLETE
		global RES_SOFT_CTL
		global RES_SOFT_CTL_RESET_RQST

		push_log_settings 1
		push_polling
		set ::is_in_dap_init 1

		switch $res_type {
			"XRES" {
				# Prereset - do hardware reset (XRES)
				# And handshake - wait while SWJ pins are enabled after the reset so we can connect to DAP
				reset_config srst_only; adapter assert srst; sleep 100; adapter deassert srst; reset_config none
			}
			"SOFT" {
			catch { mww $RES_SOFT_CTL $RES_SOFT_CTL_RESET_RQST }
			}
			default {
				reset_config srst_only; adapter assert srst; sleep 100; adapter deassert srst; reset_config none
			}
		}

		set result 0; set t_end [expr {[ms] + $TIMEOUT_RESET_HANDSHAKE + $TIMEOUT_BOOT_COMPLETE}]
		while {[ms] < $t_end} {
			if {[catch {dap init; mww $::TST_MODE_REQ $::TST_MODE}] == 0} {
				set result 1; break
			}
		}

		unset ::is_in_dap_init
		pop_polling
		pop_log_settings 1

		log_proc_return $result
		return $result
	}

	proc acquire_in_wfa_mode_ns {} {
		log_proc_entry

		set token [expr {[info exists ::DEBUG_CERTIFICATE] && [file exists $::DEBUG_CERTIFICATE]}]

		if {$::ENABLE_CM33} {

			# Check DAUTHSTATUS - if NS debug is enabled, no WFA needed
			set ns_policy [debug_policy]
			if {[lindex $ns_policy 1] == "allowed"} {
				echo "** NS debug enabled, skipping WFA procedure"
				log_proc_return
				return
			}

			# NS_DEBUG_ONLY requires OEM token to open NS debug
			if {$token && $::DEBUG_CERTIFICATE_RQST == $::OEM_DBG_RQST} {
				echo "** Sending debug certificate (NS_DEBUG_ONLY mode)"
				if [catch {send_debug_certificate_ns}] {
					puts stderr "** Connect via debug certificate failed, examination skipped"
				} else {
					# Prevent OpenOCD from issuing sysresetreq - send_debug_certificate_ns already does soft reset
					$::_TARGET_CM33 configure -event reset-assert {}
					# No reset-deassert-post needed - send_debug_certificate_ns already examined and halted

					$::_TARGET_CM33 configure -event reset-deassert-pre {
						# NS CSW is already configured by send_debug_certificate_ns via debug_policy
						# Just ensure AP is selected correctly
						$::CHIPNAME.dap apsel $::AP_SEL_CM33
						$::CHIPNAME.dap apcsw $::AP_CSW_CM33 $::AP_CSW_CM33_MASK
					}
				}
			} else {
				puts stderr "** CM33 AP is closed and no OEM certificate specified, examination skipped"
				puts stderr "** NS_DEBUG_ONLY mode requires OEM token type"
				puts stderr "** Use 'DEBUG_CERTIFICATE' variable to specify certificate filename with full path"
				exit 0;
			}
		}

		log_proc_return
	}

	proc acquire_in_wfa_mode {} {
		log_proc_entry

		set token [expr {[info exists ::DEBUG_CERTIFICATE] && [file exists $::DEBUG_CERTIFICATE]}]

		if {$::ENABLE_CM33} {
			if {[is_ap_open_adiv6 $::CHIPNAME.dap $::AP_SEL_CM33] == 0  } {

			$::_TARGET_CM33 configure -defer-examine
				
				if {$token && [check_debug_token_type $::DEBUG_CERTIFICATE_RQST]} {
					echo "** Sending debug certificate"
					if [catch {send_debug_certificate}] {
						puts stderr "** Connect via debug certificate failed, examination skipped"
					} else {
						# Prevent OpenOCD from issuing sysresetreq - send_debug_certificate already does soft reset
						$::_TARGET_CM33 configure -event reset-assert {}
						$::_TARGET_CM33 configure -event reset-deassert-post "event_cm33_reset_deassert_post"

						$::_TARGET_CM33 configure -event reset-deassert-pre {
							catch {send_debug_certificate sysresetreq}
							read_and_init_secure $::_TARGET_CM33
						}
					}
				} else {
						puts stderr "** CM33 AP is closed and no certificate specified, examination skipped"
						puts stderr "** Use 'DEBUG_CERTIFICATE' variable to specify certificate filename with full path"
						exit 0;
				}
			} else {
				$::_TARGET_CM33 configure -event reset-deassert-post "event_cm33_reset_deassert_post"
			}
		}

		log_proc_return
	}

	proc read32_adiv6 { dap ap address } {
		push_log_settings
		catch {
			$dap apreg $ap 0xD00 0xAB000002
			$dap apreg $ap 0xD04 $address
			$dap apreg $ap 0xD0C
		} result options
		pop_log_settings
		return {*}$options [string trim $result]
	}

	proc write32_adiv6 { dap ap address val } {
		$dap apreg $ap 0xD00 0xAB000002
		$dap apreg $ap 0xD04 $address
		$dap apreg $ap 0xD0C $val
	}

	# Function check SID and NSID fields in DAUTHSTATUS
	# in order to check Secure/Non-Secure debug availability
	# Returns list of Secure/Non-Secure debug availability
	proc debug_policy {} {
		log_proc_entry
		set ret {"prohibited" "prohibited"}

		set DAUTHSTATUS_ADDR              0xE000EFB8
		set DAUTHSTATUS_NSID_MASK         0x00000003
		set DAUTHSTATUS_SID_MASK          0x00000030
		set DAUTHSTATUS_SID_PROHIBITED    0x00000020
		set DAUTHSTATUS_NSID_PROHIBITED   0x00000002

		if {$::NS_DEBUG_ONLY == 1} {
			$::CHIPNAME.dap apsel $::AP_SEL_CM33
			$::CHIPNAME.dap apcsw $::AP_CSW_CM33 $::AP_CSW_CM33_MASK
		}
		push_log_settings
		if {![catch {$::_TARGET_CM33 arp_examine; $::_TARGET_CM33 read_memory $DAUTHSTATUS_ADDR 32 1} dauthstatus]} {
			set sid  [expr {$dauthstatus & $DAUTHSTATUS_SID_MASK}]
			set nsid [expr {$dauthstatus & $DAUTHSTATUS_NSID_MASK}]

			set sid_str  [expr {$sid == $DAUTHSTATUS_SID_PROHIBITED ? "prohibited" : "allowed"}]
			set nsid_str [expr {$nsid == $DAUTHSTATUS_NSID_PROHIBITED ? "prohibited" : "allowed"}]

			echo [format "Secure Invasive debug: %s" $sid_str]
			echo [format "Non-Secure Invasive debug: %s" $nsid_str]

			set ret [list $sid_str $nsid_str]
		} else {
			echo "Warn : Unable to read DAUTHSTATUS"
		}
		pop_log_settings

		log_proc_return $ret
		return $ret
	}

	proc is_ap_open_adiv6 { {dap {}} {ap {}} } {
		log_proc_entry
		push_log_settings
		push_polling

		set ret 0
		# read location of the ROM Table to check if AP is opened
		if ![catch {
				set dbg_base [string trim [$dap apreg $ap 0xDF8]]
				read32_adiv6 $dap $ap $dbg_base
			}] {
			set ret 1
		}

		pop_polling
		pop_log_settings
		log_proc_return $ret
		return $ret
	}

	proc define_flash_banks_psc3 {} {
		log_proc_entry

		flash bank ${::_TARGET_CM33}.main0_s        cmsis_flash 0x32000000 0 4 4 $::_TARGET_CM33 $::FLASH_FLASHLOADER 1024 prefer_sector_erase
		flash bank ${::_TARGET_CM33}.main0_ns       cmsis_flash 0x22000000 0 4 4 $::_TARGET_CM33 $::FLASH_FLASHLOADER 1024 prefer_sector_erase
		flash bank ${::_TARGET_CM33}.main0_cbus_s   virtual     0x12000000 0 4 4 $::_TARGET_CM33 ${::_TARGET_CM33}.main0_s
		flash bank ${::_TARGET_CM33}.main0_cbus_ns  virtual     0x02000000 0 4 4 $::_TARGET_CM33 ${::_TARGET_CM33}.main0_ns

		flash bank ${::_TARGET_CM33}.super_s        psc3    0x33400000 0x8000 4 4 $::_TARGET_CM33
		flash bank ${::_TARGET_CM33}.super_ns       psc3    0x23400000 0x8000 4 4 $::_TARGET_CM33
		flash bank ${::_TARGET_CM33}.super_cbus_s   virtual 0x13400000 0x8000 4 4 $::_TARGET_CM33 ${::_TARGET_CM33}.super_s
		flash bank ${::_TARGET_CM33}.super_cbus_ns  virtual 0x03400000 0x8000 4 4 $::_TARGET_CM33 ${::_TARGET_CM33}.super_ns

		log_proc_return
	}

	proc select_current_ap {} {
		variable dap
		variable ap

		set target [target current]
		set dap [$target cget -dap]
		set ap  [$target cget -ap-num]
	}

	# Set flash banks size
	# Function sets flash size from database cympn.cfg
	# If db does not contain mpn, then maximum possible size from serie is set
	# If optional arg 'flash_size' is given, that value is used instead of MAIN_FLASH_SIZE_MAX
	proc update_flash_size {{flash_size "default"} {force no}} {
		log_proc_entry

		# Run setting flash size once per session, unless forced
		global flash_set
		if { [info exists flash_set] && $force != "force" } {
			log_proc_return
			return
		}
		set flash_set 1

		if {$flash_size == "default"} {
			set flash_size $::MAIN_FLASH_SIZE_MAX
		}

		if {[info exists ::si_id_gl]} {
			if { [dict exists $::MPN $::si_id_gl] } {
				set flash_size [lindex $::MPN($::si_id_gl) 2]
				detect_wrong_config $::si_id_gl
				terminate_if_wrong_config

				if {[string is integer -strict $flash_size]} {
					# Convert size from KiB to bytes
					set flash_size [ expr {$flash_size << 10}]
					cmsis_flash set_bank_size ${::_TARGET_CM33}.main0_s $flash_size
					cmsis_flash set_bank_size ${::_TARGET_CM33}.main0_ns $flash_size
				}
			} else {
				# Unable to fetch data from MPN db, set flash bank size as a max size of the serie
				# or 'flash_size' argument if given
				cmsis_flash set_bank_size ${::_TARGET_CM33}.main0_s $flash_size
				cmsis_flash set_bank_size ${::_TARGET_CM33}.main0_ns $flash_size
			}
		}

		# Check if MCU in dual bank mode and change flash banks if necessary
		if {[info exists ::FLASH_BANK_MODE] && $::FLASH_BANK_MODE == "dual"} {
			set db_size [expr {$flash_size/2}]

			cmsis_flash add_dual_bank_pair ${::_TARGET_CM33}.main0_s  ${::_TARGET_CM33}.main1_s  0x32800000 $db_size
			cmsis_flash add_dual_bank_pair ${::_TARGET_CM33}.main0_ns ${::_TARGET_CM33}.main1_ns 0x22800000 $db_size
			
			# Creates C-bus virtual banks for dualbank mode
			cmsis_flash add_virtual_bank   ${::_TARGET_CM33}.main1_s  ${::_TARGET_CM33}.main1_cbus_s  0x12800000
			cmsis_flash add_virtual_bank   ${::_TARGET_CM33}.main1_ns ${::_TARGET_CM33}.main1_cbus_ns 0x02800000
		}

		log_proc_return
	}

	# Perform full chip erase
	proc erase_all {} {
		log_proc_entry
		puts "Erasing entire flash.."
		lset banks [flash list]
		set banks_count [llength $banks]
		for {set i [expr {$banks_count - 1}]} { $i >= 0 } { incr i -1 } {
			set bank [lindex $banks $i]
			set bank_driver $bank(driver)
			set bank_name $bank(name)

			# Full chip erase is done via Non-secure alias
			# SFlash, virtual and secure flash banks skipped
			if { $bank_driver != "virtual" && [string first "super" $bank_name] == -1 && [string first "_ns" $bank_name] != -1} {
				echo [format "Erasing flash bank \"%s\"..." $bank_name]
				flash erase_sector $i 0 last
			}
		}
		log_proc_return
	}
	add_help_text erase_all "Erases all non-virtual flash banks"

}
