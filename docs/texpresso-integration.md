# TeXpresso integration

Agreed design, 22 October 2026:

- Native Qt preview on macOS and Linux. Bundle the same pinned TeXpresso fork as
  the VS Code extension and use the user's installed TeX Live.
- Automatically start a session per project root. Tabs sharing a root share an
  engine. Switching projects preserves each engine and its page/zoom/scroll;
  inactive sessions pause compilation. Explicit Pause/Stop survive tab changes.
- Keep normal TeXstudio compilation and its PDF viewer for final PDF output.
- Continuous scrolling with bounded page rendering, following the extension;
  retain a single-page mode for users who prefer it.
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
Then extend dependency watching/diagnostics, packaging checks and adaptive SVG
rendering using the extension as the behavioral reference.

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
virtualized Qt viewer. `src/texstudio_texpresso.cpp` connects it to TeXstudio.

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
The current `863e9880` snapshot incorporates the extension's `4919c95` engine
and retains our no-op rerun correction described below.

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
panel, with source context in the tooltip. Its Output button opens TeXstudio's
shared Log panel. Compiler log publication is coalesced per event-loop
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

Engine `863e9880` also preserves an already-polled SDL event when starting an
on-demand finishing pass. Dropping a queued reload left its coalescing flag set,
so later reloads were suppressed even after a repaired document produced a page.
Rapid reruns followed by source repair exposed this intermittent stall. The
engine now requeues the event before continuing the pass; the Qt regression
retains detailed request/source state and optional protocol tracing.

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

## Shared compilation output

The existing Messages and Log panels have a synchronized Build/Live/Engine trace
selector. Output and the selected source are retained per project. Live output
uses the existing TeX log parser, error/warning/bad-box filters, clickable table,
next-error navigation and editor gutter marks. Log parsing is coalesced over
150 ms; publishing background diagnostics does not focus the panel or select its
first error. Engine stderr is available separately for debugging.

Source edits immediately mark previous live diagnostics stale and disable their
navigation and gutter marks. Acknowledged compiler output for the new source
sequence restores them. Live locations address current unsaved buffers; normal
build locations use the saved line snapshot. The normal build has an independent
parsed model, so viewing live errors cannot affect its success, rerun or
bibliography decisions. Build & Refresh still runs the normal recipe and imports
the resulting auxiliary state when that complete recipe succeeds.

## Continuous scrolling and cursor preparation

The scrolling viewer reuses the shared Qt PDF controls, tools and SyncTeX entry
points. It keeps a bounded pool of canvases and a 32 MiB page-image cache. Ordinary
views retain four cached images; zooming out expands the entry limit to visible
demand plus two neighbors, capped at sixteen images and sixteen canvases. Visible pages take priority over idle neighbor rendering. A gesture
permits at most one speculative page beyond the known compilation frontier;
prefetch cannot recursively compile the entire document. Page exports preserve
the engine's foreground display list and PNG comparison base. Zoom and edits
preserve the viewport's page anchor. Single-page mode remains available.

Cursor preparation follows the extension's idle selection scheduler: wait 200 ms
after a cursor change, then lazily resolve the engine file alias and UTF-8 byte
offset from the current editor buffer. It runs only for the active, unpaused
session after foreground rendering settles. Typing, scrolling, source navigation
and magnifier work cancel preparation immediately. No editor update waits for
the idle timer. Requests share the renderer's monotonic IDs and validate source
sequence and engine revision. A bounded 16-entry history deduplicates positions
in 64-byte buckets, and a five-second watchdog releases unacknowledged work.
Preparation is best effort; a cancelled checkpoint may fall back to foreground
replay. Skipped checkpoints retry only after foreground progress. Preparation invalidates
the assumed native SyncTeX page, so inverse search restores its actual page first.

The scrolling and cursor-preparation pass validated seven live-client suites (63 Qt cases),
including real scrolling at thumbnail zoom, engine-only rerun invalidation,
Unicode cursor preparation, cancellation, watchdogs and both SyncTeX directions.
Five additional complete rerun-suite passes cover rapid reruns and error repair.
The full application passes 1,613 cases across 29 suites, including the real
Biber workflow with Live output selected; all 18 native-tooling tests pass.
Linux execution remains for CI.

One additional upstream checkpoint-reuse check remains timing sensitive: after
cancelling preparation, the engine's 10 ms worker synchronization fallback may
discard the speculative checkpoint. Its edited PNG arrives, but the optional
checkpoint-reuse assertion times out (eight of nine upstream engine checks
passed). Qt's preparation cancellation, subsequent editing and source-navigation
checks pass. Keep checkpoint reuse best effort rather than guaranteeing it under
load.

## Hyperref links

Live pages use the engine's existing link rectangles and named destinations.
A plain click follows an internal link or opens an HTTP, HTTPS or email link;
hover shows its target and the pointing-hand cursor. Ctrl/Command-click still
uses SyncTeX, and modified zoom gestures keep their existing behavior. Links
work on secondary continuous pages and when returning to cached pages.

Internal links ask the engine to locate a named destination, compiling far enough
to discover a target that has not been reached yet. The client loads or restores
that page, then reveals its actual destination coordinates. Manual navigation,
source changes and a bounded wait cancel the pending jump; a same-sequence source
barrier also clears abandoned native destination demand. Queued viewport layout
updates cannot override an explicit link navigation.

Annotation metadata belongs to the same revision, source sequence, page and width
as its image. It travels with cache entries and background exports, and is
replaced even by a zero-byte PNG patch. Stale images and gestures interrupted by
an edit, page recycling or dragging cannot follow links. Region exports for the
magnifier do not replace page annotations.

The live client follows the extension's external-URI policy (HTTP, HTTPS and
mailto). External opening and failure dialogs are shared with the saved-PDF
viewer through `ViewerLinks`; that viewer retains its existing local-file URL
resolution and supported actions. No engine update is needed for this port.

Validation includes real hyperref TOC entries, references, citations, wrapped email
links, lazy destinations, missing targets, cached and secondary-page clicks,
source synchronization, stale-link suppression and geometry updated at reveal.
URI-dispatch tests intercept desktop URL handlers rather than launching browsers.
All ten client suites now pass (101 Qt checks), as do the application's 1,613
checks, including its real Biber workflow. This validation ran on macOS.

## Source-follow routing and viewport stability

The engine pin now includes the extension's authoritative source-record routing
(`3fbbd75`) plus both native rerun fixes. A new source barrier cancels the prior
source search; manual preview navigation cancels pending native source demand
and releases any foreground export credit retired without a reply. Older engine
revisions cannot invalidate a newer frame. Searches without a matching typeset
record release idle rendering once the current pass is known complete; slow
search replies remain eligible until the user replaces that navigation.

Each Qt session retains the editor's active caret independently from the changed
range and the selection start used for preparation. Automatic following deduplicates
identical document/version/line/source-sequence/revision intents before sending,
so compiler flushes cannot create a search feedback loop. Manual page navigation,
scrolling and links hold the preview until the source selection or document version
changes, or an explicit source-sync command resumes following. Horizontal caret
motion updates that hold identity without resending an otherwise identical
line-based query. Engine recovery preserves the manual hold.

Source coordinates are revealed only after the destination frame is fresh.
The viewport follows the extension's comfortable region: up to 48 pixels, or ten
percent of the viewport, with four pixels of hysteresis. An already visible target
leaves each comfortable axis unchanged; an outside target centers only that axis.
Links retain their explicit destination navigation. Coalesced layout updates and
programmatic scrolling cannot turn a pending source reveal into manual navigation.

The sustained-editing corpus is frozen from extension `d175622`, with attribution
and regeneration instructions in `src/texpresso/tests/sustained/README.md`.
The opt-in Qt session test drives finite 100 ms early/middle/late edit streams on
naturally flowing 30/100-page documents, including manual navigation, an incomplete
paste, repair and undo. It then compares the live owning page against independent
XeLaTeX/standalone SyncTeX and unique text extracted from the clean PDF. The Linux
workflow runs both sizes and retains diagnostic artifacts on failure. This tests
routing correctness, rather than establishing Qt interface latency benchmarks.

Both document sizes pass locally: 732 mutations each, one retained engine process,
and matching early/middle/late owning pages from the independent oracle. The
30-page fixture ends at 31 pages (zero-based owners 0/15/29); the 100-page fixture
ends at 100 pages (owners 0/49/98). Compact provenance and results are recorded in
`docs/texpresso-source-follow-validation-2026-10-22.json`.

## Remaining work

- Measured Qt SVG/adaptive rendering.
- Full dependency reconciliation (missing/deleted files, external include
  directories), source renames, bounded idle-session eviction and memory budgets.
- Auxiliary-directory precedence in the engine.
- Automated editor interaction tests and Linux runtime verification in CI.
- macOS/Linux release packaging with complete Qt deployment, release signing,
  notarization and clean-machine checks. Existing macOS engine signing tools are
  available, but the development application is not a notarized release.
