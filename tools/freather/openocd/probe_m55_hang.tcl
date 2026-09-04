# M55 挂死取证: halt cm55 -> 读 PC/LR/SP + CFSR 快照
echo "probe: halting cat1d.cm55..."
catch {cat1d.cm55 halt} r
echo "halt: $r"
if {[catch {cat1d.cm55 curstate} st] == 0} {
    echo "state: $st"
}
if {[catch {cat1d.cm55 reg pc} pc] == 0} { echo "PC: $pc" }
if {[catch {cat1d.cm55 reg lr} lr] == 0} { echo "LR: $lr" }
if {[catch {cat1d.cm55 reg sp} sp] == 0} { echo "SP: $sp" }
if {[catch {cat1d.cm55 reg xpsr} x] == 0} { echo "xPSR: $x" }
# CFSR (0xE000ED28) / HFSR (0xE000ED2C)
if {[catch {mdw 0xE000ED28} cfsr] == 0} { echo "CFSR: $cfsr" }
if {[catch {mdw 0xE000ED2C} hfsr] == 0} { echo "HFSR: $hfsr" }
shutdown
