# Qt live-preview benchmark

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
