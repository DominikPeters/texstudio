# Live and saved-PDF viewer comparison

Source audit: 2026-10-31, TeXstudio Espresso's current branch. This compares
working features, including the PDF viewer's menus and docks, rather than only
its toolbar. It does not propose implementing all of these at once.

## Already shared or present in both

The live viewer has first/previous/next/last page controls, direct page-number
entry, continuous and single-page modes, zoom presets/slider/custom percentage,
actual size, fit width/window/text width, trackpad zoom, hand dragging and the
magnifier. It reuses the PDF viewer's toolbar components, cursors and magnifier.
It also has forward/inverse source synchronization, cursor following in the
source-to-preview direction, hyperref links to positions, external links, page
coordinates copied with Ctrl+Shift+click, grayscale and color inversion through
the shared configuration. Both are hosted by `ViewerPanel`'s Live/PDF switch.
The editor adapter also maps existing live actions to the managed PDF action IDs,
so customized shortcuts for those shared actions are already supported.

Live compilation, temporary-error retention, per-project engine sessions,
prewarming and compiler output integration are additional live-viewer behavior.
The final-PDF viewer naturally continues to show the saved build output.

## Gaps and feasibility

| Working saved-PDF feature | Live status | Port feasibility and dependencies |
| --- | --- | --- |
| Back/Forward navigation history, including position within a page | Missing | **Small, high value.** Store page coordinates and zoom/scroll per project, record explicit jumps and links, restore without recording another jump. Reuse toolbar slots/icons. Stale page positions need clamping after edits. |
| Temporary highlight at a forward-SyncTeX destination | Live scrolls to the point without the PDF viewer's highlight overlay | **Small** for a timed point/line marker, using the shared highlight color/duration; **medium** for matching exact SyncTeX rectangles, since the live source-location reply currently exposes a point. |
| Find / Find Again, case-sensitive and whole-word search with highlighted results | Missing | **Medium to substantial, high value.** Reuse the search panel UI, but Poppler search cannot operate on a live DVI display list. Add revision-scoped text/quad extraction or a search command to the shared engine; support lazy pagination, cancellation and changing results. The existing `preview-search` command searches for a *source position*, not text. |
| Select text and copy, including drag/Shift extension and double-click words | Missing | **Medium to substantial, high value.** Requires text with glyph/word geometry, reading order and Unicode from the live engine. MuPDF structured text is a useful basis, but TeX font encodings, XDV glyph-only records, ligatures and mathematics need fidelity tests. Share the selection overlay/UI with the PDF viewer after separating Poppler-dependent access. |
| Contents / PDF bookmarks dock | Missing | **Medium.** An initial live outline can use TeXstudio's existing source structure and forward SyncTeX, including unsaved sections. Exact PDF bookmark parity needs bookmark specials/metadata from the engine. These are different sources of structure and should be identified in the UI. |
| Overview thumbnails | Missing | **Medium.** Reuse dock layout with a renderer-independent thumbnail model and a small bounded background queue/cache. Request low-resolution images without changing the foreground width, priority or source generation. |
| Facing pages and arbitrary page grids; starting-column offset and single-page/grid stepping | Missing; live layout is one vertical column | **Medium.** Layout geometry can be shared/extracted from the PDF viewer; no new engine primitive is required. Must adapt visible-page demand, link/SyncTeX mapping and pinch anchoring. |
| Cursor follows scrolling, and synchronization among multiple views | Only source-to-preview following | **Medium.** Inverse SyncTeX already exists, but scroll-driven queries need throttling and loop suppression. A cursor movement caused by preview following must not restart compilation or source prewarming. Multiple live views also need an explicit policy for sharing a session and foreground demand. |
| Windowed/embedded viewer, clones, full screen, presentation controls and laser pointer | Live pane remains embedded | **Small to medium** for moving the existing panel into a window or full screen; **medium** for presentation and independent synchronized views. The shared `PDFLaserPointer` is reusable. Moving a pane should preserve the engine; cloning needs the demand-sharing policy above. |
| Go to Page dialog and arrow/Page-Up/Page-Down boundary behavior | Direct page entry and shared action shortcuts exist; no full boundary-navigation parity | **Small.** Add the page-dialog shortcut and port the PDF widget's scroll-or-change-page behavior, particularly in single-page mode, without stealing editor keys. Existing shared actions already honor managed shortcut overrides through `texstudio_texpresso.cpp`. |
| Auto-hide toolbar and viewer enlarge/shrink controls | Missing as live-specific controls | **Small.** Share these through the host panel rather than duplicate rendering logic. |
| Direct Invert Colors / Grayscale actions | Live rendering honors the shared settings, but has no corresponding actions in its own menu | **Small.** Share the actions/configuration binding and repaint cached images. This needs no engine change. |
| Viewport-clipped rendering at high zoom | Main live view renders a whole page; magnifier already requests a crop | **Medium, high performance value.** The engine's existing region requests provide a basis for viewport tiles. Needs tile cache/composition, cancellation, source fences and correct full-page link/SyncTeX coordinates. This avoids paying for offscreen page pixels or hitting the full-page raster budget at extreme zoom. |
| PDF annotation inspection/editing, file information, embedded-font list, printing, external viewer, split/merge | No corresponding live-document data or output file | **Keep on the PDF side.** These operate on the final saved PDF. Printing/sharing unsaved output should first run a normal build; persisting review annotations onto an ephemeral DVI preview needs a separate product design. PDF multimedia also belongs to saved-output handling; video is disabled in this build. |

The PDF source still declares `kSelectImage` / `actionSelect_Image`, but the
selection tool is not wired up. Likewise, a declared action is not evidence of a
working rotation, cropping or other feature. Those are excluded from the gaps.

## Suggested order

1. Navigation history, a SyncTeX destination marker and remaining keyboard
   navigation parity: useful, low risk, no shared engine changes for the initial
   point-marker implementation.
2. Text search and selection on a shared engine text/geometry interface: the
   largest everyday functionality gap; coordinate this interface with VS Code.
3. Source outline and lazy thumbnails; facing-page layout if useful to users.
4. Viewport tiles for very large previews/high zoom, then optional window and
   presentation features. Keep saved-PDF inspection and output operations in PDF mode.

## Source anchors

- [Shared controls and default shortcuts](../src/pdfviewer/ViewerControls.cpp)
- [Live viewer tools, layout, actions and context menu](../src/texpresso/texpressowidget.cpp)
- [Live protocol and cache](../src/texpresso/texpressopreview.cpp)
- [Saved-PDF menus, tools, history, selection and search](../src/pdfviewer/PDFDocument.cpp)
- [Saved-PDF docks](../src/pdfviewer/PDFDocks.h)
- [Shared pane host](../src/pdfviewer/ViewerPanel.cpp)
- [Editor adapter and managed shortcuts](../src/texstudio_texpresso.cpp)
- [Reusable magnifier and laser pointer](../src/pdfviewer/ViewerTools.cpp)
- [Engine source-search command](../native/texpresso/src/frontend/editor.c)
- [DVI/XDV glyph-to-text mapping](../native/texpresso/src/dvi/dvi_prim.c)
