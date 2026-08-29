# FeatherTalk Common

This directory owns the source-compatible contract shared by the FeatherTalk
M33 and M55 firmware projects. Keep hardware handles and RT-Thread kernel
objects out of this interface.

The initial contract contains:

- independent M33 and M55 firmware versions;
- one product version;
- a versioned IPC ABI;
- a fixed-width IPC frame header suitable for transport over the platform IPC.

Any incompatible IPC layout change must increment
`FEATHERTALK_IPC_ABI_VERSION`.
