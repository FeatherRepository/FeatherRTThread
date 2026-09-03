# 定位 CPU 实际执行位置: halt -> 读 PC -> resume 2s -> halt -> 再读 PC
# 用于区分: 固件在跑(疑似串口线断) vs 启动链卡死
proc locate {} {
    catch {halt}
    if {[catch {cat1d.cm33 reg pc} pc0] == 0} {
        echo "locate: pc_at_attach=$pc0"
    }
    catch {resume}
    after 2000
    catch {halt}
    if {[catch {cat1d.cm33 reg pc} pc1] == 0} {
        echo "locate: pc_after_2s_run=$pc1"
    }
    if {[catch {cat1d.cm33 reg lr} lr] == 0} {
        echo "locate: lr=$lr"
    }
    # 读当前栈顶附近几个 word 看有没有 fault 帧
    catch {cat1d.cm33 reg msp} sp
    echo "locate: msp=$sp"
    shutdown
}
locate
