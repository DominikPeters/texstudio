# Distribution licensing inventory

The macOS TeXstudio Espresso package now carries its library, dictionary, artwork
and documentation notices, an offline **Licenses and Source** page linked from
About, and matching project/dependency source downloads. The
[2026-11-04 inspected build snapshot](distribution-inventory-macos-2026-11-04.json)
records 96 Mach-O images, 34 dependency packages and all 96 dictionary/thesaurus
archives. This inventories the tested arm64 macOS package. Intel CI is prepared
but was not run for this work; Linux needs its own actual dependency inventory.

The candidate is staged at
`.native-sources/app-updates/current/TeXstudio Espresso.app`; it has an ad-hoc
signature and has not been installed, notarized or published. Its project source
archive is `.native-sources/releases/texstudio-espresso-macos-arm64-sources.zip`,
with an adjacent SHA256 file. Reusable dependency sources live separately in
`.native-sources/releases/dependency-sources/` with a browsable index.
**The binary download page must identify the matching project sources and exact
dependency sources.** These need not be a single ZIP or hosted on one server;
access to the matching versions must remain available. This follows
[GNU's source-on-another-server guidance](https://www.gnu.org/licenses/gpl-faq.en.html#SourceAndBinaryOnDifferentSites).
The monolithic ZIP was a packaging choice, not a license requirement.
A GitHub repository's default source download omits submodule contents.
Release-channel terms need separate review if an app store is considered.

## Evidence and licensing basis

| Component | Basis and supplied evidence |
| --- | --- |
| TeXstudio and inherited editor/PDF code | GPLv3 baseline, original per-file notices and modified source. `ESPRESSO-CHANGES.md` identifies this unofficial modified fork. |
| TeXpresso frontend | MIT code combined with the separately licensed renderer and worker; the entire runtime is not MIT-only. |
| XeTeX/Tectonic/SyncTeX worker | Mixed permissive code plus GPL-2.0-or-later dvipdfmx. Full pinned engine source, notices and build instructions are supplied. |
| Compact MuPDF and jbig2dec | AGPLv3 components. Feature trimming does not remove these terms. Static renderer sources, retained font notices, CMap notices and profiles are included. |
| Poppler | Source package offers GPL2 or GPL3; GPL3 fits this app. The local build now has a checksum-locked source, recorded compiler/configuration and binary hashes. |
| Qt | Shipped target SPDX records retain LGPLv3/GPL alternatives. Exact module sources, original third-party attributions and full license texts are supplied. |
| QuaZip/MiniZip and other inherited code | Original LGPL/static-linking-exception, zlib-style and individual notices remain; see `native/license-components.json`. |
| Other shared libraries | Exact installed recipes, source archives, patches and full notices, including notices referenced from other license files. Package metadata and target-level evidence remain distinct. |
| Dictionaries/thesauri | All 92 spelling dictionaries and four thesauri are retained, each bound to the exact decompressed payload hashes and original notices or the documented project contribution license. |
| Artwork, symbols, templates and documentation | Original authors, file-level declarations and modifications are recorded in `native/asset-provenance.json`; original license texts are included. |
| Qt translations | Current QtBase catalogs replace unused opaque Qt4 binaries in the packaged app. BSD-3-Clause Qt translation sources include editable TS files. App translations remain unchanged. |

Dictionary provenance is in `native/dictionary-provenance.json`: 78 spelling
pairs match historical wooorm snapshots, six spelling pairs and four thesauri
match LibreOffice, and Czech matches its original contributor ZIP. Other
payloads carry explicit declarations. The Uyghur dictionary was contributed
directly to GPLv3 TeXstudio in
[PR 1743](https://github.com/texstudio-org/texstudio/pull/1743); the project license
is its recorded basis. Missing a separate file header does not by itself remove
that license. No conflicting third-party declaration was found. This follows
[GitHub's contribution-under-repository-license terms](https://docs.github.com/en/site-policy/github-terms/github-terms-of-service#6-contributions-under-repository-license).
Mongolian's explicit LPPL declaration is preserved instead of substituting a
nearby upstream version that does not match its bytes.

Crystal, Oxygen and Colibre credits and their LGPL, CC-BY-SA and CC0 declarations
are retained. Font Awesome Free icons are credited to Fonticons under CC-BY 4.0;
source fonts are OFL1.1 and code is MIT. The coffee cup uses the TeXpresso v3
repository's MIT license, with the removed lettering/background and composition
recorded. A different logo's CC0 declaration is not used as its basis. Generated
manual assets retain their Sphinx, Furo, jQuery, Underscore and other notices.

Qt Virtual Keyboard, its plugin/frameworks and unneeded QtQuick are omitted.
They provide an on-screen touch keyboard that this desktop editor does not use.
The automatically deployed Qt PDF image plugin and QtPdf are also omitted:
TeXstudio's normal PDF viewer uses Poppler, while the live renderer supplies PNG.
Deployment follows the dependency graph and keeps modules genuinely required by
remaining binaries; a real new Virtual Keyboard dependency fails packaging.
QtQml stays for the editor's JavaScript macros. Native Cocoa input methods stay.
See [Qt licensing](https://doc.qt.io/qt-6/licensing.html) and
[Virtual Keyboard's terms](https://doc.qt.io/qt-6/qtvirtualkeyboard-index.html#licenses-and-attributions).

No TeX Live distribution is bundled. Users supply it separately; embedded
renderer fonts and other data actually shipped in our binaries remain inventoried.

## Reproduce the inventory

Deployment collects notices under `Contents/Resources/licenses/` before signing.
Engine notices remain under `Contents/Resources/texpresso/notices/`. For a separate
read-only post-sign report, choose a new output directory:

```sh
python3 native/scripts/license_inventory.py \
  --app '.native-sources/app-updates/current/TeXstudio Espresso.app' \
  --output .native-sources/license-inventory \
  --qt-prefix /opt/homebrew/opt/qt --cellar /opt/homebrew/Cellar \
  --library-path "$PWD/.native-sources/dependencies/poppler/lib"
```

Keep only the latest report and useful compact results. Its `inventory.json`
records code UUIDs/hashes/origins, source package versions/checksums, every
ordinary resource, dictionary archive member and QRC input, static renderer
evidence and publication work. `notices/`, `qt-sbom/` and `assets.json` retain the
original declarations; `index.html` provides offline navigation. All local links
are checked before sealing the candidate.

Origins match installed binaries by Mach-O UUID, not merely filename. Poppler's
`share/texstudio-build-provenance.json` also binds source/configuration and actual
compiler evidence to library hashes. The embedded inventory describes the stage
**before final signing**; signing changes signature bytes and engine-manifest
hashes. The independent report records final hashes. Inventories omit their own
contents and signature resources to avoid recursive hashing.

Qt module attribution collections are a source-level superset, while per-target
`binaries[].qt` records identify shipped modules/plugins. Original license
expressions are retained; an `OR` expression does not require choosing every
alternative. Notice collection checks package version, source checksum and every
notice digest; dictionary checks also fail if member names or payload bytes drift.

The compact renderer retains LCMS2, 14 URW CFF fonts (OFL1.1) and
DroidSansFallbackFull (Apache2). Their actual font bytes were matched in the
unstripped frontend. `renderer_evidence.py` repeats this check for fresh engine
builds and rejects unknown static third-party object members. It compares the
pinned source/profile/font set rather than demanding architecture-identical
archive bytes. The existing local engine also matches the earlier reviewed
archive hashes in `native/license-static-components.json`. Changed renderer
profiles or retained resources require a refreshed review.

## Matching source and local modification

The macOS workflow builds Poppler through `native/scripts/build_poppler.py`,
collects exact dependency sources/patches and regenerates source-derived notices
before signing. It uploads the project source ZIP beside the binary, and the
separate dependency files as another artifact. Source preparation still verifies
every input; splitting delivery does not silently drop dependencies.
The project ZIP includes editor/engine sources, exact dependency URLs/checksums,
patches, recipes, build configuration and notices. Dependency archive names use
full SHA256 values and retain their original formats. Existing files are verified
and reused rather than copied into each version's project ZIP. The index keeps
sources referenced by earlier releases; do not prune them while those binaries
remain distributed. No publishing or CI run is performed by local preparation.

To prepare a source archive for an already inventoried app:

```sh
python3 native/scripts/source_release.py --plan .native-sources/source-plan \
  --inventory '.native-sources/app-updates/current/TeXstudio Espresso.app/Contents/Resources/licenses/inventory.json' \
  --build-dir build-cursor-app
python3 native/scripts/source_release.py --assemble .native-sources/source-plan \
  --cache .native-sources/distribution-sources \
  --output .native-sources/releases/texstudio-espresso-macos-arm64-sources.zip \
  --dependencies .native-sources/releases/dependency-sources --fetch
```

Use new plan/project ZIP paths; the dependency directory is intentionally
reusable. `--fetch` explicitly permits HTTPS retrieval; omit it for cached offline
assembly. Source plans retain installed build recipes and
patches, editor compiler flags/CMake cache and the app's complete notice evidence.
Source notices can be regenerated from verified inputs using
`collect_qt_notices.py` and `collect_library_notices.py`, as in the workflow.

Restore just the editor and engine offline with Python 3.12+ and Git:

```sh
python3 texstudio-espresso-macos-arm64-sources.zip --output ./espresso-source
```

For native engine build inputs, also supply the separate source directory:

```sh
python3 texstudio-espresso-macos-arm64-sources.zip --output ./espresso-source-full \
  --dependencies ./dependency-sources
```

Only MuPDF/LCMS2/zlib-ng snapshots are needed by this restore step. Missing files
fail before extraction; add `--fetch` to explicitly retrieve those snapshots from
the manifest's HTTPS URLs and verify their SHA256. Qt, Poppler and other sources
remain individually accessible from the index/manifest for their own builds.
No network access occurs by default.

The helper verifies used input checksums and initializes editable local Git
snapshots. Original IDs remain in
`SOURCE-ORIGIN.json`; only native lock references change to the new local snapshot
IDs when native dependencies are restored. No program implementation is changed.
Commit modifications before invoking the pinned builder and update dependency lock references when modifying those
snapshots. Original recipes and patches stay in the project ZIP; complete library
sources are separate. Unused legacy Qt4 binary catalogs are excluded from the
export, while current editable Qt TS sources are available in the indexed
Qt translations archive. This does not delete checkout files.

Use `native/LOCAL_TEST_APP.md` and `native/MACOS_RELEASE.md` from the restored
source for build/packaging instructions. Local modified apps use ad-hoc signing;
no distributor signing key or notarization is required. Apple SDK/system libraries
and unmodified external build tools are not included. This supports inspection,
modification and rebuilding without promising bit-identical output across SDKs
and compilers. The project ZIP is about 130 MiB, dominated by inherited dictionary
sources. Dependency sources total about 385 MiB and can be reused unchanged.
OpenSSL's Apache-2.0 license itself does not mandate source delivery; we retain
its source in the dependency set for reproducibility and any combined-work
requirements, rather than implying an OpenSSL-specific source obligation.
See its [license](https://github.com/openssl/openssl/blob/openssl-4.1.0/LICENSE.txt).

## Verification scope

The packaged candidate passed signature/dependency and live preview rendering,
diagnostics, scrolling and link checks. All 55 native tooling tests passed, including split-source restoration and
missing/corrupt dependency checks.
An offscreen full-editor smoke test made 128 edits across a 30-page fixture,
checked five independent PDF/SyncTeX probes and left no test processes. A separate
probe exercised the bundled Poppler PDF text/raster backend and a current Qt
translation. The offline source restore passed native pinned-source checks.
These are packaging and functional checks, not a new long-session performance
measurement or a claim that all dependencies were rebuilt from the restored ZIP.
