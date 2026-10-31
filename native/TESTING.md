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

## Installation and visible checks

Build and verify first. Leave the new app staged and report that it is ready;
routine testing does not authorize replacing or restarting the user's running
app. Install/restart only on an explicit request. Keep one installed test app;
staging directories are build inputs, not additional interactive installations.

Offscreen tests cannot establish physical trackpad delivery, native cursor/focus
behavior, Dock icon caching, or actual compositor presentation. Report those
limits, and arrange a brief visible check when the user requests one. Never
escalate a routine test into foreground computer control merely for reassurance.
