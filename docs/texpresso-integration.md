# TeXpresso integration

Agreed design, 22 October 2026:

- Native Qt preview on macOS and Linux. Bundle the same pinned TeXpresso fork as
  the VS Code extension and use the user's installed TeX Live.
- Automatically start a session per project root. Tabs sharing a root share an
  engine. Switching projects preserves each engine and its page/zoom/scroll;
  inactive sessions pause compilation. Explicit Pause/Stop survive tab changes.
- Keep normal TeXstudio compilation and its PDF viewer for final PDF output.
- Start with one page at a time; port scrolling as the extension develops it.
- Reuse the existing editor and headless-preview protocols. Keep compilation,
  MuPDF rendering and SyncTeX in the engine process.
- Share rendering-independent viewer controls and tools with the saved-PDF
  viewer. Keep Poppler and the live engine behind separate rendering adapters.

Implementation boundaries: process/framing, project session/buffer overlays,
preview scheduling, Qt widget, and the TeXstudio document adapter. Guard every
asynchronous image with process generation, source sequence, revision and request
ID. Keep stale pixels visible while updating, but disable their interactions.
Bound image allocation/cache memory and pending requests. Terminate the complete
engine process group on shutdown.

The editor adapter uses QDocument's UTF-16 `changedText` ranges and reconciles
whole-buffer reloads and edits suppressed by the collaboration signal. Use
TeXstudio's existing root resolution and engine-reported input paths. Keep
unsaved overlays across compiler backtracking and send them to every subscribed
project session.

Initial acceptance: install the bundled application, open two multi-file
projects, edit unsaved included text (including Unicode and undo), switch tabs,
and recover each project's correct preview without restarts or path setup.
Then extend dependency watching/diagnostics, SyncTeX, regions/patches, packaging
checks, and finally scrolling/SVG using the extension as the behavioral reference.

## Shared viewer components

`src/pdfviewer/ViewerControls.*` owns the common action definitions, toolbar
ordering, page entry/count widgets, zoom menu/slider and zoom-step calculations.
Both `PDFDocument` and `TexpressoWidget` use these components. Live actions use
the PDF action IDs and widget-scoped shortcuts, so customized PDF shortcuts also
apply without intercepting editor commands. The live panel exposes magnifier,
hand scrolling, page navigation, actual size, fit width and fit window. PDF-only
capabilities (text selection, annotations and fit-to-text bounds) remain absent
until the engine provides their data.

`ViewerTools.*` contains the existing PDF magnifier and laser-pointer painting,
without Poppler dependencies. The magnifier accepts a renderer callback:
Poppler supplies its usual detailed region; the live adapter currently supplies
an enlarged crop of its PNG frame, bounded to the lens neighborhood on large
and high-DPI displays. Engine region rendering can replace that
callback without adding another magnifier implementation. Both viewers use
the same magnifier appearance, size, color conversion and zoom preferences.

Both inverse-sync paths call `Texstudio::syncFromPreview`, sharing file opening,
editor activation, navigation history, approximate word matching and temporary
source highlighting. Saved PDF positions pass through the saved line snapshot;
live positions address current buffers directly. The editor's existing forward
sync entry point also reaches the active live session. The live toolbar shares
the PDF viewer's “Scrolling follows cursor” preference, and explicit sync works
regardless of that preference. Engine SyncTeX remains authoritative for live
page/source mappings; reading the final build's `.synctex` file would map the
wrong document revision.

## First working implementation

`src/texpresso/` contains the independently testable Qt client: binary protocol
reader, subprocess/process-group lifecycle, sessions, renderer scheduling and a
one-page Qt widget. `src/texstudio_texpresso.cpp` connects it to TeXstudio.

Automatic sessions follow the existing root resolution and use canonical paths
for editor identity while retaining the engine's logical aliases. Open included
buffers override disk, including extensionless `\input{chapter}` lookups.
Source updates use ordered UTF-16 ranges and barriers; whole-document reloads
and edits suppressed by collaboration signals reconcile afterward. Shared
buffers update every subscribed session. Explicit Pause/Stop survives tab
switching. Page, zoom, scroll and the last image are stored per session.

The first renderer uses bounded full-page PNG frames and one decode worker per
session, discarding outdated frames after source/page changes. Failed TeX passes
keep old pixels visible with their source interactions disabled, then recover
when the source is corrected. Source synchronization uses current buffers and
converts between zero-based editor lines and one-based SyncTeX lines.

TeX Live is selected explicitly. macOS GUI launches discover MacTeX's
`/Library/TeX/texbin`; conventional `/usr/local/texlive/<year>/bin/<platform>`
installations are also discovered. No engine executable path is exposed to users.
The application includes the pinned native runtime in its build and install
outputs; validation before bundling checks source revision, platform and file hashes.

Validation on macOS includes the full application build/install, relocated
engine validation with a clean environment, 18 native-tooling tests, and sixteen Qt
checks, including shared zoom controls, page navigation, magnifier interaction
and stale-frame source-click rejection. Pixel checks verify magnifier contents
and refresh on a large viewport. The real-engine cases cover explicit/extensionless includes, unsaved
Unicode (including UTF-16 surrogate pairs), undo, inactive-project edits, both
SyncTeX directions, page preservation, compilation-error recovery and process
group cleanup. The actual editor was also exercised with an unsaved included
file edit and undo. The extension's three headless-preview tests passed against
the same bundled engine without changing the extension checkout.

The [first performance pass](texpresso-performance.md) adds a bounded decoded-page
cache, memoized color conversion and merged viewer refreshes. It follows the
extension's deferred native-page preparation for inverse SyncTeX on cached pages.
Cache tests cover invalidations, source/width changes, entry and byte eviction,
abandoned inverse clicks and a real two-page source-navigation round trip.

Run the client tests separately:

```
cmake -S src/texpresso -B build-client
cmake --build build-client --parallel
TEXPRESSO_TEST_ENGINE="$PWD/native/.bundles/darwin-arm64/bin/texpresso" \
  ctest --test-dir build-client --output-on-failure
```

Use the corresponding `linux-<architecture>` bundle on Linux. The new TeXpresso
CI workflow builds a Linux runtime, runs real-engine Qt tests and installs the
application with both executables. It needs its first CI run; Linux execution
was unavailable on the development host. Inherited CI/CD jobs explicitly build
upstream compatibility configurations without live preview.

## Remaining work

- Continuous page scrolling, following the extension's implementation.
- Engine region detail for the shared magnifier, PNG patches and measured Qt
  SVG/adaptive rendering.
- Full dependency reconciliation (missing/deleted files, external include
  directories), source renames, bounded idle-session eviction and memory budgets.
- Integrated log diagnostics, rerun controls and coordinated final-build refresh.
- Automated editor interaction tests and Linux runtime verification in CI.
- macOS/Linux release packaging with complete Qt deployment, release signing,
  notarization and clean-machine checks. Existing macOS engine signing tools are
  available, but the development application is not a notarized release.
