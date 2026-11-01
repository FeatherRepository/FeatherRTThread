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

## Bluetooth warm restart

Use the M55 FeatherTalk console (COM17 in the current setup) and require
three complete cycles:

```text
bt_status
bt_off
bt_on
bt_status
```

Each `bt_on` must return to `enabled=yes` within 15 seconds without a board
reset. The LEA probe must still discover `FeatherTalk-LE` afterwards.

## A2DP Source headphones

Set the A2DP role to SOURCE, then use the M55 console (the same quick IPC path
as the UI):

```text
feather_bt_source role source
feather_bt_source scan
feather_bt_source status
feather_bt_source connect 0
feather_bt_source status
feather_bt_source disconnect
```

The UI path is `Settings -> Bluetooth -> Bluetooth headphones (source)`.
Scanning is user-driven and no longer auto-connects the first audio device.
Windows may need the FeatherTalk pairing removed and recreated after the board
was reflashed because the controller-side bond database can change. Remove only
the FeatherTalk entry, never unrelated host pairings.

## A2DP Source input selection

The A2DP Source is not fixed to USB audio. Its encoder reads exactly one tap:
the USB host or the local media player. The UI toggle is the `音源: USB/本地`
button in the headphone scan row.

```text
feather_bt_source status
feather_bt_source input usb
feather_bt_source status
feather_bt_source input player
feather_bt_source status
feather_player status
```

`input usb` forwards the USB host and mutes the UAC worker's local `sound0`
path so the same PCM is not played twice. `input player` leaves UAC local
playback alone and reads the player tap instead. The encoder never holds
`sound0`, so local speaker playback can coexist with either source. The media
page output selector is coupled to this setting: `Bluetooth` selects the local
player, `Speaker` selects USB. Switching the input restarts a playing local
track on the new output. Without a connected A2DP peer the encoder stays
inactive, so these commands verify the selection and status path only; real
USB-to-BT and file-to-BT listening still requires a USB host and a classic
headphone.

## Local player output routing

The media page can send local WAV/MP3 playback either to the onboard speaker
or to a connected A2DP Source headphone. The UI path is `Media -> Output
device`; the console exposes the same switch:

```text
feather_player status
feather_player output speaker
feather_player output bt
feather_player status
ft_sbc_enc_stats
```

`feather_player status` must report the selected output and any stream error.
`output bt` is rejected by the UI when no headphone is connected; on the
console it blocks for at most two seconds waiting for the SBC encoder and then
enters the `error` state. With a headphone connected and a track playing,
`ft_sbc_enc_stats` must show `active=1`, growing `frames`, and a non-zero
`ring2 used`. Switching back to `speaker` restarts the current track on
`sound0` without changing pause/play state.
