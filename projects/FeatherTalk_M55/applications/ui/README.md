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
- an explicit application registry in `feathertalk_ui_pages.c`, split into mutable
  common Tile properties (name, grid span, opacity, and pattern) and app-owned
  private properties (icon, live-loop policy, period, content callback, and context);
- long-press Start Tile editing with an internally clipped body-scale animation,
  body-drag movement,
  four inset 90-degree corner Chevron resize handles, floating pointer tracking,
  live sibling
  reflow, and release-time grid snapping;
- a bounded, allocation-free route stack with a maximum depth of eight;
- a routed Search page with filtering, a fixed bottom-overlay keyboard with an
  explicit collapse control, and Cortana-style activity animation;
- interactive System, Settings, Media, Gallery, Files, and About pages;
- a detailed System inventory that separates physical capacity, linker allocation,
  and live M55 usage across CPU/clock domains, on-chip ROM/RRAM/RAM, external
  Flash/HyperRAM, communication links, display transport, and registered drivers;
- M33 system-status IPC plus status-bar Wi-Fi/Bluetooth connection indicators, with
  explicit unavailable states for missing hardware drivers;
- power-loss-safe per-device accent, Tile opacity, language/time, background,
  and wallpaper preferences stored as CRC-protected A/B records on `/flash`;
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
upward drags update the panel at most once for each changed pointer position;
repeated `PRESSING` samples at the same Y coordinate are ignored. The dimming mask
uses 12 progress levels instead of invalidating the almost full-screen alpha layer
for every pixel of motion. Release distance and sampled velocity select the settle
direction. While the shade is being dragged or settling, the one-second status,
Live Tile, and quick-control refresh is deferred so it cannot interrupt the gesture.
Wi-Fi, Bluetooth, and rotation are backed
by the ABI 4 M33 capability/enabled/connected contract; Wi-Fi additionally carries
signal percentage and maps it to disconnected/weak/medium/strong status icons. The
controls remain visibly unavailable while their board drivers are absent. Brightness is a real M55 `pwm18` control with both a
quick toggle and a 0-100% slider. The user range maps to a safe 50-100% hardware
duty range, so the minimum remains visible instead of switching the panel black.
Its displayed value is read back from the RT-Thread
PWM device instead of relying on a UI-only cache. Availability also requires P20_6
to be routed to TCPWM0 line 265, preventing a GPIO override from silently accepting
brightness changes that never reach the panel. Notifications use a fixed-capacity model independent
of LVGL object lifetime and support unread tracking, swipe delete, and clear-all.
Each notification card owns a horizontal press/drag/release state machine: it follows
the pointer using style translation, deletes after one-quarter-card travel or a fast
horizontal release, and otherwise eases back in 160 ms. Vertical input remains with
the scrolling list and the shade-close gesture. All shade and dynamically created
card labels select the Noto Sans SC application fonts explicitly, so stock-widget
font inheritance cannot reintroduce CJK placeholder boxes.
The model carries a revision; opening an unchanged or empty queue reuses the existing
LVGL tree instead of cleaning and rebuilding every notification card. Quick-control,
radio-status, label, and external Live Tile setters also skip values already on screen.

Performance reporting distinguishes the 18 ms LVGL refresh scheduler from frames
that actually rendered and reached the final display flush. `feather_ui_status`
reports present FPS, scheduler Hz, render/flush totals, flushed pixels per second,
last/peak render time, and shade hot-path applied/skipped counters. Consequently an
idle screen correctly reports `present-fps=0` while `refresh-hz` remains about 55.

## Hardware-aware Settings application

Settings is a routed application rather than a single personalization form. Its
root page has a local search field with the same fixed, collapsible keyboard overlay
contract as application Search. Filtering checks category names, descriptions, and
hardware keywords. Selecting a row pushes a bounded child route, so shell Back/Home
semantics and object-lifetime cleanup remain unchanged.

The visible categories are limited to settings and hardware that exist on the
current product: Display & brightness, Wi-Fi, Bluetooth, Storage, USB,
Time & language, and Personalization. Time & language provides 12/24-hour status-bar formatting, seven
fixed UTC offsets, and Simplified Chinese/English selection. It defaults to
Simplified Chinese, UTC+08:00, and 24-hour time; the M33 RTC remains the time source.
Notification queue management remains in the notification shade, while Storage
owns Flash/SD capacity, formatting and browse actions. Runtime diagnostics and About
remain routed Settings children rather than duplicate standalone applications.
Cellular/SIM/phone, hotspot, VPN, NFC, account, manual RTC setting, application uninstall,
and every status-only placeholder from the Windows Phone visual reference are
intentionally absent. Display brightness directly uses the M55 `pwm18` adapter.
Wi-Fi and Bluetooth read the ABI 4 M33 capability/enabled/connected fields, plus
Wi-Fi signal percentage, and visibly disable their action when the corresponding
M33 service is not enabled. Storage, RTC, locale, rotation, and other incomplete
board services state the missing driver or persistence boundary instead of exposing
an in-memory switch that would pretend to change hardware.

Time format, fixed offset, language, accent, Tile opacity, background, and the
selected wallpaper path share a per-device A/B preference store under
`/flash/.feathertalk`. Each record has a schema, generation and CRC32; writes are
debounced for two seconds and use the inactive slot with readback verification.
USB export and local Flash formatting freeze the writer first. If the host formats
the 2 MiB Flash volume, the live settings are automatically re-seeded after remount.
The UTC-offset selector does not
pretend to provide a time-zone database or automatic daylight-saving changes. Wi-Fi
and Bluetooth use clean, dedicated SVG assets: a symmetric three-level Wi-Fi mark
and a standard outline Bluetooth rune, without the former slider overlays.

The display language is global rather than local to Settings. Switching between
Simplified Chinese and English refreshes the persistent status/quick-settings shell
in place and reconstructs the active route stack, so Start Tile names, All Apps,
search, application pages, Settings options, notifications and system information
change together. User-renamed Tiles are treated as private data and are not overwritten.

Chinese text uses an offline-generated Noto Sans SC product font. The checked-in LVGL
C assets contain all 6,763 GB2312 level-1/level-2 Han characters plus extra punctuation
and UI-source glyphs at 12/14/16/22 px, 2 bpp. Montserrat remains the ASCII/symbol
fallback. The full TTF and converter packages are host-only cache files; regenerate
the tracked fonts with `tools\freather\fonts\build-ui-fonts.cmd`. Source provenance,
the pinned SHA-256 and the SIL OFL 1.1 license are documented under
`tools/freather/fonts/`.

## System information application

System uses a compact overview-first layout. Four responsive cards show storage,
external RAM, on-chip RAM, and processor information at a glance. A 480 x 800 screen
uses a 2 x 2 card grid, while wider landscape profiles can place all four cards on
one row. Device specifications are expanded initially; Memory & storage, Interfaces
& peripherals, and Runtime status are collapsed accordions, so the page does not
present every diagnostic value as one long wall of text.

Opening the detail groups reveals the full inventory: live M55/M33/GFX/NPU clock
domains and cache state; physical on-chip ROM, RRAM, TCM, M33 SRAM and SoC/GFX SRAM;
exact linker-derived M55 XIP, DTCM and display-buffer occupation; RT-Thread internal
and HyperRAM heap current/peak usage; the complete 16 MiB external Flash and 16 MiB
HyperRAM partitioning; SMIF, MIPI-DSI, UART, I2C, IPC, PWM and AXI-DMA settings; and
the actual RT-Thread device registry.

Values outside the M55 ownership boundary are labelled as allocated, unavailable,
or not observable instead of being presented as live usage. The source hierarchy,
address map, units and maintenance rules are documented in
`SYSTEM_INFORMATION_zh.md`.

## Automatic interaction test

`FEATHERTALK_UI_TEST_MODE` is a product Kconfig option and is disabled by
default. When enabled, an LVGL timer starts the test after boot and sends real
LVGL events from the LVGL thread. The sequence covers notification click/drag,
same-coordinate redraw suppression, mask update bounding, unchanged-queue reuse,
real render/flush counters, distance/velocity settling,
mask/Home/Back/Search/upward-swipe closing, the four
quick controls, explicit IPC unavailable states, queue unread/single-delete/
clear/overflow behavior, Alert lifecycle, Search keyboard overlay geometry,
collapse/reopen/cancel behavior, Search filtering/routing,
all navigation buttons, four Start tiles,
four All Apps entries, Settings search/keyboard and every hardware category route,
real Settings brightness write/readback, 12/24-hour, UTC-offset and language restore
checks, every built-in personalization choice, all Media controls,
Gallery/Files actions, System summary/accordion state, transient-object lifecycle cleanup, System-to-Search
address-reuse regression, Tile model/edit-handle/reorder/resize/property/live-content
behavior, route depth/overflow/release, and an object-leak check.
It snapshots the device preferences, suppresses Flash writes during the run, exercises
the reset path, and restores the original per-device preferences when complete.

## Start Tile application contract

Each entry in `s_apps` is an application, while its Start Tile is a mutable runtime
view of that descriptor. `ft_tile_common_properties_t` owns the reusable shell
properties: display name, one-to-N column span, one-to-three row span, per-Tile
opacity, and an optional low-opacity pattern icon. The shell multiplies the local
opacity by the global Start Tile preference.

`ft_tile_private_properties_t` belongs to the application. It supplies the app icon
and may opt into a periodic live-content callback. The callback receives a transparent
LVGL content host owned by the Tile; the shell schedules frames but does not create or
interpret the application's children. Media currently creates and cycles track text,
while System receives external M33 status. Gallery is a normal routed application;
its Start Tile does not need desktop-specific image-decoder logic.

## Music Cover Flow

The Music page uses five virtual album cells around the selected track. Horizontal
dragging has momentum, scroll-one containment, and center snapping; tapping a side
cover scrolls it into selection. After a snap, the cells are rebound around the new
track so browsing remains circular without allocating an unbounded list. This is a
classic Cover Flow transition rather than a flat carousel: the selected cover faces
forward, covers on either side turn inward around the visual Y axis, compress in
width, recede in height, and darken at the far edge. All values are continuously
interpolated from scroll distance and reverse as a cover crosses the center. The
previous/next controls use the same animated scroll-and-snap path as touch dragging.
Track, artist, album, play/pause, and volume state stay synchronized.

Music is backed by a real folder playlist rather than demonstration labels. The
source row opens a directory picker rooted at `/sdcard` and `/flash`; the direct
WAV/MP3 children of the selected directory become the playlist (up to 24 validated
files). Scanning is deliberately non-recursive, so choosing a folder also defines
the album/loop boundary. The default is `/sdcard/Music`; choosing
`/sdcard/Recordings` exposes Recorder output without turning the Music app into a
second Files browser. Folder loop advances to the next entry and wraps to index 0.

RIFF chunks are parsed instead of assuming a fixed 44-byte payload offset. PCM WAV
formats accepted by `sound0` are streamed directly. MP3 Layer III is decoded by the
vendored official `lieff/minimp3` single-header decoder (CC0-1.0; source and license
live in `applications/third_party/minimp3`). The common 44.1 kHz MP3 case is
converted to the board's supported 48 kHz PCM output before RT-Thread Audio updates
TDM0 and ES8388. The selected file's source format, elapsed time, duration, and
play/pause state are reflected in the details panel. Playback continues when the
Music page is closed.

`sound0` is a single physical output. Local Music and USB Audio claim it through
the shared audio-owner arbiter; a second client receives `-RT_EBUSY` instead of
opening the codec concurrently. The commands `feather_player dir [path]`, `scan`,
`list`, `loop <0|1>`, `play <index>`, `pause`, `resume`, `stop`, and `status`
expose the same backend for board diagnosis. AAC and FLAC are not currently listed
or decoded.

The stage is responsive: portrait builds stack Cover Flow above transport controls,
while 90/270-degree landscape builds use a wide two-column player. Display rotation
is currently selected by the board build because the LCD scanout geometry and touch
transform are compile-time settings; opening Music does not silently rotate the
entire device at runtime.

The cover face stays on LVGL's ordinary GPU rectangle object path. The earlier
two-triangle face produced an antialiased diagonal seam, while both a dynamic closed
path and custom per-frame rectangle injection eventually stalled the PSE84 draw-task
lifecycle under repeated animated turns. The cover still compresses horizontally,
recedes vertically, fades, and gains a far-edge shade to preserve the Cover Flow
depth cue without runtime path construction. Side-cover typography is culled once it turns away;
the selected cover and the details panel retain the readable title. Cover fading is
applied to individual primitives. Parent-group opacity is deliberately forbidden
because it creates off-screen LVGL layers and multiple GPU submissions. The current
implementation remains one GPU command chain and one submit per full frame.

## Gallery and wallpaper

The Messages application was removed because this board has no cellular/SMS receive
path. Its stable application slot now hosts Gallery. Gallery exposes Flash and SD as
separate sources and uses only `/flash/Pictures` and `/sdcard/Pictures`. It creates
each collection when its mounted medium is writable and lists up to 64 photos whose
JPG/JPEG/PNG/BMP metadata can actually be decoded; it is not a second Files browser.
It uses the LVGL POSIX `P:` drive and validates file size, decoded dimensions, root
containment, and decoder metadata before preview. Previous/next, close, and
Set wallpaper are available in the single-image viewer.

Personalization offers Black, Dark, Accent, and Gallery photo backgrounds. Selecting
Set wallpaper stores the canonical `/flash/Pictures/...` or `/sdcard/Pictures/...`
path and immediately
applies the image behind routed pages. A missing removable SD image falls back to the
configured solid background without discarding the saved path. Photos copied to the
fixed 2 MiB Flash volume are device-local and survive reboot; formatting that volume
removes both those photos and the durable configuration files.

A curated deployment pack provides three native 480x800 JPEG wallpapers in
`tools/freather/wallpapers`. Copy them to the Flash MSC volume's
`/flash/Pictures` directory; Gallery will discover them and can set any one as
the persistent wallpaper. Source and license links are recorded in the
[wallpaper pack README](../../../../tools/freather/wallpapers/README.md).

Start Tiles use LVGL's mutually exclusive input events: `SHORT_CLICKED` opens an
application and `LONG_PRESSED` enters edit mode and arms movement from the Tile body.
The same held gesture can immediately drag the Tile; no corner is reserved as a move
control. A plain `PRESSED` event, layout event, or the `PRESS_LOST`/`CLICKED` sequence
that may follow a long press cannot enter an application or initialize editing. Edit
mode disables desktop/tileview scrolling until editing ends. The selected Tile is
moved to the container's last drawn child and paired with an equal-size placeholder.
The desktop itself uses explicit responsive grid cells rather than Flex compaction,
so a size change cannot move an unrelated sibling.

Body movement uses a two-stage snap state. While the dragged center is outside a
pit's confirmation radius, the placeholder is hidden and every sibling stays at its
gesture-start cell. Once the center enters that radius, the placeholder appears on
the nearest aligned row-and-column pit; releasing forces confirmation of the nearest
pit. Occupied pits are valid targets. Only after confirmation are the Tiles covered
by the reservation moved to their nearest free cells with a 180 ms ease-out snap.
This makes every middle, edge, empty, or occupied aligned pit selectable without
reflowing the desktop during uncommitted pointer motion.

All four corners contain a centered, two-stroke Chevron whose legs meet at exactly
90 degrees and point toward that corner. They are resize controls only: upper-left
moves the top and left edges,
upper-right moves the top and right edges, lower-left moves the bottom and left edges,
and lower-right moves the bottom and right edges. The two opposite edges remain fixed
for the whole gesture. Continuous pixel coverage is tracked separately from final
rounded span. The complete visible Tile body follows the pointer at pixel resolution;
on release a 180 ms ease-out animation settles its position and size onto the rounded
legal grid rectangle. As soon as the growing rectangle enters an occupied grid cell,
only the
covered Tile snaps to the nearest free cell, while all non-conflicting Tiles keep their
gesture-start cells. Shrinking back before release restores cells whose conflict
disappeared. Each gesture computes the spans that still fit between the fixed opposite
edges and the logical desktop boundary; reaching that limit stops growth rather than
forcing a new row or column. Holding the Tile body or a lower resize handle in the
top/bottom edge zone scrolls that logical desktop by bounded steps. The floating Tile
remains under the pointer while the snap solver evaluates newly exposed rows, so the
bottom of the screen is a scroll trigger, not an artificial placement or resize limit.

Move and resize release both settle the selected Tile and placeholder on the committed
explicit cell, keep the Tile as the foreground child, and resume its body-scale
animation. Geometry remains responsive column/row spans rather than absolute
480 x 800 coordinates. Tap the selected Tile again, tap empty desktop space, switch
to All Apps, or use Home/Back to leave edit mode.

The pointer hot path is transaction-gated. Repeated `PRESSING` samples inside the
same confirmed pit do not rerun the reservation solver, restart sibling animations,
or force a full LVGL layout. Resize samples update the selected Tile continuously,
but sibling collision resolution runs only when the pixel rectangle crosses into a
different set of grid cells. A snap animation already targeting the same cell is
left running. This keeps move/resize responsive without changing the confirmation,
collision, or release semantics above.

An edited Tile is promoted to an LVGL `FLOATING` child so it stays above every
sibling. LVGL still includes the parent's scroll offset in `lv_obj_get_x/y()` for
such a child, while `lv_obj_set_pos()` expects the unscrolled local style position.
The move/resize path therefore records and updates `style_x/style_y`, and converts
grid coordinates explicitly when entering the floating layer. This keeps the Tile
under the pointer even after the Start page has been scrolled. A single viewport repair
is scheduled when the interaction or its final snap animation completes. If the old/new
visual area crosses the content/navigation seam, only a narrow screen-root stripe is
redrawn in Shell child order (content first, navigation last); no full-page redraw is
performed for every pointer sample.

The edit chrome is deliberately self-contained. A fixed transparent outer Tile owns
input and clipping, while an inner body contains the accent background, application
icon, labels, pattern, and live content. The complete inner body scales from 248/256
back to its original size around its center, so the Tile visibly breathes without ever
leaving its own rectangle. Four transparent 35 px touch targets plus an 8 px extended
hit margin provide an effective clipped corner target of about 51 x 51 px. They are
siblings of the body, remain fixed during that animation, and draw only their compact
Chevron; the central area remains owned by body long-press movement. The
outer Tile does not enable `OVERFLOW_VISIBLE` or report an extended draw area, so
editing cannot paint into the desktop scrollbar or content/navigation seam. Edit mode
still exposes a bounded logical workspace; leaving edit collapses its physical extent
to the lowest real Tile so normal use does not expose an artificial blank desktop.

The top-level content object is a hard clip viewport between the status and navigation
bars. Local Tile overflow is therefore permitted inside the desktop but can never draw
through the navigation seam. The LVGL performance monitor remains compiled for metric
collection, while its default bottom-left system-layer label is hidden because it
overlaps the product navigation bar. Equivalent FPS, refresh, heap, object, and route
metrics remain available through `feather_ui_status`.

The board touch path is also frame-based. The compatible ST7123/ST7102 controller
sets the `With Coord` bit when a coordinate frame is available, and reading that
frame clears its interrupt. The driver therefore does not sample the interrupt
after reading and mistake a stationary finger for a release. A contact requires
both a valid slot and non-zero touch intensity, is held between coordinate frames,
and is released only after three consecutive invalid frames. Position changes of
three pixels or less are treated as stationary jitter. LVGL is configured with a
500 ms long-press time and an 18 px scroll limit. `feather_ui_status` exposes the
Tile edit state and touch frame/hold/press/release counters for board diagnosis.

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
