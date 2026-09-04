# FeatherTalk Common

This directory owns the source-compatible contract shared by the FeatherTalk
M33 and M55 firmware projects. Keep hardware handles and RT-Thread kernel
objects out of this interface.

The runtime radio resource manager and its coexistence rules are documented in
[RADIO_COEXISTENCE_zh.md](RADIO_COEXISTENCE_zh.md). Wi-Fi and Bluetooth are
independent clients; shared supply ownership is not a feature-macro decision.

The initial contract contains:

- independent M33 and M55 firmware versions;
- one product version;
- a versioned IPC ABI;
- a fixed-width 16-byte message suitable for the PSoC E84 IPC Pipe payload;
- HELLO/HEARTBEAT request and acknowledgement message identifiers;
- common M33, M55, IPC, LVGL, and peer-online status flags.

Any incompatible IPC layout change must increment
`FEATHERTALK_IPC_ABI_VERSION`.
