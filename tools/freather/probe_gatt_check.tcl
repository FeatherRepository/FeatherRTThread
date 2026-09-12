# 在 profile_data 尾段搜 PACS 记录 (01 06 00 00 00 00 12 02 01 80)
init
echo "=== profile_data +0x100/+0x140/+0x180 ==="
mdw 0x6034c138 8
mdw 0x6034c178 8
mdw 0x6034c1b8 8
shutdown
