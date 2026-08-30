# Official Infineon Bluetooth assets

The child directories are git submodules, not copied EULA source in the
FeatherRTThread repository:

- `btstack`: headers and precompiled CM33 soft-float host-stack library;
- `bt-fw-ifx-cyw55500a1`: CYW55500A1 controller PatchRAM source container.

Both URLs and revisions are pinned by the parent repository. Initialize them
with `git submodule update --init --recursive` and review each submodule's
`LICENSE.txt` before use or distribution.
