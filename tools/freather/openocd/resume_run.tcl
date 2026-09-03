# 经 guard 停核后直接放行运行 (bring-up from halt)
proc go {} {
    if {[catch {feathertalk_prepare_cm33} err]} {
        echo "go: prepare failed: $err"
        shutdown
    }
    echo "go: halted at secure boot, resuming app"
    resume
    after 500
    catch {cat1d.cm33 curstate} st
    echo "go: state=$st (应为 running)"
    shutdown
}
go
