# Edit preview figures in TikZ Editor

When the local TikZ Editor desktop app is detected, a live-preview figure can
show a subtle outline and **Edit in TikZ Editor** after the tooltip delay. The
button includes the installed editor's app icon and sits beside the figure's
outline, preferably above its top-right corner. It stays within the visible
preview; moving to the button keeps the outline visible. Its context menu offers
the same action. Ordinary figure clicks retain the preview tool; hyperlinks take
precedence over figure hover targets. Hovering does not request a compiler step,
inverse SyncTeX query, or another render.

The handoff opens the original source file and selects its literal picture using
`tikz-editor --source-line=N /absolute/source.tex` (one-based source line). It
requires the TikZ Editor source-line integration, added alongside this feature.
An older editor may open the file without selecting that picture. Existing TikZ
Editor windows receive the request through its single-instance handling.

TeXstudio asks to save a modified source buffer before opening it. TikZ Editor
reuses an already-open file and preserves unsaved visual edits rather than
replacing them with the disk copy. Its existing linked-file synchronization
handles saves and external-change conflicts. No temporary extracted drawing is
silently written back into a paper.

## Discovery

- macOS: application registration for `com.tikz.editor`, then standard user and
  system Applications directories. The executable comes from the bundle's
  `CFBundleExecutable` metadata; it need not be named `tikz-editor`.
- Windows: App Paths registration, standard user/system install directories,
  and PATH. WSL source paths are translated to host-accessible UNC paths.
- Linux: registered desktop launchers and PATH.

For an unregistered portable installation, set `TEXSTUDIO_TIKZ_EDITOR` to the
executable, or set the `texpresso/tikzEditorPath` settings key. These are paths,
not shell commands (macOS also accepts an `.app` bundle). Discovery is performed once at startup; restart TeXstudio
after installing or moving the editor. No app is launched during discovery.

## Figure metadata and eligibility

The shared engine installs an immutable preview-only LaTeX hook when TikZ loads.
It annotates outer pictures using PGF's actual bounds and the DVI renderer's
transformation, including `scalebox` and `rotatebox`. It does not alter the paper
on disk or normal build commands. Nested pictures inside node labels are
suppressed, so one diagram does not contain dozens of competing hover targets.

`tikzFigures: true` advertises optional image `annotations.figures` entries with
`bounds`, `file`, `line` and `endLine`. Bounds use page points with top-left
origin. **The source fields are execution locations, not guaranteed literal
source ranges.** Lines are one-based and the end is exclusive. TeX executes a
collected macro argument at its closing line, so a multiline `scalebox` can
report that line for both endpoints. TeXstudio resolves it only when a balanced
source argument ending there contains a unique literal picture; it never picks
the nearest preceding figure. Ambiguous same-line/grouped pictures are omitted,
including a literal picture plus a macro-generated picture that both execute at
the same collected argument's closing line.

Eligibility also requires width >= 24 pt, height >= 18 pt and area >= 900 square
points, independently of preview zoom. This omits tiny inline symbols. A macro
call without an unambiguous literal source picture is omitted even if its output
is large. Short `\tikz{...}` drawings and externally cached graphics currently
have no direct editing handoff. Figure actions use the same fresh-frame
revision/sequence discipline as links. Cached figure metadata is bounded.

## Background verification

```sh
python3 native/texpresso/test/test-tikz-figures.py native/texpresso/build/texpresso
QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=dummy \
  TEXPRESSO_TEST_ENGINE="$PWD/native/texpresso/build/texpresso" \
  build-texpresso/texpresso-tikz-tests
```

Use a writable disposable `XDG_CACHE_HOME` when testing an unpackaged engine.
The native test compares the live PNG byte-for-byte with an ordinary-build XDV
render to check that the metadata hook changes neither layout nor pixels.
Qt tests cover source resolution, protocol validation, hover activation, stale
frames, nested/macro/tiny pictures, transforms, included sources and edits.
The real-session comment-edit regression shifts figure source lines forward and
back in a UTF-8 included file. It checks fresh source routing and unchanged
pixels/bounds while requiring display-list cache hits without misses.

`TEXPRESSO_TEST_TIKZ_PAPER` optionally points at `approval-irv-journal.tex`. The
test uses a disposable excerpt containing its original nested macros and first
large diagram at their original line numbers. It expects exactly one eligible
target despite the many nested/inline pictures. Its older ACM class also fails
ordinary XeLaTeX with a duplicate `c@conjecture` declaration on the current local
TeX installation, so the excerpt isolates figure behavior from that class issue.
The original paper and project are never modified or opened interactively.
