init
targets cat1d.cm33
echo "=== profile_data via exec alias 0x0834c038 (+0x180 tail) ==="
mdw 0x0834c038 4
mdw 0x0834c1b8 8
resume
shutdown
