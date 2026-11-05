# Persistent macOS test app

Use this for local development builds. The default application is
`~/Applications/TeXstudio Espresso.app`. Keep this single path
for subsequent builds rather than creating additional feature-named apps.
Build the locked Poppler dependency once into the repository's ignored area:

```sh
python3 native/scripts/build_poppler.py --cache .native-sources/distribution-sources \
  --work native/.work/poppler-provenance --prefix .native-sources/dependencies/poppler \
  --qt-prefix "$(brew --prefix qt)" --jobs 3 --fetch
```

This writes a source/configuration/binary-hash receipt used by the licensing
inventory. Use new work/prefix paths when replacing an active dependency; update
the editor's CMake/PkgConfig paths before removing superseded inputs.

Its launcher uses separate,
persistent settings in `~/Library/Application Support/TeXstudio TeXpresso Test`,
allows it to run beside the normal editor, and disables Debug-build self-tests
on ordinary launch. TeX Live remains user-installed.

Run from the repository root with an existing configured CMake build:

```sh
python3 native/scripts/local_test_app.py --build-dir build-cursor-app \
  --macdeployqt "$(brew --prefix qt)/bin/macdeployqt" \
  --library-path "$PWD/.native-sources/dependencies/poppler/lib" \
  --library-path "$(brew --prefix)/lib" \
  --poppler-data "$(brew --prefix)/share/poppler"
```

This is the current development machine's command. Change
`.native-sources/dependencies/poppler/lib` if your Poppler prefix is elsewhere. It is a
**build input**: the packaging step copies
the libraries and encoding data into the app, rewrites library references,
removes build-machine search paths, and rejects non-system dependencies outside
the app. The installed app does not depend on that temporary directory.

The command rebuilds `texpresso_bundle`, then reuses `deploy_macos.py` to package
Qt frameworks/plugins, PDF dependencies/data and the pinned engine. It applies
an ad-hoc local signature, verifies the complete signature and dependencies,
and adds the isolated launcher and the larger, unlettered coffee-cup icon from
[the Icon Composer assets](icons/README.md). It does not use Apple credentials or perform
notarization. See [signed macOS builds](MACOS_RELEASE.md) for distributed builds.

For routine development, set `--output` to an ignored staging path such as
`.native-sources/app-updates/current/TeXstudio Espresso.app` and verify it with
[the background testing workflow](TESTING.md). Building and verification do not
require quitting the installed app. Installation/restart and interactive launch
are separate, explicitly requested operations; do not open a native file dialog
to perform routine verification.

It refuses to overwrite an existing output. Build and verify the staged update
first. Once installation is authorized and the app is closed, keep the previous
bundle only as a temporary rollback copy, move the verified update to the default
path, and verify its installed signature/dependencies. Remove the rollback copy
after those checks pass. Do not move a running app: its editor and engine may
still need bundle resources. Keep one installed app and at most one current
staged update awaiting installation. Remove superseded staging/retired apps,
obsolete engine bundles/ZIPs and finished experiments before ending work; preserve
active build inputs and explicitly retained comparison evidence. See
[the cleanup instructions](../AGENTS.md#generated-artifact-cleanup).
To start with fresh settings, quit the test app and move its settings directory aside.

The script reuses the configured CMake cache. After switching branches or when
refreshing the About dialog's Git version, first run `cmake -S . -B build-cursor-app` to
refresh the configuration and version metadata.

The native bundle must use the compact static MuPDF build from
`native/scripts/build.py`. macOS configuration rejects bundles carrying the
general-purpose MuPDF dylib or its OCR/HTML/JavaScript dependencies. When
switching `TEXPRESSO_BUNDLE_DIR`, the staging target replaces the entire helper
directory so libraries from an older bundle cannot remain in the app.
The current local compact bundle can be selected with:

```sh
cmake -S . -B build-cursor-app \
  -DTEXPRESSO_BUNDLE_DIR="$PWD/native/.bundles/darwin-arm64"
```

The compact arm64 runtime is approximately 31 MB, compared with 88 MB for the
previous general-purpose MuPDF runtime. Its manifest retains the engine pin,
filtered ICU and PNG compression metadata, and records the compact archive hashes.

Bundled spelling dictionaries/affix pairs and thesauri are stored as ZIPs by
default (`TEXSTUDIO_COMPRESS_DICTIONARIES`). CMake generates them under the build
directory and removes the previous raw copies from incremental macOS app builds.
Licenses, word lists and other dictionary resources stay available uncompressed.
The runtime lists languages without extracting them and materializes only selected
languages under Qt's user cache location, in `dictionaries/v1/<archive-sha256>`.
Extracted files are verified and reused across launches; a missing or damaged
cache is rebuilt. Settings retain the original language and dictionary paths.
User-supplied plain dictionaries still load directly, while ignored words and
custom thesaurus entries stay in the settings location, outside the cache.

For a new build directory, first build the engine as described in
[the native README](README.md), and configure TeXstudio with the matching
Poppler/Qt development dependencies, for example:

```sh
export PKG_CONFIG_PATH="/path/to/poppler-prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="/path/to/poppler-prefix;$(brew --prefix qt)" \
  -DTEXSTUDIO_ENABLE_TESTS=ON \
  -DTEXPRESSO_BUNDLE_DIR=/absolute/path/to/pinned-engine-bundle
```

The test app includes `Contents/Resources/texpresso-test-app.json` with its source
revision, dirty-source flag, build path and input executable hash. The adjacent
`texpresso-release.json` and `texpresso/build-manifest.json` record the source and
engine packaging metadata. These paths are provenance, not runtime dependencies.

For automated checks invoke the real executable directly, since the Finder
launcher intentionally disables self-tests:

```sh
python3 native/scripts/ui_soak.py \
  --app "$HOME/Applications/TeXstudio Espresso.app/Contents/MacOS/texstudio" \
  --output /tmp/texstudio-installed-smoke --seconds 10 --pages 30
```

Only the disposable smoke-test evidence and configuration use `/tmp` here;
the application and its ordinary interactive settings are persistent.

Validated on 24 October 2026: the default installed app passed strict signature
and bundled-dependency checks, launcher startup, and a 128-edit editor/preview
smoke with all five independent PDF/SyncTeX probes and no leftover processes.
All 25 native tooling tests passed. Local logs are
`/tmp/texstudio-persistent-test-app.log` and
`/tmp/texstudio-persistent-app-smoke/`.
