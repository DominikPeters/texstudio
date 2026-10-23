# Persistent macOS test app

Use this for local development builds. The default application is
`~/Applications/TeXstudio TeXpresso Test.app`. Its launcher uses separate,
persistent settings in `~/Library/Application Support/TeXstudio TeXpresso Test`,
allows it to run beside the normal editor, and disables Debug-build self-tests
on ordinary launch. TeX Live remains user-installed.

Run from the repository root with an existing configured CMake build:

```sh
python3 native/scripts/local_test_app.py --build-dir build \
  --macdeployqt "$(brew --prefix qt)/bin/macdeployqt" \
  --library-path /tmp/texstudio-poppler/lib \
  --library-path "$(brew --prefix)/lib" \
  --poppler-data "$(brew --prefix)/share/poppler"
open "$HOME/Applications/TeXstudio TeXpresso Test.app"
```

This is the current development machine's command. Change
`/tmp/texstudio-poppler/lib` if your Poppler prefix is elsewhere. It is a
**build input**: the packaging step copies
the libraries and encoding data into the app, rewrites library references,
removes build-machine search paths, and rejects non-system dependencies outside
the app. The installed app does not depend on that temporary directory.

The command rebuilds `texpresso_bundle`, then reuses `deploy_macos.py` to package
Qt frameworks/plugins, PDF dependencies/data and the pinned engine. It applies
an ad-hoc local signature, verifies the complete signature and dependencies,
and adds the isolated launcher. It does not use Apple credentials or perform
notarization. See [signed macOS builds](MACOS_RELEASE.md) for distributed builds.

It refuses to overwrite an existing output. Quit and rename the previous app
before rebuilding, or pass `--output "$HOME/Applications/Another Test.app"` to
keep both versions. Both names use the same test settings directory. To start
with fresh settings, quit the test app and move that directory aside.

The script reuses the configured CMake cache. After switching branches or when
refreshing the About dialog's Git version, first run `cmake -S . -B build` to
refresh the configuration and version metadata.

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
  --app "$HOME/Applications/TeXstudio TeXpresso Test.app/Contents/MacOS/texstudio" \
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
