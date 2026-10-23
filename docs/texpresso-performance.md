# Qt live-preview performance — 22 October 2026

This pass changes the Qt client, following the extension's page-cache and
deferred inverse-search approach. Both sides of the measurement use the same
bundled engine revision, `08684365`, to isolate the client changes.

## Changes

- Cache up to four decoded page/width variants with a 32 MiB byte budget.
  The cache belongs to one engine/source revision and is cleared on source
  barriers and engine invalidations. Returning to a cached page bypasses native
  rendering, PNG transport and decoding. Switching projects releases extra page
  images while retaining each project's current frame, page, zoom and scroll.
  Inactive decode completions cannot refill the extra-page cache.
- Before inverse search on a cached page, restore the engine's matching native
  display list. A page change, source mutation or invalidation cancels pending
  clicks. Forward search also marks native-page identity uncertain until a render
  restores it. Cached pixels alone never establish a valid source map.
- Paint unmodified images directly. Cache the grayscale/inverted pixmap by image
  identity and color settings instead of converting every repaint. Keep one
  converted image; changing pixels or color settings invalidates it.
- Merge model notifications within an event-loop batch into one viewer refresh.
  Invalidate stale image interaction synchronously. Status updates no longer
  repaint an unchanged page or restart the raster-width debounce timer; only a
  changed viewport width or project schedules that request.

## Reproducing measurements

The opt-in [benchmark](../src/texpresso/benchmarks/README.md) emits raw JSON samples
and native render/paint counts. Use Release builds and the same Qt and engine on
both sides. The baseline is commit `c832b0645`, which adds the harness without
optimizing the client. Correctness tests are separate from timing measurements.

Measurements use macOS arm64, Qt 6.12.0 and the offscreen platform plugin. Canvas
repaint work uses a 1600×2200 synthetic RGB image and an 800×1100 canvas, with five
warmup iterations and twenty measured iterations. Engine navigation uses a
three-page text/math fixture, primes two pages at the settled viewport width,
and performs twelve alternating revisits. Each run then sends 1,000 status
notifications in ten event-loop batches after the last navigation has settled.

Navigation timing stops when the controller has a valid image; cache hits can
complete synchronously before Qt paints. Repaint timings measure CPU Qt painting
to a reused image, not GPU presentation or physical screen latency. Results do
not measure typing latency, TeX compilation speed, cold startup, or SVG/TikZ
workloads. They are local observations, not cross-machine timing guarantees.

## Verification

Three sequential paired runs, with compilation and tests finished before the
measurement loop. Entries below are medians of the three per-run medians;
[raw samples](texpresso-performance-2026-10-22.json) retain each run's observations.

| Measurement | Before | After |
| --- | ---: | ---: |
| Controller image availability on a cached-page revisit | 9.33 ms | <0.01 ms |
| Native renders per 12 page revisits | 12 | 0 |
| Normal-color repaint | 0.76 ms | 0.76 ms |
| Grayscale repaint | 34.83 ms | 0.78 ms |
| 1,000 status notifications | 14.45 ms | 1.21 ms |
| Canvas paints during those status notifications | 10 | 0 |

The normal-color result is effectively unchanged. The large grayscale difference
comes from avoiding repeated full-frame conversion; the first conversion still
has to happen after an image or color-setting change. Cached navigation still
requires Qt to paint afterward.

The 16 Qt checks pass against the bundled engine, including real cached-page
inverse search, Unicode/undo/inactive-project edits, compile-error recovery,
process-group cleanup, stale decode rejection, page-cache invalidation and LRU
entry/byte budgets. Cached color conversion is checked against the original
converter for new images and setting changes. The color and magnifier pixel
checks also pass at 2× scale.

## Engine snapshot update

After measuring the Qt changes, the runtime was updated from `08684365` to the
extension's `5d906d6` snapshot from the same fork. This brings its checkpoint
reuse and interrupted-worker recovery improvements, the obsolete-render request
handling fix, export timing metadata and safe ownership of dashed stroke arrays.
The extension continues developing independently; this is a fixed, tested
snapshot rather than a moving branch reference.

The macOS runtime was reused from the extension's immutable dashes archive,
validated against the pinned source and every manifest file hash, and tested
with a clean environment. All 16 Qt checks and 18 native-tooling tests pass with
this snapshot. Linux CI builds this same source pin; Linux execution was not
available on the development host.

Next useful measurements are edit-to-visible-frame latency in large multi-file
projects and remaining native rendering/PNG costs on cache misses. Full-buffer
serialization in the editor adapter is also a candidate for a separate measured
pass; this change leaves its reconciliation behavior intact.

## PNG patches and magnifier regions

The next client pass uses the same `5d906d6` engine's PNG patch and region
protocols. A real-engine regression fixture renders a page containing thirteen
lines of text at the default 1200-pixel width, changes one digit, then appends
source whitespace that does not change its appearance. On the development
macOS host, its encoded image bodies were:

| Update | Encoded PNG body |
| --- | ---: |
| Initial full page | 20,305 bytes |
| Single-digit edit | 210 bytes |
| Visually unchanged edit | 0 bytes |

The small edit transfers 98.97% fewer image bytes in this fixture. This measures
image body size, excluding JSON metadata; it is not an edit latency or compiler
speed claim. The engine still draws the new raster before comparing pixels,
and large changes can correctly return a full frame. Qt decodes only the patch
PNG and composes it against the validated base in its image worker. Empty
patches preserve the existing image without a decode or pixel copy.

The same fixture requests a 600×300 native region around text and verifies its
pixels differ from an enlarged crop of the full-page raster. Detail arrives
asynchronously and updates the shared PDF magnifier, while its immediate crop
keeps dragging responsive. Only the latest pending lens position is retained.
The rendering checks also cover malformed bases and geometry, keyframe
recovery, stale source/lens replies, color conversion, fallback capabilities,
and interaction suppression during external builds.

Run these checks with the bundled engine:

```
QT_QPA_PLATFORM=offscreen \
TEXPRESSO_TEST_ENGINE="$PWD/native/.bundles/darwin-arm64/bin/texpresso" \
  ./build-texpresso/texpresso-rendering-tests
```

Use the appropriate Linux bundle path on Linux. The engine revision remains
shared with the extension; these changes require no engine modifications.

## Scrolling and cursor checkpoints

The subsequent port follows extension commits `c0424c9` (scrolling) and `2e07dff`
(cursor preparation), using engine `863e9880`, based on its `4919c95` snapshot
plus the settled-rerun fix. Continuous scrolling uses bounded canvases and the
existing 32 MiB cache. The ordinary four-entry limit grows with visible demand
when zoomed out, capped at sixteen entries and sixteen canvases. Visible pages precede idle neighbor prefetch;
each gesture permits at most one page beyond the known compiler frontier.

Cursor positions are resolved to engine aliases and UTF-8 offsets only after
200 ms of idle time. Prepared positions are deduplicated by source generation,
revision, file and 64-byte bucket, with a 16-entry history. Actual edits and
foreground rendering cancel speculative work immediately. This ports the
extension's scheduling behavior; the earlier measurements above do not measure
the latency benefit of cursor preparation or continuous scrolling.

## Shared native PNG caches (23 October)

The engine now follows the extension's `8576d13` performance snapshot, with the
two TeXstudio rerun fixes reapplied as `c8d1d14` and `9cf7741`. Both clients use
the resulting `9cf7741adb9fae3229e9bf75e33b783060c5a443` revision. The extension
thread reviewed the merge and passed its 50 native integration checks against
the same fresh bundle. The engine worker protocol changed, so frontend and
worker binaries must be updated together.

This adds bounded native caches for unchanged page display lists (eight entries,
32 MiB accounting budget) and encoded exports with their retained PNG rasters
(four entries, 32 MiB budget). Conservative keys include page content, fonts and
geometry. External resources, virtual fonts and other unsafe cases bypass
reuse. These budgets describe cache retention, not total engine memory. SyncTeX
and link annotations remain current. Compiler bursts also yield sooner to
incoming commands; failed or cancelled finishing passes cannot replace good
auxiliary state.

Qt continues requesting PNG. It now uses identified source searches and terminal
cancellation replies, so an older search cannot navigate a newer cursor intent
or retain request credit. The native suffix lookup fix is paired with actual-path
editor overlays, including unsaved files that do not yet exist on disk.

A controlled macOS arm64 comparison uses the same Qt 6.12 executable against
the old `09f0351` and new `9cf7741` bundles. After compiling a three-page text
fixture and priming pages zero and one at 1600 pixels, each run alternates those
pages sixteen times. It clears the Qt LRU before every return and checks every
decoded image against its reference. Thus each sample includes native export,
IPC and asynchronous Qt PNG decoding/publication. It excludes widget painting.
Runs are serial, in old/new/new/old order, with one-millisecond event polling.

| Forced native page return | Old engine | Shared engine |
| --- | ---: | ---: |
| Median of 32 samples | 18.00 ms | 8.18 ms |

This is about 55% lower elapsed time in this fixture. It does not establish a
typing-latency improvement or the benefit for cache-ineligible pages. Existing
Qt image-cache hits already avoid native export entirely. Raw samples, engine
hashes, cross-engine decoded-pixel equality and the method are in
`texpresso-png-cache-performance-2026-10-23.json`.

Reproduce with two complete engine bundles, changing only the selected bundle:

```
cmake -S src/texpresso -B build-client -DTEXSTUDIO_TEXPRESSO_BENCHMARKS=ON
cmake --build build-client --target texpresso-png-cache-benchmark
QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=dummy \
TEXPRESSO_TEST_ENGINE=/absolute/path/to/bundle/bin/texpresso \
  ./build-client/texpresso-png-cache-benchmark
```

## Recent edit checkpoints and responsive startup

The next shared revision, `2010b9fec2c2450b4d06013544d5968d8605922b`,
keeps the PNG caches above and changes compiler snapshot retention. Pruning
reserves existing checkpoints at the two most recently used distinct edit
sites, plus an existing predecessor when needed for undo. It preserves the
usual rollback validity checks and requests no additional eager forks.

The extension's focused native regression exercises repair and undo near a
12.98-million-token prefix. It avoids the roughly 11-million-token replay
observed on baseline repair and checks earlier/different-file edits and clean
PNG equality. This is engine replay-work evidence, not a Qt typing benchmark.
The test retains 5–6 workers after pruning versus 3–4 before. The snapshot table
still has 32 entries; that is not a hard total-PID or resident-memory limit.

Worker startup also stops blocking the frontend on its greeting. The engine
incrementally polls the unchanged C02/S02 greeting, processes editor commands
between polls, and waits five milliseconds only while startup or its failure
cleanup needs progress. A 120-second monotonic deadline allows cold format
creation but terminates a stuck worker and reports failure. This improves
command responsiveness during startup; it does not reduce the time the worker
itself takes to initialize.

Qt integration tests gate a real worker before its greeting and verify that
source barriers and Pause are processed first, then Resume compiles the latest
overlay. They also check Stop/reaping and a shortened test deadline followed by
Restart with retained source. The extension independently covers fragmented,
coalesced, incompatible and prematurely closed greetings. No host protocol
adaptation or SVG support is needed.

## Editor work found by the UI soak

The full-editor soak recorded occasional missed input deadlines. A separate
20-second CPU sample of the real application editing the 100-page corpus
confirmed avoidable document reconstruction inside the synchronous edit path.
The adapter captured the entire QDocument for every range signal, then captured
it again for every queued contentsChanged reconciliation. A selected-text
replacement normally emits an erase and an insertion: four full captures.

The adapter now immediately applies each ordered UTF-16 range to the retained
QString overlay, which remains available to all project sessions and Restart.
A per-document zero-delay timer captures and reconciles the full editor contents
once per event batch. Reloads, collaboration edits and delayed macros retain
that full check; unknown/invalid ranges fall back to a snapshot. Range delivery
and interaction invalidation remain synchronous. This reduces a normal selected
replacement to one full document capture. The QString update can still copy or
scan source text; this is not a claim of constant-time editing.

A paired old/new/new/old benchmark uses the same real-QEditor test, Qt and
compiler, with native compilation disabled. There are ten warmups and 100
measured four-character replacements at each location. Entries are medians of
the two per-run medians:

| Corpus / location | Write before | Write after | Write + event turn before | After |
| --- | ---: | ---: | ---: | ---: |
| 30 pages / early | 1.42 ms | 1.20 ms | 2.14 ms | 1.74 ms |
| 30 pages / middle | 1.80 ms | 1.58 ms | 2.87 ms | 2.55 ms |
| 30 pages / late | 2.13 ms | 1.97 ms | 3.57 ms | 3.23 ms |
| 100 pages / early | 2.64 ms | 1.33 ms | 4.21 ms | 2.41 ms |
| 100 pages / middle | 3.88 ms | 2.62 ms | 6.71 ms | 4.87 ms |
| 100 pages / late | 5.08 ms | 3.96 ms | 9.24 ms | 7.43 ms |

These are macOS arm64 / Qt 6.12.0 **Debug application** measurements of host
editing work. Event-turn time includes other application syntax/UI work. They
do not establish Release performance, compiler speed, edit-to-visible latency,
or the cause of every input-deadline miss in the soak. Raw samples, hashes and
method are in [the editor performance record](texpresso-editor-performance-2026-10-23.json).
The reproduction command is in the benchmark README.

The 100-page profiling workload also exposed a source-search stall after
Restart: discovering an unsaved include supplied an overlay and advanced the
source barrier, cancelling a pending Sync request. No replacement search was
issued, so cursor prewarming could resume while the intended reveal was lost.
The session now preserves an active reveal through internal overlay/rescan
barriers, suppresses prewarming while awaiting acknowledgment, and reissues the
search against the latest sequence. Explicit Sync works with cursor following
disabled. Manual navigation and real editor mutations still replace/cancel the
intent. The shared engine remains at `2010b9f`.

Post-change verification passed 1,614 application checks (bibliography enabled,
two opt-in skips) and all 15 Qt integration suites. A 100-page UI smoke passed,
followed by a ten-minute 100-page run: 616 seconds in the UI workload, 631 seconds
including cleanup/oracles, 43 cycles and 5,504 scheduled edits. Both sessions
stayed retained between lifecycle checks. All five final independent PDF-text
and SyncTeX probes agreed, and no owned processes remained. The final engine
tail shows the interrupted middle-source search reissued against sequence
11274 after the dirty include advanced it from 11273.

The long run observed 4,737 fresh current-source paints; 767 inputs lacked that
observation. Only 93 of 172 typing phases met the 20 ms cadence tolerance. This
is correctness evidence on a larger corpus, not a before/after UI-latency claim.
A separately packaged macOS app also passed a visible-window 100-page smoke,
all five probes, strict signature verification and its packaged engine hashes.
[Validation details](texpresso-editor-validation-2026-10-23.json) retain hashes,
resource summaries, source probes and the initial test-fixture correction.
