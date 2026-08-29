# FeatherTalk M55 UI

This directory owns the product UI. Board display and input drivers remain in
the BSP/SDK layers.

The product UI now provides:

- a persistent display-sized shell with status, notification, Alert, and navigation bars
  (the current board is 480 x 800); the notification shade follows downward/upward
  pointer movement and settles using release distance and velocity;
- a bounded notification queue with unread count, stable IDs, swipe-to-delete,
  clear-all, and mask/Home/Back/upward-swipe closing paths;
- Wi-Fi, Bluetooth, PWM brightness, and rotation quick controls, with M33 IPC
  capability/enabled/connected state, Wi-Fi signal strength, and explicit disabled
  states for hardware drivers that are absent;
- a Windows Phone-inspired Start screen and All Apps view;
- an explicit application registry in `feathertalk_ui_pages.c`;
- a bounded, allocation-free route stack with a maximum depth of eight;
- a routed Search page with filtering, a fixed bottom-overlay keyboard with an
  explicit collapse control, and Cortana-style activity animation;
- interactive System, Settings, Media, Messages, Files, and About pages;
- M33 system-status IPC plus status-bar Wi-Fi/Bluetooth connection indicators, with
  explicit unavailable states for missing hardware drivers;
- memory-backed accent, Tile opacity, and background preferences;
- live System Tile updates, FPS/heap/object metrics, and route leak diagnostics;
- runtime accent-color propagation with deleted-object tracking;
- an SVG-source icon system with generated 24/32/48 px A8 assets and runtime recoloring;
- a runtime responsive-layout profile with compact, portrait, large-screen, and landscape breakpoints; and
- a read-only `feather_ui_status` MSH diagnostic command.

Page and control dimensions must use the helpers in `feathertalk_ui_layout.h`.
The layout profile, breakpoint formulas, supported geometries, and new-page
rules are documented in `RESPONSIVE_LAYOUT_zh.md`.

## Icon workflow

Navigation, status, default-app, and media-control icons come from the neutral
SVG artwork under `assets-src/`. `ft_icon_create()` is the only page-level
construction API; it resolves the requested ID and size and optionally tracks
the current accent color. Regenerate the checked-in LVGL descriptors after an
SVG change with:

```powershell
.\tools\freather\build-ui-icons.cmd
```

The firmware consumes generated A8 masks instead of parsing SVG at runtime.
See `UI_ASSET_POLICY_zh.md` and `assets/manifest.json` for format, storage,
attribution, and third-party-brand rules.

## System notification and quick-settings shade

The P0 shade milestone is implemented and dual-core board-tested. Top-edge and
upward drags update the panel and mask every frame; release distance and sampled
velocity select the settle direction. Wi-Fi, Bluetooth, and rotation are backed
by the ABI 4 M33 capability/enabled/connected contract; Wi-Fi additionally carries
signal percentage and maps it to disconnected/weak/medium/strong status icons. The
controls remain visibly unavailable while their board drivers are absent. Brightness is a real M55 `pwm18` control with both a
quick toggle and a slider. Notifications use a fixed-capacity model independent
of LVGL object lifetime and support unread tracking, swipe delete, and clear-all.

## Automatic interaction test

`FEATHERTALK_UI_TEST_MODE` is a product Kconfig option and is disabled by
default. When enabled, an LVGL timer starts the test after boot and sends real
LVGL events from the LVGL thread. The sequence covers notification click/drag,
distance/velocity settling, mask/Home/Back/Search/upward-swipe closing, the four
quick controls, explicit IPC unavailable states, queue unread/single-delete/
clear/overflow behavior, Alert lifecycle, Search keyboard overlay geometry,
collapse/reopen/cancel behavior, Search filtering/routing,
all navigation buttons, six Start tiles,
six All Apps entries, every personalization choice, all Media controls,
Messages/Files actions, transient-object lifecycle cleanup, System-to-Search
address-reuse regression, route depth/overflow/release, and an object-leak check.
It restores all default preferences and the Start screen when complete.

The serial console prints one PASS/FAIL record per assertion followed by:

```text
[UI-TEST] COMPLETE pass=<n> fail=<n> actions=<n> duration=<n>ms
```

`feather_ui_status` also prints the current automatic-test phase and counters.
The checked-in M55 configuration currently enables the option for board
validation; disable `CONFIG_FEATHERTALK_UI_TEST_MODE` for production images.

`lv_user_gui_init()` invokes `feathertalk_ui_init()` from the LVGL thread, so
all initial widget creation and subsequent LVGL event callbacks stay on the
correct thread. See `UI_ASSET_POLICY_zh.md` for the A8/RGB565/ARGB8888
conversion and built-in/external storage policy.
