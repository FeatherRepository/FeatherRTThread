# 在 AIRCR(0xE000ED0C) 上写观察点: 谁写 SYSRESETREQ 谁就是复位元凶
# 先 guard 停核, 再对双核下观察点, 放行 3s
proc probe {} {
    if {[catch {feathertalk_prepare_cm33} err]} {
        echo "probe: prepare failed: $err"
        shutdown
    }
    catch {cat1d.cm33 wp 0xE000ED0C 4 w} e1
    echo "probe: cm33 wp -> $e1"
    catch {cat1d.cm55 wp 0xE000ED0C 4 w} e2
    echo "probe: cm55 wp -> $e2"
    echo "probe: resume 3s ..."
    resume
    after 3000
    foreach t {cat1d.cm33 cat1d.cm55} {
        if {[catch {$t curstate} st] == 0} {
            echo "probe: $t state=$st"
            if {$st eq "halted"} {
                catch {$t reg pc} pc
                catch {$t reg lr} lr
                echo "probe: $t HALTED pc=$pc lr=$lr  <-- 复位元凶在这里"
            }
        } else {
            echo "probe: $t unreachable"
        }
    }
    shutdown
}
probe
