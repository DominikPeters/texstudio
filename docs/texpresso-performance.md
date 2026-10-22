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
