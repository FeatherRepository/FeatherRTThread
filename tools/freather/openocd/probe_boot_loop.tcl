# 抓启动死循环: 复位停在入口 -> 下 HardFault 断点 -> 放行, 看是否命中
proc run_probe {} {
    reset halt
    # HardFault_Handler @ 0x08394a4a, rt_hw_hard_fault_exception @ 0x08394b20
    bp 0x08394a4a 2 hw
    bp 0x08394b20 2 hw
    echo "probe: resumed, waiting 2s for fault..."
    resume
    set t0 [clock milliseconds]
    set halted 0
    while {[expr {[clock milliseconds] - $t0}] < 2500} {
        if {[catch {cat1d.cm33 curstate} st] == 0 && $st eq "halted"} {
            set halted 1
            break
        }
        after 50
    }
    if {$halted} {
        echo "probe: HALTED (fault breakpoint hit!)"
        catch {cat1d.cm33 reg pc} pc
        echo "probe: pc=$pc"
        catch {cat1d.cm33 reg}
    } else {
        echo "probe: no fault bp hit in 2.5s (not a software fault -> watchdog/power)"
    }
}
run_probe
shutdown
