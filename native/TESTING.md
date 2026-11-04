# Background-first verification

Routine development must not steal focus, drive the system keyboard, open macOS
file dialogs, or restart the editor the user is working in. Select the smallest
test tier that covers the change; broaden only for a failure or an uncovered risk.

| Change | Default verification |
|---|---|
| Native parser, compression, protocol | Relevant native C/protocol tests; independent decoding or compiler oracle where applicable |
| Preview/controller/session behavior | Standalone Qt suites with offscreen widgets and headless engine |
| Editor integration or long editing sessions | Full-editor offscreen smoke/soak with a disposable profile and generated projects |
| Packaging | Signature/dependency checks, offscreen version probe, helper capabilities, Qt tests against the packaged helper |
| Physical macOS behavior | Visible check only when explicitly requested by the user |

The Qt tests exercise actions, widgets, scrolling, links, rendered images and
source routing directly. They do not need a system mouse or file picker.
Full-editor tests call the application's document/edit operations and use their
own configuration and fixtures. For a paper-specific regression, use the existing
opt-in copied-paper tests; they preserve the original source. Do not open the
user's paper interactively as a routine smoke test.

## Packaged app check

Run against a staged app while the installed editor remains open:

```sh
python3 native/scripts/verify_local_app.py \
  --app ".native-sources/app-updates/feature/TeXstudio Espresso.app" \
  --client-build build-texpresso --output /tmp/texstudio-background-verification
```

This checks signatures/dependencies, starts only the real executable's offscreen
version probe with a disposable profile, verifies the helper's capabilities
against its manifest, and runs the rendering/diagnostic/scrolling/link suites
against that helper. It does not launch the Finder wrapper, touch the normal
profile, quit an existing app, install anything, open a native dialog, or use
computer control. Logs and `result.json` are retained. `--tests` selects a
different relevant set; no matching tests is an error.

For editor integration:

```sh
python3 native/scripts/ui_soak.py \
  --app ".native-sources/app-updates/feature/TeXstudio Espresso.app/Contents/MacOS/texstudio" \
  --output /tmp/texstudio-background-editor-smoke --seconds 10 --pages 30
```

Its default platform is offscreen. See [the soak description](../docs/texpresso-ui-soak.md)
for longer runs and independent PDF/SyncTeX checks. Use generated public fixtures
for routine checks and PR screenshots. Offscreen preview captures are suitable
for layout/pixel assertions and can be reviewed as files without activating the
editor.

## Dictionary compression

The full editor's dictionary regression uses generated dictionaries and a
disposable cache to cover language discovery, spelling/suggestions, ignored-word
persistence, plain user dictionaries, thesaurus lookup, damaged archives,
concurrent extraction, cache repair and changed archive contents. It also loads
the packaged English and German dictionaries when present. Run it twice in
separate processes to check cache reuse across launches:

```sh
python3 native/scripts/test_dictionaries.py \
  --app ".native-sources/app-updates/feature/TeXstudio Espresso.app/Contents/MacOS/texstudio" \
  --output /tmp/texstudio-dictionary-verification
```

Use a Debug build with internal tests enabled. The runner invokes only the real
executable with offscreen widgets, disposable settings and cache; logs and
`result.json` remain in the output directory. Packaging round trips and archive
determinism are covered by `python3 -m unittest discover -s native/tests`.

## Installation and visible checks

Build and verify first. Leave the new app staged and report that it is ready;
routine testing does not authorize replacing or restarting the user's running
app. Install/restart only on an explicit request. Keep one installed test app;
staging directories are build inputs, not additional interactive installations.
Keep only the current staged candidate while it awaits installation. After a
verified replacement, remove its rollback copy and superseded staging bundles,
engine archives and completed experiment directories. Preserve inputs referenced
by active builds and evidence explicitly needed for review; follow
[the cleanup instructions](../AGENTS.md#generated-artifact-cleanup).

Offscreen tests cannot establish physical trackpad delivery, native cursor/focus
behavior, Dock icon caching, or actual compositor presentation. Report those
limits, and arrange a brief visible check when the user requests one. Never
escalate a routine test into foreground computer control merely for reassurance.

### Windows / WSL and package providers

The Windows workflow runs `texpresso-wsl-tests.exe` natively with
`QT_QPA_PLATFORM=offscreen` against a real WSL2 engine. It supplies
`TEXPRESSO_TEST_ENGINE`, `TEXPRESSO_TEST_COMMAND_RUNNER` and
`TEXPRESSO_WSL_DISTRIBUTION`; the command runner exercises the same Windows
compiler/Biber bridge that standard build recipes use. A second preview fixture
lives under the distribution's Linux `/tmp` and is accessed through WSL UNC.
Tests only create disposable projects and do not touch the user's editor/profile.

The same suite runs on macOS/Linux using `TEXPRESSO_TEST_ENGINE`. Its pure tests
cover provider discovery, MiKTeX capability gating and explicit package-download
policy without installing anything. Real MiKTeX validation additionally requires
a MiKTeX environment and an engine advertising `packageProviders: ["miktex"]`;
TeX Live tests alone do not establish MiKTeX compatibility. Set
`TEXPRESSO_PACKAGE_PROVIDER=miktex` when running against that environment.

TikZ figure/editor integration has a standalone `texpresso-tikz` Qt suite and
an ordinary-build pixel oracle; see [its background checks](../docs/texpresso-tikz-editor.md).

The MiKTeX CI scenario explicitly permits package downloads for its first Qt
Hyperref/bibliography run, then repeats editing/navigation/restart with downloads
disabled. Cold setup has a five-minute fixture deadline; warm starts keep the
90-second deadline. Known-valid fixtures fail immediately on compiler errors or
terminal pagination, with full compiler output, rather than timing out silently.
The first Ubuntu run measured about 81 seconds to the initial Hyperref frame
with setup/downloads permitted, and 11 seconds on a new engine using the populated
package cache. These measure time to the first frame; editing latency is a
separate measurement. The provider's per-lookup process/time limits still apply.
