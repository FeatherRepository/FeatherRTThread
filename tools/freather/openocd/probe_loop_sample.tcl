# 循环中多次采样 PC: 看落在哪
proc sample {n} {
    for {set i 0} {$i < $n} {incr i} {
        catch {resume}
        after 120
        catch {halt}
        if {[catch {cat1d.cm33 reg pc} pc] == 0} {
            echo "sample $i: pc=$pc"
        }
    }
}
sample 6
shutdown
