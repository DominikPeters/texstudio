# Bundled TeXpresso

The engine is the same `DominikPeters/texpresso` `vscode` fork pinned by
texpresso-vscode. `native/texpresso` records an exact commit; update both clients
together after testing shared engine changes. The older web-wrapper branch is
unrelated. The application always launches its bundled engine with `-texlive`.
Users install TeX Live, but do not configure an engine executable.

The macOS builder, dependency lock, profiles, notarization tools and tests were
adapted from `DominikPeters/texpresso-vscode/native` (engine pin 0868436). Keep
behavior aligned with that implementation; eventual shared engine releases can
replace these source builds. All build inputs are supplied explicitly; building
does not download dependencies.

Initialize the engine with `git submodule update --init native/texpresso`.
On macOS, supply MuPDF and ICU at the versions in `dependencies.lock.json`:

```
python3 native/scripts/build.py --mupdf-source /path/to/mupdf --icu-prefix /path/to/icu
```

On Linux install the engine's build dependencies (see the live-preview CI), then:

```
python3 native/scripts/build_linux.py
```

Bundles contain `bin/texpresso`, `bin/texpresso-xetex`, relocated runtime
libraries, dependency notices and `build-manifest.json`. macOS binaries are
ad-hoc signed by the builder; `notarize.py` supports Developer ID signing and
notarization of a separate bundle. Application release signing must sign the
nested helpers/libraries before the outer app. Linux bundles require the build
host's glibc baseline and system graphics/font configuration; glibc and the
ELF interpreter are deliberately supplied by the target system.

CMake requires a matching bundle by default on macOS/Linux. It discovers
`native/.bundles/<darwin|linux>-<architecture>`, or a packager can specify
`-DTEXPRESSO_BUNDLE_DIR=/path/to/bundle`. It validates the pinned revision,
platform, capabilities and file hashes before staging the entire runtime into
the app and installing it. This is a build input, never an end-user preference.
`-DTEXSTUDIO_ENABLE_TEXPRESSO=OFF` builds the original editor without live preview.

Validate native tooling with `python3 -m unittest discover -s native/tests`.
