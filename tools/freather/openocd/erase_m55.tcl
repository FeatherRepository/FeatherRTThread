# 二分: 只擦 M55 镜像区 (0x60580000 + 8MB), 保留 M33, 看 M33 是否还循环
proc erase_m55 {} {
    if {[catch {feathertalk_prepare_cm33} err]} {
        echo "erase_m55: prepare failed: $err"
        shutdown
    }
    echo "erase_m55: erasing M55 region 0x60580000..0x60D80000 ..."
    if {[catch {flash erase_address 0x60580000 0x00800000} err]} {
        echo "erase_m55: erase failed: $err"
        shutdown
    }
    echo "erase_m55: done (M55 镜像已擦除, M33 独跑)"
    resume
    shutdown
}
erase_m55
