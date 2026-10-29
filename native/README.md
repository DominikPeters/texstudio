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
python3 native/scripts/build.py --mupdf-source /path/to/mupdf --icu-prefix /path/to/icu
```

On Linux install the engine's build dependencies (see the live-preview CI), then:

```
python3 native/scripts/build_linux.py
```

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
or pausing. A failed pass bounds navigation to its available prefix without
claiming a final total. Last Page still reveals the last available page; when
there is no earlier total, its count is gray without a trailing `+`, since no
further page is currently available. Compiler diagnostics explain that boundary.
Only one discovery placeholder can extend the layout;
genuine blank pages remain pages and are never used to infer EOF.
Successful automatic convergence also records the exact total before restoring
the live compiler checkpoint used for fast edits. That total survives checkpoint
restoration and cursor prewarming, so a completed document loses its `+` without
requiring Last Page. Real source changes and new finishing passes invalidate it;
failed passes never supply an exact total. The additive `preview-stopped`
notification bounds failed passes at EOF even if no beyond-end image was
requested, removing their `+` and discovery placeholder without marking the
count as exact.

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

The standalone Qt `texpresso-pagination` test compares live counts with separate
XeLaTeX/PDF `pdfinfo` counts, including empty documents, blank final/intermediate
pages and two-column layouts. It also covers shrink/grow edits, failure/repair,
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
