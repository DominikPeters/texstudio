# Distribution licensing inventory

This inventories the macOS TeXstudio Espresso package. It is a technical record
of what we distribute and the available licensing evidence, not a declaration
that a development build is ready for public distribution. A Linux release must
be inventoried separately against its actual binaries and system/bundled split.

The [2026-11-04 inspected build snapshot](distribution-inventory-macos-2026-11-04.json)
records all 98 code images, 33 installed dependency packages and the hashes of all
96 dictionary/thesaurus payloads, with verification results and remaining review
items. All dependency code origins matched by UUID, including three local Poppler
libraries whose exact source/build provenance still needs archiving. The full
post-sign report is retained in `.native-sources/license-inventory/`.
The packaged app passed signature/dependency and preview protocol checks, all 40
native tooling tests passed, and a disposable offscreen editor smoke test made
128 edits across a 30-page fixture with five independent PDF/SyncTeX probes and
no leftover test processes. This is packaging/startup evidence, not a new
long-session performance measurement. The verified candidate is staged at
`.native-sources/app-updates/current/TeXstudio Espresso.app`; installation remains
separate.

## Reproduce and inspect

`native/scripts/deploy_macos.py` now puts the inventory and collected notices in
`Contents/Resources/licenses/` before signing. The local test app generator
refreshes it after adding its launcher, icon and metadata. The existing engine
notices remain under `Contents/Resources/texpresso/notices/`.

For an independent, read-only inspection of a signed app:

```sh
python3 native/scripts/license_inventory.py \
  --app '.native-sources/app-updates/current/TeXstudio Espresso.app' \
  --output .native-sources/license-inventory \
  --qt-prefix /opt/homebrew/opt/qt \
  --cellar /opt/homebrew/Cellar \
  --library-path /tmp/texstudio-poppler/lib
```

The output directory must not already exist. Keep the latest report and compact
evidence, removing superseded experiment copies. The report contains:

- `README.md`: package/version/license tables and explicit evidence gaps.
- `inventory.json`: every Mach-O image, UUID, SHA256 and origin; all ordinary
  resources and hashes; every dictionary archive member hash; all root QRC input
  paths/hashes; engine build manifest; static renderer snapshot; review items.
- `notices/`: original package license/copyright/author files and in-tree evidence.
- `qt-sbom/`: original upstream Qt SPDX documents for the modules represented.

Code origins are matched against installed dependency binaries by Mach-O UUID,
which survives relocation and signing. A same-named library alone is not enough.
Locally built libraries without package receipts remain marked for source/build
provenance reconciliation. The local Poppler libraries are such a case.

The embedded inventory describes **the stage before final signing**. Signature
bytes, engine-manifest hashes and outer signatures subsequently change. Use an
independent post-sign inspection for final artifact hashes. The embedded inventory
excludes itself and code-signature resources to avoid recursive hashing.

Qt and Homebrew license expressions are retained verbatim as evidence. A whole
formula can include licenses for tools, docs and components we do not ship; the
per-target `binaries[].qt` records identify the distributed Qt modules/plugins.
The retained full SPDX documents also contain unshipped build targets. Neither a
formula expression nor the presence of a COPYING file certifies compliance, and
an `OR` expression does not require selecting every offered license.

## Main findings

| Component | Evidence and distribution implications |
| --- | --- |
| TeXstudio, QCodeEdit, inherited editor/PDF code | GPLv3 distribution baseline. Keep inherited per-file notices and provide complete matching modified source. About already declares GPLv3. |
| TeXpresso frontend | Original MIT terms, combined with the renderer and other dependencies below. Do not label the whole bundled runtime MIT-only. |
| XeTeX/Tectonic/SyncTeX worker | Mixed permissive code plus **GPL-2.0-or-later dvipdfmx** compiled into the worker. Provide the complete worker source and preserve its individual notices. |
| Compact MuPDF | **AGPLv3**. Disabling modules does not change this. Exact archive hashes are recorded in the engine manifest. |
| jbig2dec | **AGPL-3.0-or-later**, independently of MuPDF. The installed LICENSE supplies evidence where the Homebrew source SPDX says NOASSERTION. |
| Poppler | Upstream source-package metadata offers GPL2 or GPL3; the GPL3 option fits this app. Local rebuilt binaries need exact source/configuration archived. |
| Qt | The remaining shipped target SPDX records offer LGPLv3/GPL alternatives. Collect the applicable Qt and bundled third-party notices from matching source releases. |
| QuaZip/MiniZip | LGPL2.1 with an explicit static linking exception; MiniZip has zlib-style terms. Preserve both. |
| QMarkedScrollBar, adwaita-qt, other inherited code | Additional GPL/LGPL/per-file notices, recorded in `native/license-components.json`. adwaita's main style source permits GPL2 **or later**. |
| Other shared libraries | The generated report records exact installed versions, upstream source URLs/license expressions and available texts (OpenSSL, ICU, GLib, codecs, font libraries, SDL, etc.). |
| Dictionaries/thesauri | **92 spelling dictionaries and four thesauri** in the inspected app. Mixed licenses independent of Hunspell. Reconcile each exact payload, copyright and source; the generic upstream URL in README_others is insufficient. |
| Artwork, symbols, docs, templates and translations | Separate attribution/copying terms apply. About identifies Crystal LGPL, Oxygen CC-BY-SA-3.0 and Colibre CC0 artwork. File-level provenance and modifications still need reconciliation. |

### Virtual keyboard removal

Qt Virtual Keyboard implements an on-screen touch keyboard; it is not the
system's ordinary desktop input method. The editor does not link it or request
it. `macdeployqt` automatically copied the platform input-context plugin, bringing
in VirtualKeyboard, VirtualKeyboardQml and additional Quick dependencies.

Deployment now removes that plugin and both keyboard frameworks. It also removes
Quick/QmlModels/QmlWorkerScript only when no remaining code image requires them,
following the dependency graph transitively. A newly introduced real keyboard
dependency fails packaging instead of producing a broken app. QtQml remains:
TeXstudio uses its JavaScript engine for user macros. Native Cocoa input methods
are retained.

Upstream describes Virtual Keyboard as GPLv3/commercial, so removing the unused
component also removes its separate third-party notice requirements:
<https://doc.qt.io/qt-6/qtvirtualkeyboard-index.html#licenses-and-attributions>.
This does not remove the app's existing GPL/AGPL obligations.

### Static renderer and embedded data

`native/license-static-components.json` records the exact compact archive member
lists and hashes, plus font symbols observed in the unstripped frontend. It is
applied only to builds with matching archive hashes; a mismatch creates a review
item. Archive membership alone does not establish final linked retention.

For this build, `libmupdf-third.a` contains **LCMS2 only** (MIT). The frontend
retains **14 URW CFF font payloads** (OFL1.1) and **DroidSansFallbackFull.ttf**
(Apache2). The larger source tree includes many fonts that are absent from this
frontend; do not describe all of them as shipped. The hyphenation archive is in
libmupdf.a, but its resource symbol was not present in the unstripped frontend.
The 15 retained font symbols were also mapped to Mach-O file offsets and their
actual payload bytes matched the source font files exactly.

Unmodified URW/Droid/LCMS2 license files and the source CMaps' individual Adobe
copyright/license blocks are retained in `native/notices/mupdf/` and collected
into the app. MuPDF's Artifex ICC profiles and generated CMap code also belong
in the corresponding-source archive. Repeat the link/data review after renderer
profile changes; this snapshot is not a general future clearance.

## Remaining release work

1. Reconcile the exact dictionaries/thesauri against upstream snapshots; ship
   their full notices and any required source. Replace a payload only if its
   provenance cannot be established, rather than guessing from its language code.
2. Complete Qt's full license/third-party-attribution collection for the selected
   targets. The Homebrew Qt module installs provide SPDX data but omit most full
   license texts. Keep the distinction between package-level and target-level data.
3. Resolve the artwork/documentation/template exceptions, including the exact
   coffee-cup v3 SVG. An older logo has CC0 metadata; the v3 file does not repeat
   it, so confirm provenance instead of silently extending the older declaration.
4. Publish complete corresponding source beside the binaries, matching every
   release. Include recursive submodules, dependency sources, modifications,
   build/configuration/packaging scripts, private ICU filtering and static data.
   Preserve the local Poppler source/build recipe. A default GitHub source ZIP
   does not include submodule contents. Verify engine commits are public.
5. Provide an accessible Licenses/Source entry in About and clear release source
   links, together with instructions to build, modify and re-sign locally. Review
   release-channel terms separately if considering an app store.

No TeX Live distribution is included: users supply their own installation.
This avoids redistributing the installed TeX package/font collection; it does not
cover dependencies or font data actually embedded in our binaries.
