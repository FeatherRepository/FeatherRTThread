# 用经过验证的 guard 流程先停核 (烧录同款, 对死循环可靠), 再下断点放行捕捉
proc probe {} {
    if {[catch {feathertalk_prepare_cm33} err]} {
        echo "probe: prepare failed: $err"
        shutdown
    }
    bp 0x08394e84 2 hw
    bp 0x08394a4a 2 hw
    bp 0x08394b20 2 hw
    echo "probe: halted@boot + bps set (cpu_reset/hardfault/hf_exc), resume 3s ..."
    resume
    after 3000
    catch {cat1d.cm33 curstate} st
    echo "probe: state=$st"
    if {$st eq "halted"} {
        catch {cat1d.cm33 reg pc} pc
        catch {cat1d.cm33 reg lr} lr
        echo "probe: HALTED pc=$pc lr=$lr (0x08394e84=cpu_reset 0x08394a4a=hardfault 0x08394b20=hf_exc)"
    } else {
        echo "probe: still running after 3s (bp 未命中?)"
    }
    shutdown
}
probe
