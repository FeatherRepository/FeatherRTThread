# 全片恢复: guard 停核 -> 擦除外部 flash 应用区+存储区 (0x60340000..0x61000000)
# 目的: 清掉被中断烧录闩住的 flash 挂起态 (XIP 取指间歇出错 -> 双核死循环)
proc recover {} {
    if {[catch {feathertalk_prepare_cm33} err]} {
        echo "recover: prepare failed: $err"
        shutdown
    }
    echo "recover: erasing external flash 0x60340000..0x61000000 ..."
    if {[catch {flash erase_address 0x60340000 0x00CC0000} err]} {
        echo "recover: erase failed: $err"
        shutdown
    }
    echo "recover: erase done"
    shutdown
}
recover
