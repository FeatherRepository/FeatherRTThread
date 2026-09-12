init
targets cat1d.cm33
halt
echo "=== VTOR ==="
mdw 0xE000ED08
echo "=== PC ==="
reg pc
resume
shutdown
