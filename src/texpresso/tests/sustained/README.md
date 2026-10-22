The frozen corpus and edit plans come from `texpresso-vscode` commit
`d175622858218bc15cad15c34b4587d2db54405b`,
`scripts/profiling/varied-fixture.cjs`, under the accompanying MIT license.
`varied-30/varied.tex` and `varied-100/varied.tex` use naturally flowing prose,
math, theorem/proof blocks, TikZ and Unicode source comments. They do not force
page breaks. Their names describe the approximate page count under local TeX Live;
the test obtains the actual page count from independently compiled output.

Qt consumes the committed `.tex` and `plans.json` files without Node.js. To
regenerate the corpus after intentionally updating the frozen generator, run:

```sh
node src/texpresso/tests/sustained/regenerate.cjs
```

The normal `texpresso-sustained` test validates the corpus and all finite edit
plans; expensive compiler cases are skipped. To exercise the production Qt
session and independent pagination oracle on both document sizes, run:

```sh
QT_QPA_PLATFORM=offscreen \
TEXPRESSO_TEST_ENGINE="$PWD/native/.bundles/darwin-arm64/bin/texpresso" \
TEXPRESSO_TEST_SUSTAINED=1 \
TEXPRESSO_SUSTAINED_ARTIFACTS=/tmp/texstudio-sustained \
build-texpresso/texpresso-sustained-tests
```

The full cases require local XeLaTeX (including lipsum, AMS, geometry, TikZ and
hyperref), standalone `synctex`, and Poppler's `pdftotext`. They run the same
finite early/middle/late edit plans as the extension: typing every 100 ms,
manual navigation away/back, incomplete paste, repair, and undo. Input deadlines
never wait for images. After editing, two clean XeLaTeX passes in a separate
directory provide a fresh SyncTeX file and PDF. The test compares the live
owning page for each anchor to both standalone SyncTeX and Poppler's unique
visible text match. It checks that editing retains the same engine process.
This is correctness coverage for the Qt implementation, not browser or Qt UI
latency evidence.

Set `TEXPRESSO_SUSTAINED_ARTIFACTS` to retain final/intermediate sources, engine
and compiler logs, oracle output, input deadlines and page probes in
`result.json`, including on failure. Without it temporary output is removed.
Use the Qt Test row selector `sustainedEditing:30pages` or
`sustainedEditing:100pages` to run just one compiler case.
