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
Then extend dependency watching/diagnostics, packaging checks, and finally
scrolling/SVG using the extension as the behavioral reference.

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
Poppler supplies its usual detailed region; the live adapter supplies an
immediate crop of its PNG frame while requesting detailed region pixels from
the engine. Region results update the same magnifier without replacing the
page image. Both viewers use
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

The renderer uses bounded PNG frames and incremental PNG patches, with detailed
regions for the magnifier and one decode worker per session. It discards
outdated frames after source/page changes. Failed TeX passes
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
The initial runtime update used the extension's `5d906d6` engine snapshot,
bringing checkpoint reuse, worker recovery and dashed-stroke ownership fixes.
The current `664486e` snapshot adds the no-op rerun correction described below.

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

## Compiler passes and normal builds

Automatic TeX reruns are enabled for new live sessions. The panel also offers
an Automatic Reruns toggle and Rerun Once. These use the shared engine's
auxiliary convergence mechanism: finishing passes feed generated auxiliary
contents back into compilation until their bytes stabilize. Automatic passes
wait for 500 ms of idle time and are limited to five finishing passes per edit.
Inactive, paused and stopped projects do not request manual reruns.

Bibliography and index programs remain part of TeXstudio's normal build
recipes. BuildManager reports the beginning and result of the entire recipe,
including automatic bibliography and TeX reruns. The live adapter captures the
build's root, pauses that project's live compilation, and suppresses its Qt
dependency callbacks during the build. After success it restarts the engine
to clear cached missing files and consume newly created `.aux`/`.bbl` inputs.
The session, open-buffer overlays, page, zoom, scroll and explicit pause/stop
state survive; switching tabs cannot refresh a different project. A failed or
cancelled recipe resumes the existing engine without forcing a restart. The
engine's own filesystem polling can still observe files left by a failed build;
pause does not provide filesystem snapshot isolation.

Live startup searches auxiliary directories inferred from expanded compiler,
latexmk and bibliography commands, together with TeXstudio's configured log
search directories. Standalone bibliography commands inherit a project's last
successful directories when none are explicit. A new compiler recipe replaces
that selection. Paths hidden in an opaque wrapper or `.latexmkrc` must be
listed in TeXstudio's additional log search paths. The shared engine searches
beside the source before its additional directories: remove obsolete duplicate
auxiliary files beside the source when moving a project to a separate output
directory.

The panel's **Build & Refresh** runs TeXstudio's normal Compile recipe, including
its save policy, compiler/bibliography magic comments and automatic bibliography
passes. **Rerun Once** only requests another TeX pass; it cannot create a missing
`.bbl` by invoking Biber or BibTeX. Explicit bibliography requests now take
priority over generic reference rerun warnings, so ordinary TeX reruns cannot
consume the entire build budget before the bibliography tool runs. The build's
captured root also supplies its log and bibliography override after tab switches.
An invalid regular expression in the inherited command-permission check also
prevented plain configured IDs such as `txs:///biber` from being recognized;
these single IDs are recognized again, while chains, arguments and modifiers
continue through the existing permission check.

Recoverable TeX errors and missing bibliography warnings appear directly in the
panel, with source context in the tooltip. Output dialogs follow their original
session and update live. Compiler log publication is coalesced per event-loop
turn, while a protocol flush publishes immediately; checkpoint truncation clears
obsolete diagnostics. A completed compile with zero pages reports a failure
instead of leaving the panel on “Updating preview…”. Source repair can resume
the same process.

### Settled-document reruns

Engine `664486e` fixes a hang after auxiliary convergence. Previously,
`rerun-once` invalidated the retained display list before determining whether a
finishing pass was necessary. When the auxiliary contents were already stable,
no finishing pass or redraw followed, leaving a same-page image request pending
forever. No-op reruns now retain the display list and consume their pending flag;
actual finishing passes still invalidate and redraw normally. Regression tests
cover automatic/manual modes, repeated settled reruns, a following edit with
automatic reruns disabled, zero-page errors and recovery. A separate read-only
check on a 35-page mathematical document completed three settled reruns in the
same process without a restart.

## Incremental raster updates and magnifier detail

Clients with `pngPatches` support receive a full PNG base followed by cropped
patches when a sufficiently small part of the page changes. Patches must name
the retained base and match its page, dimensions and bounds; a mismatch or
failed patch decode requests a full frame. A metadata-only patch reuses the
same image without decoding or copying pixels. Source changes discard page
caches while retaining the displayed raster as a possible patch base; the
engine still validates that base before choosing a patch.

The magnifier requests clipped native regions at its own resolution, with one
in-flight image export and only the latest pending lens position. It displays
an enlarged page crop immediately, then replaces that crop with detailed
pixels. Region replies never replace the page image or patch base, and stale
source, revision, request, page and lens tokens are rejected. Unsupported
engines retain full PNG frames and the immediate crop fallback.

The first compiler/rendering pass passed all three live-client suites (33 Qt test
cases), including real BibTeX and Biber builds in both root and separate output
directories, initially missing and changed bibliographies, executed TOC entries,
forward-reference convergence and project lifecycle cases. The rendering suite
also passes at 2× display scale. The complete application's automatic test run
passes, including all 64 BuildManager cases and the new recipe lifecycle tests.
Linux CI installs Biber and the bibliography packages so those tests run there
as well; Linux runtime verification still requires a CI run.

The rerun/diagnostics follow-up passes all five client suites (45 Qt cases) and
the full application's automatic tests. Its opt-in real Biber workflow starts
without a `.bcf` or `.bbl`, modifies a named editor buffer, clicks Build &
Refresh, and switches to a different project with a conflicting bibliography
override. It verifies the original root's Biber command, saved buffer contents,
generated bibliography and refreshed live session. Run that application check
with `TEXPRESSO_TEST_BIBLIOGRAPHY=1` and TeX Live on `PATH`; it uses temporary
projects and an isolated settings directory.

## Remaining work

- Continuous page scrolling, following the extension's implementation.
- Measured Qt SVG/adaptive rendering.
- Full dependency reconciliation (missing/deleted files, external include
  directories), source renames, bounded idle-session eviction and memory budgets.
- Clickable log diagnostics and auxiliary-directory precedence in the engine.
- Automated editor interaction tests and Linux runtime verification in CI.
- macOS/Linux release packaging with complete Qt deployment, release signing,
  notarization and clean-machine checks. Existing macOS engine signing tools are
  available, but the development application is not a notarized release.
