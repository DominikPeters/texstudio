# PNG compressor experiment

zlib-ng is worth adopting for live preview. Its streaming level 1 is the fastest
tested choice in the final comparison; level 2 is a useful smaller-file tradeoff.
libdeflate improves on the current system zlib but is less attractive for this
pipeline because it requires whole-image scratch buffers. All candidates still
produce standard, lossless PNG and use Qt's existing decoder.

The initial experiment changed the opt-in benchmark only. The engine, its build
dependencies, and the installed app have not been switched. A production change
should use zlib-ng's separate native API to avoid replacing the system zlib
symbols used by other libraries, pin/build it for macOS and Linux, and run the
existing native PNG tests with both backends. Intel and Linux performance remain
unmeasured here.

The subsequently approved implementation enables zlib-ng **level 2** in bundled
macOS/Linux builds, with a system-zlib fallback for standalone builds. The
actual C production writer passed the native PNG tests with both backends and
Qt rendering/scrolling/link/diagnostic tests with zlib-ng. A verification run of
the actual production function (rather than the C++ experimental wrapper) at
2240 px measured 10.86 ms text, 8.81 ms vectors and 77.90 ms noise encoding;
all outputs decoded exactly. See [production verification](production.json).
Bundlers build the locked native static library, run the PNG regression and
verify the backend/version/level from capabilities before packaging.

## Matched comparison

Apple M1 Pro, macOS 15.7.9 arm64, Qt 6.12.0, MuPDF 1.28.5,
zlib-ng 2.3.3 (commit `12731092979c6d07f42da27da673a9f6c7b13586`),
libdeflate 1.26. Baseline is the actual production `txp_preview_png` linked to
macOS `/usr/lib/libz.1.dylib`, not Qt's PNG encoder. Candidate wrappers and the
production objects use `-O2`; zlib-ng's own library uses its Release settings.

Times below are **encoding / Qt decoding**, milliseconds, each a median of seven
samples after two warmups. Baseline uses the final repeated run in each case;
both baseline runs and every individual sample are in
[matched.json](matched.json). Sums of stage medians describe codec cost, not
measured edit-to-screen latency.

| Content, 2240 × 3168 | Current zlib 1 | zlib-ng 1 | zlib-ng 2 | libdeflate 1 |
|---|---:|---:|---:|---:|
| Text/math | 19.70 / 3.57 | 7.89 / 4.00 | 8.87 / 4.17 | 12.39 / 4.55 |
| Colored vectors | 30.52 / 15.37 | 8.22 / 15.62 | 9.06 / 17.05 | 21.80 / 17.30 |
| RGB noise image | 148.08 / 31.23 | 56.28 / 31.50 | 79.51 / 32.82 | 62.05 / 31.73 |
| Word patch, 256 × 80 | 0.09 / 0.05 | 0.03 / 0.04 | 0.06 / 0.05 | 0.05 / 0.05 |
| Paragraph patch, 1200 × 160 | 0.77 / 0.22 | 0.37 / 0.21 | 0.45 / 0.22 | 0.44 / 0.24 |

At this size, streaming zlib-ng level 1 reduces full-page encoding time by
60–73% and combined encode/decode time by 48–51%. It helps patches too, although
their absolute savings are small. At 3360 × 4752 (**15.97 MP**):

| Full page | Current zlib 1 | zlib-ng 1 | zlib-ng 2 | libdeflate 1 |
|---|---:|---:|---:|---:|
| Text/math | 37.59 / 5.26 | 16.17 / 6.08 | 17.43 / 7.09 | 25.14 / 8.91 |
| Colored vectors | 66.78 / 32.42 | 18.60 / 33.33 | 19.17 / 35.58 | 51.64 / 38.55 |
| RGB noise image | 331.39 / 69.53 | 122.22 / 64.67 | 176.66 / 71.75 | 139.26 / 70.22 |

The 1200 px cases also favor zlib-ng. The final sweep has 75 fresh process runs
and 525 measured encode/decode pairs. All 75 outputs passed pixel-for-pixel
equality checks against the original RGB raster after Qt decoding.

## Output size and memory

| Full page at 2240 px, KiB | Current zlib 1 | zlib-ng 1 | zlib-ng 2 | libdeflate 1 |
|---|---:|---:|---:|---:|
| Text/math | 276.8 | 316.9 | 235.2 | 231.4 |
| Colored vectors | 328.9 | 455.8 | 252.5 | 290.8 |
| RGB noise image | 6778.8 | 7392.0 | 6709.9 | 6695.5 |

Level 1 trades a 9–39% larger payload here for speed. Level 2 makes these files
1–23% smaller than current PNG, while still substantially improving latency.
For ordinary text/vector pages its encoding penalty relative to level 1 is
under 1 ms at 2240 px. On the image stress page it costs another 23 ms.
The size/latency choice is explicit; level 1 need not compress like zlib level 1.

The streaming wrappers allocate one row plus 64 KiB compressed scratch, as the
production encoder does. At 15.97 MP this is about **74 KiB** for RGB. The tested
libdeflate wrapper allocates filtered input plus its compression-bound output:
**30.5 MiB for grayscale** or **91.4 MiB for RGB** at that size (13.5 / 40.6 MiB
at 2240 px). These exclude compressor-internal state and encoded output.
The JSON's zero scratch count for baseline means not instrumented; it does not
mean that the production encoder allocates nothing.

Fresh-process RSS high-water marks are also recorded, but include rasterization,
allocator behavior, retained output and Qt images. They vary substantially and
must not be read as isolated compressor memory or added directly to expected
whole-app memory. The explicit buffer accounting is the reliable structural
comparison. A libdeflate design could reduce its output allocation by attempting
a smaller buffer and retrying, with an additional complexity/time tradeoff.

## Controls and exploratory runs

The matched wrappers use the same grayscale scan/packing, unfiltered scanlines,
MuPDF output buffer operations, 64 KiB IDAT chunks, resolution metadata, and
system-zlib CRC routine as production. Allocation/setup, row packing and
compression are included in encode time. Rasterization, file writes, quality
analysis and source compilation are excluded. Libdeflate input/output buffers
are uninitialized allocations so redundant zero-fill is not charged to it.
Output is retained until Qt decoding, just like the baseline. No codec uses
worker threads in this comparison.

An [initial sweep](level-sweep.json) covered zlib-ng levels 1/2/3 and libdeflate
1/2/3/6. Higher levels did not produce a compelling interactive-latency choice.
That exploratory wrapper used a QByteArray output and zero-filled vector
buffers, compiled at `-O3`; use the matched results above for the conclusion.
A [second sweep](crc-sweep.json), with `-O2` and the same exploratory buffers,
compared system CRC with zlib-ng CRC. It showed no consistent gain from the
checksum substitution. The final matched sweep uses MuPDF buffers and removes
the redundant libdeflate scratch zero-fill. Each batch records its own binary
hash. These three batches contain 330 process runs / 2310 measured pairs.

The fixture is the same public text/math, vector-hairline and deterministic
high-entropy RGB image used in the earlier WebP comparison. It contains no
user paper. Noise is a stress test, not representative photography. This is
one machine and corpus; the numbers exclude rendering, IPC, painting, and
cache hits that avoid encoding altogether.

See [reproduction instructions](../../src/texpresso/benchmarks/README.md#png-compression-comparison)
and [provenance](provenance.json). The upstream
[zlib-ng native API](https://github.com/zlib-ng/zlib-ng/blob/2.3.3/zlib-ng.h.in)
supports streaming and distinct symbols; the
[libdeflate API](https://github.com/ebiggers/libdeflate/blob/v1.26/libdeflate.h)
defines its whole-buffer contract and compression bounds.
