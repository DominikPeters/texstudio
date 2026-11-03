# Editor and live-preview memory

The 2026-11-03 investigation found a source-editor raster leak independent of
TeX compilation. `QDocumentPrivate::drawTextLine` allocated a `QPixmap` for the
highlighted current line, but only transferred ownership for non-current lines.
The current line's temporary pixmap was never deleted. A logical source line
can wrap into a tall paragraph, making each leaked raster several megabytes.
Typing, cursor blinking and other repaints can all accumulate these allocations.

The temporary now has scoped ownership. Non-current lines transfer ownership
to the cache; highlighted current lines release the raster after painting.
The existing drawing path is unchanged. Source-line cache costs now include
pixel dimensions and depth, including Retina pixels, with a 16 MiB budget per
document instead of a count of 100 potentially very large lines. Eviction only
requires a line to be rendered again when revisited.

The generated regression paints a wrapped paragraph 32 times at 2x resolution,
after warming layout/font allocations. On macOS arm64 / Qt 6.12.0:

| Measurement | Before | After |
| --- | ---: | ---: |
| Additional editor physical footprint | 68,239,360 bytes | 245,824 bytes |
| Rendered-pixel SHA-256 | `6fd6dc74425d25c89e7a3e84dddddc7bb49ecc131a9c1597bddb4660f6152175` | identical |

Visiting 32 other wrapped paragraphs retained seven cached rasters totaling
14,818,496 bytes. These measurements reproduce a real leak and show its removal;
they do not establish the exact composition of the user's earlier 45 GB peak.

The same allocation audit found smaller leaks in accessibility notifications
for caret movement and content changes. Those events now have scoped lifetime,
following [Qt's notification example](https://doc.qt.io/qt-6/accessible-qwidget.html#qaccessiblewidget-example),
while preserving the notifications and their payloads.

Run the focused Debug-editor regression without opening a visible window or
using the normal profile:

```sh
QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=dummy \
TEXSTUDIO_TEST_EDITOR_MEMORY=1 \
TEXSTUDIO_EDITOR_MEMORY_REPORT=/tmp/editor-memory.json \
build-cursor-app/texstudio.app/Contents/MacOS/texstudio \
  --auto-tests --start-always --no-session --config /tmp/editor-memory-profile
```

Use a new disposable configuration path for each run. The test checks retained
memory after warming, repeated-paint pixel equality and the actual total bytes
of cached long-line rasters. It uses macOS physical footprint (including memory
that RSS can miss after compression/swapping), or Linux RSS for this short,
isolated regression. Other platforms skip the memory test.
