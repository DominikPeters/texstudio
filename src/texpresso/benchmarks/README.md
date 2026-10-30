# Qt live-preview benchmark

## Physical-pixel resolution budget

`texpresso-raster-benchmark` and
`native/scripts/benchmark_preview_resolution.py` compare raster budgets on a
public text/math, vector and deterministic RGB-image fixture. The helper builds
the PDF before measuring; runs old/new/new/old engines serially; and records
native export, controller frame publication, payload size, decoded image size,
and resident-memory samples. It also saves same-size text crops. No user paper
is used and no timing threshold is tested. See the
[2026-10-31 report](../../../docs/texpresso-resolution-2026-10-31/README.md)
for build/reproduction commands, raw samples, limits and measurement caveats.

## Editor buffer benchmark

For the real editor buffer path, use the application configured with
`-DCMAKE_BUILD_TYPE=Debug -DTEXSTUDIO_ENABLE_TESTS=ON`:

```sh
QT_QPA_PLATFORM=offscreen TEXPRESSO_TEST_UI_BENCHMARK=1 \
TEXPRESSO_UI_BENCHMARK_OUTPUT=/tmp/editor-buffer-benchmark.json \
  ./build/texstudio.app/Contents/MacOS/texstudio --auto-tests --start-always \
  --no-session --config /tmp/editor-buffer-benchmark-config
```

On Linux use `./build/texstudio`. It disables native compilation and measures
actual QEditor selection replacement and the following application event turn,
at early/middle/late locations in the 30/100-page corpora. Each case has ten
warmups and 100 measured edits. It checks the retained overlay against the actual
document, has no timing threshold, and is skipped by normal application tests.
Compare matching builds serially; Debug results do not establish Release or
edit-to-visible performance. See the 2026-10-23 editor measurements in
`docs/texpresso-performance.md` and the raw JSON record there.

## Preview rendering benchmark

Build and run this opt-in benchmark in Release mode:

```
cmake -S src/texpresso -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DTEXSTUDIO_TEXPRESSO_BENCHMARKS=ON
cmake --build build-perf --parallel
QT_QPA_PLATFORM=offscreen \
TEXPRESSO_TEST_ENGINE="$PWD/native/.bundles/darwin-arm64/bin/texpresso" \
  ./build-perf/texpresso-preview-benchmark > /tmp/preview-profile.json
```

Use the Linux bundle's path on Linux. Add `-DCMAKE_PREFIX_PATH=/opt/homebrew`
when needed to locate Homebrew Qt on macOS. No engine path is needed for just
the canvas repaint measurements.

The JSON contains all measured samples and medians. Repaint measurements use a
1600×2200 RGB image, an 800×1100 canvas and a reused offscreen target, discarding
five warmup iterations before measuring twenty repaints. They measure CPU Qt
painting, not GPU presentation. The synthetic color pattern also exercises
nontrivial grayscale conversion.

With an engine, the benchmark creates a three-page text/math document, lets the
initial viewport settle, and primes two pages. It measures twelve alternating
page revisits from `setPage` until the controller has a valid image. This includes
native rendering, transport and decoding on cache misses; cache hits return
synchronously. It is frame availability, not screen-presentation latency. The
native render count makes cache effects distinguishable from timing noise.
Finally, one thousand status notifications in ten event-loop batches measure
viewer update CPU time and canvas paint count.

Run sequential before/after samples with the same build type, Qt, engine,
platform plugin and machine. There are deliberately no timing assertions in CI;
correctness and cache-budget tests live in `tests/client_test.cpp`.
