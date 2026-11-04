# Bundled TeXpresso

The engine is the same `DominikPeters/texpresso` `vscode` fork pinned by
texpresso-vscode. `native/texpresso` records an exact commit; update both clients
together after testing shared engine changes. The older web-wrapper branch is
unrelated. The application always launches its bundled engine with `-texlive`.
Users install TeX Live, but do not configure an engine executable.

The macOS builder, dependency lock, profiles, notarization tools and tests were
adapted from `DominikPeters/texpresso-vscode/native` (engine pin 0868436). Keep
behavior aligned with that implementation; eventual shared engine releases can
replace these source builds. All build inputs are supplied explicitly; building
does not download dependencies.

Initialize the engine with `git submodule update --init native/texpresso`.
On macOS, supply MuPDF and ICU at the versions in `dependencies.lock.json`:

```
python3 native/scripts/build.py --mupdf-source /path/to/mupdf --icu-prefix /path/to/icu \
  --zlib-ng-source /path/to/zlib-ng
```

On Linux install the engine's build dependencies (see the live-preview CI), then:

```
python3 native/scripts/build_linux.py --mupdf-source /path/to/mupdf --zlib-ng-source /path/to/zlib-ng
```

Both builders require the zlib-ng source revision in `dependencies.lock.json`.
They build its static native API and enable streaming PNG compression at level
2, preserving system zlib for MuPDF and other dependencies. `-capabilities`
reports `pngCompression`, which the builders verify before packaging. The
manifest records the compressor version, source revision and library hash;
notices include its license. Standalone engine builds retain system zlib level
1 unless built with `-DTXP_ZLIB_NG`, the generated zlib-ng include directory and
`libz-ng.a`. No libdeflate dependency or PNG/Qt protocol change is required.
See the [compression benchmark](../docs/texpresso-png-compression/README.md).

Bundles contain `bin/texpresso`, `bin/texpresso-xetex`, relocated runtime
libraries, dependency notices and `build-manifest.json`. macOS binaries are
ad-hoc signed by the builder; `notarize.py` supports Developer ID signing and
notarization of a separate bundle. Application release signing must sign the
nested helpers/libraries before the outer app. Linux bundles require the build
host's glibc baseline and system graphics/font configuration; glibc and the
ELF interpreter are deliberately supplied by the target system.

CMake requires a matching bundle by default on macOS/Linux. It discovers
`native/.bundles/<darwin|linux>-<architecture>`, or a packager can specify
`-DTEXPRESSO_BUNDLE_DIR=/path/to/bundle`. It validates the pinned revision,
platform, capabilities and file hashes before staging the entire runtime into
the app and installing it. This is a build input, never an end-user preference.
`-DTEXSTUDIO_ENABLE_TEXPRESSO=OFF` builds the original editor without live preview.

Validate native tooling with `python3 -m unittest discover -s native/tests`.
For a persistent, isolated macOS development app in `~/Applications`, use
[the local test-app generator and instructions](LOCAL_TEST_APP.md).
For opt-in long editing sessions through the actual editor and continuous
preview, see [the full-editor soak instructions](../docs/texpresso-ui-soak.md).
The runner supports 30/100-page projects, independent PDF/SyncTeX checks and
process/resource traces; normal application tests skip that workload.

Live preview discovers pages on demand. After edits, the previous completed
total remains visible in gray as the best estimate, with an explanatory tooltip.
If no previous total exists (or the new document has already exceeded it), `N+`
shows how many pages are available while the end is unknown. **Last Page**
explicitly compiles through the end and shows the exact physical page count in
the normal text color. This operation preserves
the current page while compiling and is cancelled by navigation, source edits
or pausing. A fatal or truncated pass bounds navigation to its available prefix without
claiming a final total. Last Page still reveals the last available page; when
there is no earlier total, its count is gray without a trailing `+`, since no
further page is currently available. Compiler diagnostics explain that boundary.
Only one discovery placeholder can extend the layout;
genuine blank pages remain pages and are never used to infer EOF.
Successful automatic convergence also records the exact total before restoring
the live compiler checkpoint used for fast edits. That total survives checkpoint
restoration and cursor prewarming, so a completed document loses its `+` without
requiring Last Page. Real source changes and new finishing passes invalidate it;
fatal or truncated passes never supply an exact total. The additive `preview-stopped`
notification bounds interrupted passes at EOF even if no beyond-end image was
requested, removing their `+` and discovery placeholder without marking the
count as exact.

On transient TeX errors, an already displayed page and its scroll position stay
visible even when the failed pass stops before that page. Source following and
cursor prewarming wait while compiler errors are present, then resume after
repair. The retained picture is stale and does not offer live link/SyncTeX hit
testing. Successful document shrinkage and explicit navigation still update the
page normally. Page availability does not regress within a source revision just
because an unchanged compiler checkpoint is replayed; replaying already
converged auxiliary output does not initiate another auxiliary pass.

The live viewer accepts native trackpad pinch zoom, Qt pinch gestures and smooth
Control-wheel zoom. Scaling uses the gesture position as its anchor and applies
the same zoom limits as the toolbar.

Live **Fit to Text Width** uses the PDF viewer's action, icon and shortcut. The
engine exports optional full-page `textBounds` alongside PNG frames (including
patches and zero-byte updates). A lightweight MuPDF text-only bounds device
excludes vector figures and images; its small cache is keyed by display-list
content token, independent of raster size and codec. Qt learns the combined
horizontal text range as pages are rendered, retains it across image eviction
and edits, and trims it at known EOF. Unlike the saved-PDF viewer's whole-document
scan, fitting does not force live pagination to finish. Blank pages use the
learned range, or fit the paper if no text geometry is available.

Backward SyncTeX queries a published page's source map directly, without first
exporting another PNG or rebuilding its display list. If a source map is not
available yet, the engine retains the existing lazy preparation path. A cached
page without the foreground text display list returns the source line without
a guessed word. Editor caret movement caused by this jump does not initiate a
forward search or cursor prewarming; actual subsequent caret movement or edits
resume those behaviors. `texpresso-rendering` checks the cached-page fast path
with native profiling, and `texpresso-prewarm` checks the editor callback cycle.

Hyperref destination lookup uses the incremental destination index directly.
A compiler checkpoint can precede opening its DVI output file; a link clicked
after cursor prewarming or convergence restoration must treat that empty index
as unresolved and continue compilation, rather than dereference an absent output.
The native `make test-preview-destinations` regression covers known/missing
destinations requested before the first compiler step. The Qt links suite also
checks a link after actual prewarming before the first shipout.

For an opt-in reproduction on a local paper, set `TEXPRESSO_TEST_ENGINE` and
`TEXPRESSO_TEST_PAPER`, then run
`build-texpresso/texpresso-links-tests providedPaperDestination` with
`QT_QPA_PLATFORM=offscreen`. It copies the paper, auxiliary files and declared
bibliographies to a temporary project without changing the originals. It follows
the first-page TOC link named by `TEXPRESSO_TEST_PAPER_DESTINATION` (default
`section.6`), verifies the target annotation and optionally checks the one-based
`TEXPRESSO_TEST_PAPER_DESTINATION_PAGE`. `TEXPRESSO_TEST_PAPER_ARTIFACTS` saves the
engine log when requested.

The standalone Qt `texpresso-pagination` test compares live counts with separate
XeLaTeX/PDF `pdfinfo` counts, including empty documents, blank final/intermediate
pages and two-column layouts. Completed output with recoverable TeX errors has
a confirmed total in the normal text color; fatal/truncated passes and old
totals after edits remain grey. Compiler errors stay visible independently.
It also covers shrink/grow edits, failure/repair,
obsolete frames and end-search cancellation. Run it after building the client:

```sh
TEXPRESSO_TEST_ENGINE=/absolute/path/to/bundled/bin/texpresso \
  ctest --test-dir build-texpresso -R '^texpresso-pagination$' --output-on-failure
```

To reproduce Last Page behavior on a local paper without changing its files,
set `TEXPRESSO_TEST_PAPER=/absolute/path/paper.tex` and optionally
`TEXPRESSO_TEST_PAPER_PAGES=15` and `TEXPRESSO_TEST_PAPER_ARTIFACTS=/tmp/pagination`.
Run `build-texpresso/texpresso-pagination-tests providedPaperLastPage` with the
same engine environment. This opt-in case copies the source, auxiliary files
and bibliography resources to a temporary project, clicks the viewer action,
and checks both the selected page and the actual scroll position. Logs are
written to the artifacts directory when requested. It supports papers with
local `addbibresource` files; other external assets must be copied separately.

The Linux builder currently follows the extension's Ubuntu build approach with
system MuPDF/ICU development libraries, recording the runtime library hashes and
glibc baseline. The compact pinned MuPDF/ICU profiles apply to the macOS builder.
The live-preview workflow validates Linux bundles and their installation; the
inherited upstream compatibility workflows disable live preview explicitly.

## Preview failure handling

Malformed or incomplete DVI/XDV data is reported over the headless protocol
instead of calling `abort()`. A page-local interpretation failure emits
`["preview-error", request, message]`; the engine discards that page's partial
drawing state and remains available for later pages or a corrected compiler
revision. A failure that makes the global compiler/output state untrustworthy
emits `["engine-error", message]` and exits normally with status 1. TeXstudio
shows that message in the live-preview status and retains the engine trace in
Show Output. Internal process invariants and operating-system failures remain
fatal because continuing from them is not safe.

The native safety regression exhaustively feeds every DVI opcode short input,
then checks truncated pages, malformed boundaries, missing fonts, partial
specials, include-path bounds and recovery of the drawing context:

```sh
make test-preview-safety
```

Run the same target with AddressSanitizer and UndefinedBehaviorSanitizer when
changing the DVI parser. The Qt `texpresso-diagnostics` suite additionally
checks that a bad page does not kill the process and that a global engine error
is displayed as a normal failure rather than as a crash.

## Engine compatibility audits

The TeX Live provider supplies packages and fonts to the embedded engine; it does
not replace that engine with the installed XeTeX. Its primitive table was derived from Tectonic format version 33; the current
worker uses TeXpresso format 1001 with the compatibility extensions below. The inherited reference-source import
[leaves the TeX Live 2022 paragraph extensions commented out](https://github.com/tectonic-typesetting/tectonic-staging/blob/70dbe0603c4b27f17b3b4dcb25a85e9da3050fe9/stage1/gen-ninja.py#L345-L347).
Those definitions live in Web2C change files, outside `xetex.web`. Compare the
change-file list in upstream [xetex.am](https://github.com/TeX-Live/texlive-source/blob/trunk/texk/web2c/xetexdir/am/xetex.am)
when refreshing the reference; comparing only the base WEB source misses them.

`scripts/audit_engine_compat.py` derives candidate names from the embedded table
and supplied upstream sources, then compares them in two disposable documents.
It creates a private format/provider cache, preserves the `xelatex` executable
name when it is a symlink, and records source/binary hashes with the results.
It separately reports missing definitions and `ifprimitive` disagreements;
presence alone does not establish equivalent behavior.

With the upstream files downloaded into an ignored local directory:

```sh
python3 native/scripts/audit_engine_compat.py \
  --engine "/path/to/bundled/bin/texpresso-xetex" \
  --xelatex /Library/TeX/texbin/xelatex \
  --upstream .native-sources/engine-compatibility/upstream/xetex.web \
  --upstream .native-sources/engine-compatibility/upstream/partoken.ch \
  --upstream .native-sources/engine-compatibility/upstream/showstream.ch \
  --upstream .native-sources/engine-compatibility/upstream/tracingstacklevels.ch \
  --output .native-sources/engine-compatibility/runtime-inventory.json
```

On 30 October 2026, 502 named candidates compared against TeX Live 2026 XeTeX
0.999998 found four absent primitives in the embedded 0.999994 engine:
`partokencontext`, `partokenname`, `showstream`, and `ignoreprimitiveerror`.
There were also 425 present commands with a differing `ifprimitive` result.
An isolated worker experiment corrected the primitive lookup's string-length
test from `length(prim[p].s1) - 1` to `length(prim[p].s1 - 1)`; this removed all
425 identity disagreements while leaving the four genuinely absent primitives.
The experiment's source and worker were restored afterward. Behavioral tests
for individual extensions and package fixtures are still needed before claiming
compatibility, including changes that add no primitive names.

The current worker implements those four primitives and the corrected lookup;
the same 502-candidate audit now reports no missing primitives or identity
disagreements. Its integer parameters use normal TeX scoping/serialization, and
paragraph hooks cover vertical boxes/centers, insertions/adjustments, output
routines, and alignment boundaries. `showstream` matches XeTeX's file redirection;
`ignoreprimitiveerror` bit 1 writes the `vsplit` warning only to the log.

The engine's `test/test-engine-compat.py` compares these behaviors against XeTeX
0.999998 (TeX Live 2026), including `microtype` title/footnote fixtures, format
reloads, and optional migration from an older worker. Both format serial and cache
filenames are versioned, so existing installations regenerate their format without
manual cache deletion. See `native/texpresso/src/engine/PROVIDERS.md` for the
standalone test invocation and format-maintenance notes.
