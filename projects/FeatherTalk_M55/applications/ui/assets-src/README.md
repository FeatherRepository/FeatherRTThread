# FeatherTalk SVG icon sources

These files are the editable source of truth for the product icon system. They
use a deliberately small SVG subset so `tools/freather/ui-svg-icon-convert.py`
can generate deterministic LVGL A8 assets without a host graphics package.

Rules:

- use a `0 0 24 24` viewBox;
- use `currentColor` and monochrome geometry;
- use only `g`, `line`, `polyline`, `polygon`, `rect`, `circle`, and `ellipse`;
- do not use paths, transforms, CSS classes, filters, masks, text, scripts,
  external references, or embedded raster images;
- use neutral product symbols rather than third-party trademarks; and
- record the visual reference and licensing decision in `../assets/manifest.json`.

Regenerate firmware assets from the SDK root:

```powershell
.\tools\freather\build-ui-icons.cmd
```

Generated files under `../assets/generated` must not be edited manually.
