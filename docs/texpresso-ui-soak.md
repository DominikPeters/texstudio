# Full-editor live-preview soak

This opt-in test runs the real TeXstudio application, QEditor document tabs,
editor-to-engine adapter and continuous Qt preview. It complements the faster
session-only sustained tests. The normal test suite skips it. Requires a Debug
build with application tests, the bundled engine, Python 3, TeX Live including
the varied corpus packages, standalone SyncTeX and Poppler `pdftotext`.

For a new build, configure with `-DCMAKE_BUILD_TYPE=Debug` and
`-DTEXSTUDIO_ENABLE_TESTS=ON`, and supply the matching bundled engine as described
in `native/README.md`.

```sh
python3 native/scripts/ui_soak.py \
  --app build/texstudio.app/Contents/MacOS/texstudio \
  --output /tmp/texstudio-ui-soak-10min --seconds 600 --pages 30
```

On Linux use `--app build/texstudio`. The default Qt platform is `offscreen`:
the real widgets and paint handlers run, but there is no physical display.
Use `--platform cocoa` on macOS or `--platform xcb` on Linux to exercise an
exposed window. Keep that window visible during the run. Test input is sent
through QEditor's editing operations, not the system keyboard.

`--seconds` is a minimum workload duration, 10–3600 seconds; the final cycle
finishes before the test exits. Ten minutes is the default; thirty minutes is
`--seconds 1800`. Use `--seconds 10` for a single-cycle smoke check. Choose the
shared 30- or 100-page naturally flowing corpus with `--pages`. Output must be a
new directory. The runner isolates configuration and creates its own projects;
it does not use an existing app instance or user project.

Each cycle performs four fixed-deadline edit streams at 100 ms cadence, in
early/middle/late root content and an unsaved included buffer. Inputs do not
wait for preview frames. Between streams it waits for the final source target
to reach a fresh visible canvas paint. It also exercises wheel scrolling,
zoom, resize, project-tab switching, compile errors followed by undo/redo and
repair, Pause with an unsaved edit, and preview hide/restore. Stop/Restart runs
after the first cycle and again at the end; intervening cycles assert that the
same compiler session stays alive, so restarts cannot hide long-session issues.
The second project and unsaved buffers remain open across cycles. The test
checks the inactive project's session is retained and the stopped worker group
exits. Cycle screenshots are captured outside the paint timing observer.

At the end, actual editor contents are copied into an independent directory.
Two clean XeLaTeX passes per root provide PDFs and SyncTeX files. Five probes
check the final visible tokens and source-owning pages against both PDF text
and standalone SyncTeX, including the unsaved include and second project.

Artifacts include:

- `app.log`, bounded per-project engine/compiler tails and editor snapshots
  (also on failure), plus the final buffers used for independent compilation.
- `ui-result.json`: input deadlines, applied times, source-ready and Qt paint
  completion observations, source probes and periodic UI/session checkpoints.
- `processes.jsonl`: once-per-second app/owned-worker process states and RSS.
- `cycle-*.png`: the actual Qt preview captured after each recovery cycle.
- `oracle/` and `result.json`: independent compilation, probe results, phase
  summaries and process cleanup checks. Failures retain available evidence.

Paint completion means a fresh source target intersects the visible canvas's
paint region, followed by the next Qt event turn. The observer rejects a result
if the source epoch/revision or selected project has since changed. It does
not establish compositor presentation, physical-keyboard latency or that every
input painted before the next edit. Inputs without a current paint observation
remain counted. Each phase records maximum input lateness and flags timing as
invalid if it exceeds 20 ms; correctness checks still run.

RSS sums include shared/COW pages and profiling state, and are not unique RAM
or PSS. The report records resource history without imposing an unsupported
universal memory-growth threshold. A ten-minute pass is evidence for that
finite workload, not proof of hours-long stability.

The `TeXpresso live preview` workflow exposes an optional manual soak duration
of 1, 10 or 30 minutes. It builds a Debug application and uploads the evidence;
ordinary pushes and pull requests keep the existing fast tests and session
corpus checks. Run the script directly for visible-window or 100-page checks.

Validated on 2026-10-23 with macOS arm64, Qt 6.12.0 and shared engine
`2010b9f`, using the 30-page corpus:

- Ten-minute offscreen run: 609 seconds in the UI workload, 622 seconds including
  cleanup and independent compilation; 43 cycles and 5,504 scheduled edits.
  Both sessions remained retained, with the same main frontend verified across
  588 seconds of intervening cycles. All five final probes matched and no owned
  processes remained.
- Native macOS visible-window smoke: 128 scheduled edits, all recovery steps and
  five independent probes passed; no owned processes remained.
- Ordinary application suite with bibliography tests enabled: 1,613 passed,
  zero failed, one skip for this opt-in soak.

The long run observed 5,090 fresh current-source paints; 414 inputs lacked that
observation. Only 98 of 172 typing phases met the 20 ms cadence tolerance.
These are correctness results, not a cross-client performance comparison.
Hashes, resource summaries and probe results are recorded in
[the validation record](texpresso-ui-soak-validation-2026-10-23.json).
Linux, 100-page and 30-minute full-editor runs were not exercised in this local
validation; the new manual CI option has not been dispatched here.

The subsequent editor performance pass also validated the 100-page mode for
ten minutes, plus a visible-window 100-page smoke. See
[the performance notes](texpresso-performance.md#editor-work-found-by-the-ui-soak)
and [its validation record](texpresso-editor-validation-2026-10-23.json).
