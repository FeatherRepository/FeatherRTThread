# LE Audio regression and board diagnostics

Run from the repository root. Host tests require GCC and Python 3.

```powershell
gcc -std=c11 -ffunction-sections -fdata-sections '-Wl,--gc-sections' -Itools/freather/tests -Ibluetooth/port_rtthread -Ibluetooth/reference/btstack/src tools/freather/tests/le_audio_wire_test.c bluetooth/reference/btstack/src/btstack_util.c -o tools/freather/tests/le_audio_wire_test.exe
./tools/freather/tests/le_audio_wire_test.exe
./tools/freather/serial-monitor/python/python.exe ./tools/freather/tests/test_lc3_watermark.py
```

The first test compiles the production unicast implementation with hardware
stubs and checks protocol framing, GATT descriptors, VCS and CCCD behavior.
The second compiles the production watermark function with a deterministic clock.
Neither test proves radio interoperability or audio quality.

For Windows over-air diagnostics, install dependencies locally:

```powershell
./tools/freather/serial-monitor/python/python.exe -m pip install --target ./tools/freather/ble-validation-vendor bleak==3.0.2
./tools/freather/serial-monitor/python/python.exe ./tools/freather/tests/le_audio_ble_probe.py
```

The probe connects and reads services, then disconnects. Optional --pair creates
a local pairing; --unpair removes the matching device's local pairing before
reconnecting. Windows may deny generic application access to audio services.
Use --debug for the access-denied details. Do not treat hidden services as absent.

read_le_trace.py reads the 64-entry redacted ATT/SMP trace via KitProg3.
Its symbol addresses come from the M33 ELF, which must match the flashed image.
Logs, executables and downloaded Python dependencies are not release sources.
