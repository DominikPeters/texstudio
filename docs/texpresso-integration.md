# TeXpresso integration

Agreed design, 22 October 2026:

- Native Qt preview on macOS and Linux. Bundle the same pinned TeXpresso fork as
  the VS Code extension and use the user's installed TeX Live.
- Automatically start a session per project root. Tabs sharing a root share an
  engine. Switching projects preserves each engine and its page/zoom/scroll;
  inactive sessions pause compilation. Explicit Pause/Stop survive tab changes.
- Keep normal TeXstudio compilation and its PDF viewer for final PDF output.
- Start with one page at a time; port scrolling as the extension develops it.
- Reuse the existing editor and headless-preview protocols. Keep compilation,
  MuPDF rendering and SyncTeX in the engine process.

Implementation boundaries: process/framing, project session/buffer overlays,
preview scheduling, Qt widget, and the TeXstudio document adapter. Guard every
asynchronous image with process generation, source sequence, revision and request
ID. Keep stale pixels visible while updating, but disable their interactions.
Bound image allocation/cache memory and pending requests. Terminate the complete
engine process group on shutdown.

The editor adapter uses QDocument's UTF-16 `changedText` ranges and reconciles
whole-buffer reloads and edits suppressed by the collaboration signal. Use
TeXstudio's existing root resolution and engine-reported input paths. Keep
unsaved overlays across compiler backtracking and send them to every subscribed
project session.

Initial acceptance: install the bundled application, open two multi-file
projects, edit unsaved included text (including Unicode and undo), switch tabs,
and recover each project's correct preview without restarts or path setup.
Then extend dependency watching/diagnostics, SyncTeX, regions/patches, packaging
checks, and finally scrolling/SVG using the extension as the behavioral reference.
