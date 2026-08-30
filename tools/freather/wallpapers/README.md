# FeatherTalk wallpapers

These wallpapers are deployment assets for the device-local `/flash/Pictures`
collection. They are not linked into the M55 firmware, so the firmware image and
the fixed 2 MiB user volume do not contain duplicate copies.

All output files are baseline RGB JPEG images, centre-cropped to the native
480x800 portrait panel and limited to 128 KiB each. `prepare.py` removes source
metadata, applies a dark treatment for white Metro-style UI text, and verifies
the dimensions and file-size ceiling.

| Output | Size | Source | License | Processing |
| --- | ---: | --- | --- | --- |
| `metro-facets.jpg` | 5,217 bytes | [Geometric Abstract Background](https://commons.wikimedia.org/wiki/File:Geometric_Abstract_Background.jpg), Vidsplay | CC0 1.0 | centre crop, monochrome blue colour grade, darkening |
| `aurora-lines.jpg` | 20,502 bytes | [Gradient Abstract Background](https://commons.wikimedia.org/wiki/File:Gradient_Abstract_Background.jpg), Vidsplay | CC0 1.0 | centre crop, reduced saturation, contrast and darkening |
| `night-city.jpg` | 36,344 bytes | [City street at night](https://commons.wikimedia.org/wiki/File:City_street_at_night.jpg), Chris Spielmann / US National Cancer Institute | Public domain | centre crop, reduced saturation, contrast and darkening |

The complete set is 62,063 bytes (about 60.6 KiB), leaving ample room in the
fixed 2 MiB Flash user volume for preferences and user photos.

To reproduce the files, download the three originals with the names expected by
the script, install Pillow, and run:

```powershell
python prepare.py <original-image-directory>
```

Copy the resulting JPEG files to the Flash MSC volume's `Pictures` directory.
Gallery will then discover them without exposing the rest of the volume.
