# 擦除 M55 偏好存储区 (FAL, 0x60E00000 + 2MB), 强制回默认设置
proc erase_storage {} {
    if {[catch {feathertalk_prepare_cm33} err]} {
        echo "erase_storage: prepare failed: $err"
        shutdown
    }
    echo "erase_storage: erasing FAL storage 0x60E00000..0x61000000 ..."
    if {[catch {flash erase_address 0x60E00000 0x00200000} err]} {
        echo "erase_storage: erase failed: $err"
        shutdown
    }
    echo "erase_storage: done"
    shutdown
}
erase_storage
