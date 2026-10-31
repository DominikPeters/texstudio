# PNG versus WebP for the live preview — 2026-10-31

Keep PNG. On this machine, none of the WebP configurations tested reduced
combined encoding and Qt decoding time for these pages or incremental patches.
Allowing a little loss did not change that result. Some WebP files were much
smaller, but local preview latency matters more than compressed file size here.
No production codec, protocol, engine pin or installed app was changed.

## Results

Physical page width **2240 px**, raster **2240 × 3168**. Times are milliseconds;
each cell shows **encode + decode = combined**, using the median of each stage.
The combined figure is a sum of stage medians, not a measured presentation time.

| Content | Current PNG | WebP lossless preset 1 | Lossy WebP q75, TEXT/method 0 |
|---|---:|---:|---:|
| Text and math | 18.56 + 3.41 = **21.97** | 76.90 + 12.50 = 89.40 | 93.55 + 26.19 = 119.74 |
| Colored vectors | 30.46 + 14.67 = **45.13** | 64.95 + 8.89 = 73.84 | 81.45 + 19.39 = 100.84 |
| High-entropy RGB image | 147.79 + 31.94 = **179.73** | 164.72 + 59.90 = 224.62 | 175.74 + 71.67 = 247.41 |
| Small text patch, 256 × 80 | 0.08 + 0.05 = **0.13** | 0.29 + 0.05 = 0.34 | 0.49 + 0.16 = 0.65 |
| Paragraph patch, 1200 × 160 | 0.82 + 0.22 = **1.04** | 3.34 + 0.48 = 3.82 | 4.53 + 1.79 = 6.32 |

The same main sweep at **1200 px** also favored PNG on combined time in every
case. Lossless preset 0, its multithreaded variant, q85, q95 and q95 with sharp
YUV conversion did not beat PNG. Sharp YUV was tested with ARGB input so that
the encoder actually uses it.

Supplementary runs checked other plausible choices at 2240 px:

| Configuration | Text encode / decode | Vectors encode / decode | RGB image encode / decode |
|---|---:|---:|---:|
| q75 TEXT, multithreaded | 78.48 / 24.41 | 69.57 / 19.25 | 165.91 / 73.63 |
| q75 DEFAULT, method 0 | 84.11 / 24.69 | 78.94 / 19.55 | 144.55 / 63.74 |
| Near-lossless 80, preset 1 | 78.26 / 12.95 | 139.33 / 8.62 | 208.18 / 52.89 |
| Near-lossless 60, preset 1 | 76.90 / 12.41 | 208.41 / 8.88 | 265.38 / 49.80 |

The DEFAULT preset on the RGB page is the interesting partial win: encoding
alone is slightly faster than PNG, and its payload shrinks to **0.99 MiB** from
**6.62 MiB**. Decoding takes about twice as long, leaving combined time around
**208 ms** versus **185–191 ms** for the PNG runs bracketing it. Multithreading
helps full-page WebP encoding but does not close the gap.

Near-lossless settings on preset 0 produced exactly the same decoded pixels
and byte counts as lossless preset 0 in this corpus. The additional preset 1
runs above exercised actual near-lossless changes: maximum channel error was
1 at strength 80 and 2 at strength 60 for the vectors/image. The text page
stayed exact. These smaller files still took longer overall.

## Size, memory and quality

| Full page at 2240 px | PNG payload | Lossless preset 1 payload | q75 TEXT payload | q75 DEFAULT payload |
|---|---:|---:|---:|---:|
| Text/math | 276.8 KiB | 195.5 KiB | 573.7 KiB | 304.1 KiB |
| Vectors | 328.9 KiB | 76.5 KiB | 316.0 KiB | 204.6 KiB |
| RGB image | 6778.8 KiB | 6633.2 KiB | 2010.4 KiB | 1015.8 KiB |

Our PNG writer already checks for neutral RGB pixels and emits single-channel
PNG, using zlib level 1 and unfiltered rows. This is a particularly inexpensive
path for TeX pages. Qt decodes the neutral PNG page as Grayscale8, **6.77 MiB**,
whereas all WebP variants here decoded to RGB32, **27.07 MiB**. Colored pages use
27.07 MiB with either format. This is a property of the tested Qt pipeline;
custom WebP decoding/storage could make different tradeoffs.

Converting the WebP text image back with Qt's `convertToFormat(Grayscale8)`
cost an additional **about 27 ms**. That is measured separately and excluded
from the tables. A specialized conversion and a grayscale protocol flag could
be cheaper, but would require more implementation and would not remove WebP's
encoding cost.

Fresh-process peak RSS was also recorded before quality analysis. For example,
lossless preset 1 on the RGB page peaked at **284 MiB**, versus **117–123 MiB**
for PNG. These are process high-water marks including fixture rasterization and
libraries, not isolated encoder allocations or expected whole-app memory.

PNG and the lossless WebP modes passed pixel-for-pixel equality checks. At q75
TEXT, the text page has PSNR 46.59 dB, maximum channel error 25/255 and no counted
dark pixels lightened past 128. Text and math remain readable in the inspected
crops, with small edge/white-background changes. Colored hairlines show clearer
desaturation and color bleeding: vector-page PSNR is 28.11 dB and maximum
channel error 201/255. Increasing quality to 95 does not eliminate that color
error. Whole-page PSNR includes white margins; the raw report also gives ink
error, changed-pixel counts and altered originally-white pixels.

These are decoded crops saved as PNG for comparison; view at their native size:

| Text/math reference | Lossy q75 TEXT | Lossy q95 TEXT |
|---|---|---|
| [Reference](text-reference.png) | [q75](text-q75.png) | [q95](text-q95.png) |

| Colored vectors reference | Lossy q75 TEXT | Lossy q95 TEXT, sharp YUV |
|---|---|---|
| [Reference](vectors-reference.png) | [q75](vectors-q75.png) | [q95 sharp](vectors-q95-sharp.png) |

Lossy incremental rendering would also need a composition test: compression
changes pixels outside the original ink, so dirty rectangles computed from
exact source rasters might need padding to erase old halos. This benchmark
times independent patches; it does not test that temporal behavior.

## Method and limits

Apple M1 Pro, macOS 15.7.9 arm64; Qt 6.12.0 Release; libwebp 1.6.0;
Homebrew MuPDF 1.28.5. Native PNG objects were built with `-O2` from engine
`33d904aa423c9b504ddda3dfa41105ac033ba8d1`. The harness calls the actual
`txp_preview_png` implementation, not Qt's PNG encoder. Source revision,
binary/object/PDF hashes, settings and all individual samples are in
[measurements.json](measurements.json).

The public [fixture](../../src/texpresso/benchmarks/resolution.tex) contains
text/math, colored vector hairlines and deterministic high-entropy RGB noise.
It contains no user paper content. The image is a compression stress case,
not a representative photograph; results do not establish performance for all
image types, operating systems, CPUs or codec versions.

Each codec/case runs in a fresh process. The source is rasterized once into
RGB888 before timing. Each process discards two warmups and records seven
samples. Encoding includes WebP RGB import, encoder setup, picture allocation
and release; the encoded output buffer stays alive until after decoding.
Decoding includes Qt reader/buffer setup, reading and reader destruction.
Quality calculation, output-file writes, optional grayscale detection/conversion
and source rasterization are excluded from codec times.

There were **155 process runs**, **1085 measured encode/decode pairs**, across
three serial batches. The main sweep reverses codec order at the second width
and repeats PNG at each case's end. Supplementary batches bracket candidates
with PNG. There is some timing variability—for example, full-vector PNG ranges
from about 45 to 56 ms combined across batches—but the candidates still lose
to the PNG runs in their own batch. CPU samples help distinguish parallel speedup
from reduced CPU work. No timing threshold is asserted in CI.

These are codec timings, not edit-to-screen timings. They exclude TeX compilation,
source-map work, raster drawing, IPC and painting. Existing incremental updates,
unchanged-frame suppression and caching continue to avoid much of this work in
the app. No WebP protocol integration was added for this experiment.

## Reproduction

See the [benchmark instructions](../../src/texpresso/benchmarks/README.md#codec-comparison).
The raw record has three batches: `main` at 1200/2240 px,
`presets-and-threads` at 2240 px and `near-lossless-preset1` at 2240 px.
Different binary hashes reflect additions of the supplementary presets and
more descriptive settings metadata; the existing codec paths were unchanged.

The [libwebp API documentation](https://developers.google.com/speed/webp/docs/api)
explains the settings. Encoder effort and lossy quality are different controls;
the benchmark records both rather than comparing ambiguous “quality” numbers.
