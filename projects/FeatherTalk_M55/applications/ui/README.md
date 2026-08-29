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
- long-press Start Tile editing with a subtle breathing animation, a four-way move
  handle, three diagonal resize handles, floating pointer tracking, live sibling
  reflow, and release-time grid snapping;
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

The visible categories are limited to settings that users can actually change on
the current product: Display & brightness, Wi-Fi, Bluetooth, and Personalization.
Notification queue management remains in the notification shade, storage status
remains in Files, and runtime diagnostics/About remain separate applications.
Cellular/SIM/phone, hotspot, VPN, NFC, account, language/RTC, application uninstall,
and every status-only placeholder from the Windows Phone visual reference are
intentionally absent. Display brightness directly uses the M55 `pwm18` adapter.
Wi-Fi and Bluetooth read the ABI 4 M33 capability/enabled/connected fields, plus
Wi-Fi signal percentage, and visibly disable their action when the corresponding
M33 service is not enabled. Storage, RTC, locale, rotation, and other incomplete
board services state the missing driver or persistence boundary instead of exposing
an in-memory switch that would pretend to change hardware.

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
all navigation buttons, six Start tiles,
six All Apps entries, Settings search/keyboard and every hardware category route,
real Settings brightness write/readback, all four category routes, every personalization choice, all Media controls,
Messages/Files actions, transient-object lifecycle cleanup, System-to-Search
address-reuse regression, Tile model/edit-handle/reorder/resize/property/live-content
behavior, route depth/overflow/release, and an object-leak check.
It restores all default preferences and the Start screen when complete.

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
Messages creates inbox text, and System receives external M33 status. A future Gallery
app can create an `lv_image` in the same host and switch image frames without adding
Gallery-specific logic to the desktop.

Start Tiles use LVGL's mutually exclusive input events: `SHORT_CLICKED` opens an
application and `LONG_PRESSED` enters edit mode. A plain `PRESSED` event, layout
event, or the `PRESS_LOST`/`CLICKED` sequence that may follow a long press cannot
enter an application or initialize editing. Edit mode disables desktop/tileview
scrolling until editing ends. Each compact circular handle has its center exactly on
one Tile corner while an enlarged invisible hit area preserves touch usability. The
selected Tile is moved to the container's last drawn child and paired with an
equal-size placeholder. The desktop itself uses explicit responsive grid cells rather
than Flex compaction, so a size change cannot move an unrelated sibling. This also
guarantees that the complete Tile and all four handles stay above every sibling
throughout edit mode.

The upper-left handle is a single centered, line-drawn four-way move symbol. Movement
uses a two-stage snap state. While the dragged center is outside a pit's confirmation
radius, the placeholder is hidden and every sibling stays at its gesture-start cell.
Once the center enters that radius, the placeholder appears on the nearest aligned
row-and-column pit; releasing forces confirmation of the nearest pit. Occupied pits
are valid targets. Only after confirmation are the Tiles covered by the reservation
moved to their nearest free cells with a 180 ms ease-out snap. This makes every middle,
edge, empty, or occupied aligned pit selectable without reflowing the desktop during
uncommitted pointer motion.

The other three corners use centered diagonal double-arrow symbols and resize
continuously. The upper-right handle moves only the top and right edges, the lower-left
moves only the left and bottom edges, and the lower-right moves only the right and
bottom edges; the two opposite edges remain fixed for the whole gesture. Continuous
pixel coverage is tracked separately from final rounded span: as soon as the growing
rectangle enters an occupied grid cell, only the covered Tile snaps to the nearest
free cell, while all non-conflicting Tiles keep their gesture-start cells. Shrinking
back before release restores cells whose conflict disappeared. Each gesture computes
the spans that still fit between the fixed opposite edges and the visible desktop
boundary; reaching that limit stops growth rather than forcing a new row or column.

Move and resize release both settle the selected Tile and placeholder on the committed
explicit cell, keep the Tile as the foreground child, and resume its breathing
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
under the pointer even after the Start page has been scrolled. A single viewport
repair is scheduled when the interaction or its final snap animation completes; no
full-page redraw is performed for every pointer sample.

The handles intentionally extend beyond both the Tile and desktop-container bounds.
Because this port uses LVGL partial rendering, `OVERFLOW_VISIBLE` alone is not enough:
the Tile and its container also report a responsive 32 px extended draw area through
`LV_EVENT_REFR_EXT_DRAW_SIZE`. Handle visibility changes invalidate the full extended
area before and after the flag transition. This keeps parent clipping and dirty-area
refresh aligned, so corner circles are complete and hiding or moving them cannot leave
old pixels in the permanent display framebuffer.

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
