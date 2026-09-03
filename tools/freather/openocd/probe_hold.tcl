# 挂住不放行: 循环若停止 => 复位由软件路径触发; 仍复位 => 硬件看门狗/电源
proc hold_and_watch {} {
    catch {halt}
    for {set i 0} {$i < 8} {incr i} {
        if {[catch {cat1d.cm33 curstate} st] != 0} {
            echo "t$i: target gone (reset while halted => HARDWARE reset source)"
            continue
        }
        if {[catch {cat1d.cm33 reg pc} pc] == 0} {
            echo "t$i: state=$st pc=$pc"
        } else {
            echo "t$i: state=$st (pc read fail)"
        }
        after 500
    }
}
hold_and_watch
shutdown
