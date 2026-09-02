# minimp3 vendoring record

- Upstream: <https://github.com/lieff/minimp3>
- Commit: `ea99364f61c14656440e8d77e9c233ccf3124633`
- Retrieved: 2026-09-02
- License: CC0-1.0 (`LICENSE` in this directory)
- `minimp3.h` SHA-256: `57E437C5C1F0E8B243885D3929C8973B5E6C778451E0100AB4251D19915CB3AD`
- `LICENSE` SHA-256: `6A1EE543E5282CD9061881EDF462E6FDAB181F328DA71FC2C9A6950A80E94D01`

FeatherTalk defines `MINIMP3_ONLY_MP3` and `MINIMP3_NO_SIMD` in
`feathertalk_player.c`. The decoder therefore builds as portable C for the
Cortex-M55 toolchain and only exposes MPEG Layer III playback.
