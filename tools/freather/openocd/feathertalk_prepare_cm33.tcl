# FeatherTalk PSE84 programming guards.
#
# OpenOCD's cat1d reset_halt helper returns a Tcl boolean. A false return does
# not fail a command-line session by itself, so every state transition is
# checked here before a flash algorithm is allowed to run.

proc feathertalk_prepare_cm33 {} {
    set rh_code [catch {reset_halt cm33} rh_ok]
    if {$rh_code != 0} {
        error "reset_halt cm33 raised Tcl error: $rh_ok"
    }

    if {$rh_ok == 0} {
        echo "Warn : reset_halt cm33 returned 0; trying guarded XRES fallback"
        set ::ENABLE_ACQUIRE 0
        kitprog3 acquire_config off
        if {[catch {reset_xres} xres_err]} {
            error "CM33 fallback XRES failed: $xres_err"
        }

        set vc_code [catch {reset_halt_vector_catch cat1d.cm33} vc_ok]
        if {$vc_code != 0} {
            error "CM33 fallback vector catch raised Tcl error: $vc_ok"
        }
        if {$vc_ok != 1} {
            error "CM33 fallback vector catch returned 0"
        }
        if {[cat1d.cm33 curstate] ne "halted"} {
            error "CM33 fallback did not leave CM33 halted"
        }
        if {![is_secure_domain cat1d.cm33]} {
            error "CM33 fallback halted outside Secure state"
        }

        set pc [expr {[dict values [cat1d.cm33 get_reg pc]] & ~1}]
        set boot_lo [addr_to_s $::RRAM_MAIN_BASE_CBUS]
        set boot_hi [expr {$boot_lo + $::RRAM_MAIN_OFFSET}]
        if {$pc < $boot_lo || $pc >= $boot_hi} {
            error [format "CM33 fallback unexpected PC 0x%08x" $pc]
        }

        set tm_code [catch {read32 $::CHIPNAME.sys $::TST_MODE} tm]
        if {$tm_code != 0} {
            error "CM33 fallback cannot read Test Mode state: $tm"
        }
        if {($tm & $::TEST_MODE_MSK) != 0} {
            error "CM33 fallback remained in Test Mode"
        }
    } elseif {$rh_ok != 1} {
        error "reset_halt cm33 returned unexpected value: $rh_ok"
    }

    targets cat1d.cm33
    return 1
}

proc feathertalk_restart_cm33_ns {} {
    if {![reset_halt cm33_ns]} {
        error "reset_halt cm33_ns returned 0"
    }
    targets cat1d.cm33
    resume
    return 1
}
