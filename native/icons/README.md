# macOS app icon

`AppIcon.icon` is an editable Apple Icon Composer project using the existing
root `texstudio.icns` artwork. Its largest image is center-cropped from 512×512
to 480×480, then scaled to Icon Composer's 1024×1024 canvas. Colors, lettering,
projector and equations are preserved. No artwork is redrawn. Glass translucency,
specular effects and group shadows are disabled.

`AppIconCoffee.icon` is a comparison alternative. A separate foreground SVG layer
adds the cup from `native/texpresso/doc/texpresso_logo_v3.svg` in the top right.
The logo's background rectangle and TeX lettering are removed; the original cup
paths and steam are retained. Its composition is stored in `Assets/Coffee.svg`.
It overlays the equations in the original image. The local test app generator
uses this alternative; the standard CMake bundle keeps the cropped original.

Open either `.icon` directory in Icon Composer to adjust its composition. Compare
`preview.png` and `alternatives/coffee/preview.png` for Apple's macOS rendering.

## Regenerate

Requires macOS with Xcode 26 and its bundled Icon Composer (initial assets were
generated with Xcode 26.2 and Icon Composer 1.2):

```sh
python3 native/scripts/build_app_icon.py
# Or regenerate just one:
python3 native/scripts/build_app_icon.py --variant original
python3 native/scripts/build_app_icon.py --variant coffee
```

The script uses `iconutil` and `sips` to reproduce the crop, `actool` to compile
the `.icon` projects, and Icon Composer's `ictool` to export previews. It refreshes
the base bitmap from the root `.icns`; edit the project JSON or cup SVG for layout
changes, not that generated bitmap. A standalone installation can be selected
with `--icon-composer '/Applications/Icon Composer.app'`.

The compiled `Assets.car` and `AppIcon.icns` are committed so normal macOS builds
(including Intel builders) do not need Icon Composer. CMake places them in the
app's Resources directory. `Info.plist` names `AppIcon`; macOS selects the asset
catalog or compatibility `.icns` as appropriate. The original `texstudio.icns`
is still bundled for document icons. Linux and Windows icons are unchanged.

The coffee variant's compiled files live under `alternatives/coffee/` and are not
copied into the application by CMake. The local test app generator installs them
automatically. To try it in a
separate app bundle, copy that directory's `Assets.car` and `AppIconCoffee.icns`
into Resources, change both `CFBundleIconName` and `CFBundleIconFile` to
`AppIconCoffee`, then re-sign the bundle.
