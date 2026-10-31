# Qt live-preview benchmark

## Codec comparison

`texpresso-codec-benchmark` compares our production PNG writer with libwebp,
including Qt's actual PNG/WebP decoding. It is an opt-in developer target;
the installed app and production protocol are unaffected. On macOS with
Homebrew Qt, shared MuPDF, SDL2 and libwebp, first build the pinned engine's
`build/frontend/preview.o` and `build/frontend/renderer.o` using its Homebrew
Makefile configuration, against the same MuPDF headers/library used below.
For a clean development checkout, those objects can be built without XeTeX:

```sh
make -C /absolute/path/to/built/texpresso config
make -C /absolute/path/to/built/texpresso/src/frontend \
  ../../build/frontend/preview.o ../../build/frontend/renderer.o
```

An existing matching development engine tree works too.
Use the engine tree containing those objects as `TEXPRESSO_ENGINE_BUILD`:

```sh
cmake -S src/texpresso -B build-codecs -DCMAKE_BUILD_TYPE=Release \
  -DTEXSTUDIO_TEXPRESSO_BENCHMARKS=ON -DTEXSTUDIO_TEXPRESSO_TESTS=OFF \
  -DTEXPRESSO_ENGINE_BUILD=/absolute/path/to/built/texpresso \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build-codecs --target texpresso-codec-benchmark --parallel
python3 src/texpresso/benchmarks/benchmark_codecs.py \
  --benchmark build-codecs/texpresso-codec-benchmark \
  --engine-build /absolute/path/to/built/texpresso \
  --output /tmp/texstudio-codecs
```

The output directory must not exist. The runner generates a public three-page
fixture with installed XeLaTeX and a deterministic RGB image; `--fixture` can
reuse that PDF. It runs fresh processes serially, with two warmups and seven
samples per case. Defaults cover 1200/2240 physical pixel widths, text/math,
vectors, RGB noise and small text patches, across PNG and fast lossless,
near-lossless and lossy WebP configurations. `--widths` and `--codecs` select
subsets. Run on an otherwise quiet machine. Shared MuPDF linking was tested
on Homebrew macOS; static MuPDF builds may need additional dependency libraries.

The JSON records encoding wall/CPU time, Qt decoding time, encoded/decoded
bytes, peak process RSS, grayscale conversion cost and pixel-error metrics.
Lossless modes must reproduce every source pixel. No timing threshold is tested.
Crops and raw JSON are retained; full encoded pages are removed by default to
save disk space (`--keep-images` retains them). See the
[2026-10-31 results](../../../docs/texpresso-codecs-2026-10-31/README.md)
for measurements, limitations, exact batches and the decision to keep PNG.

## PNG compression comparison

The codec harness can optionally compare the production PNG writer with
zlib-ng's native streaming API and libdeflate's whole-buffer API. These are
benchmark dependencies only. Build zlib-ng 2.3.3 (commit
`12731092979c6d07f42da27da673a9f6c7b13586`) with its native API:

```sh
git clone --depth 1 --branch 2.3.3 https://github.com/zlib-ng/zlib-ng.git /tmp/texpresso-zlib-ng
cmake -S /tmp/texpresso-zlib-ng -B /tmp/texpresso-zlib-ng/build \
  -DCMAKE_BUILD_TYPE=Release -DZLIB_COMPAT=OFF -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build /tmp/texpresso-zlib-ng/build --parallel 4
```

With libdeflate headers/library available (tested with Homebrew 1.26), add
`-DTEXPRESSO_ZLIBNG_BUILD=/tmp/texpresso-zlib-ng/build` to the codec CMake command
above, then rebuild `texpresso-codec-benchmark`. Run:

```sh
python3 src/texpresso/benchmarks/benchmark_codecs.py \
  --benchmark build-codecs/texpresso-codec-benchmark \
  --engine-build /absolute/path/to/built/texpresso \
  --output /tmp/texstudio-png-compression \
  --widths 1200 2240 3360 --codecs png ng1 ng2 deflate1
```

`ng1`/`ng2` are zlib-ng compression levels; `deflate1` through `deflate6` select
libdeflate levels. Add `-crc` to use zlib-ng's CRC routine instead of the system
zlib routine. The matched writers use the same MuPDF output buffer operations,
grayscale scan, unfiltered rows, resolution metadata and 64 KiB IDAT chunks.
The candidate wrapper is compiled at `-O2`, matching the production native
objects. Qt decodes every output and lossless equality is required. The report
records explicit scratch buffer sizes, excluding compressor-internal state and
encoded output; process RSS includes rasterization and Qt and is less precise.
See [results and tradeoffs](../../../docs/texpresso-png-compression/README.md).

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
