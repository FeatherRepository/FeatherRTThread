# 直读 TCPWM0 GRP1 CNT9 (pwm18 背光, P20_6/line265): 活动寄存器 + 两次采样验证计数
echo "=== backlight pwm18: GRP1 CNT9 (base 0x42868480) ==="
echo "--- COUNTER t0 ---"
mdw 0x42868488 1
mdw 0x42868490 1 ;# CC0 active
mdw 0x428684A0 1 ;# PERIOD active
sleep 300
echo "--- COUNTER t1 (+300ms) ---"
mdw 0x42868488 1
shutdown
