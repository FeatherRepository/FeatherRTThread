# 全芯片 XRES 硬复位 (两个核+外设一起回零, 等价断电重启)
proc full_reset {} {
    if {[catch {reset_xres} err]} {
        echo "full_reset: xres failed: $err, fallback to reset run"
        reset run
        return
    }
    echo "full_reset: XRES done"
    resume
}
full_reset
shutdown
