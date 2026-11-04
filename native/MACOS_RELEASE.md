# Signed macOS builds

Run **Build signed TeXpresso for macOS** in the repository's Actions tab.
The manual input selects Apple Silicon (`arm64`, the default), Intel (`x86_64`),
or both. The workflow always checks out `texpresso`; it does not build the
repository's default branch or publish a GitHub Release.

GitHub requires a `workflow_dispatch` launcher on the default branch for the
**Run workflow** button. `.github/workflows/texpresso-release.yml` is that small
launcher. It calls `.github/workflows/texpresso-macos.yml@texpresso`, where build
and signing changes are maintained alongside the application source.

Each architecture builds the pinned engine, locked MuPDF/ICU profile, Poppler
with Qt support, and TeXstudio. The application includes Qt frameworks/plugins,
PDF libraries/data, and the native engine. TeX Live remains a user installation.
The runner installs BasicTeX only for tests; it is not copied into the app.
Its self-update and package installation use one CTAN mirror to avoid version
mismatches between asynchronously updated mirrors.
Poppler and its encoding data are downloaded separately from their official
releases and checked against `native/macos-dependencies.lock.json`.

Signing uses the existing repository secrets:

- `APPLE_CERTIFICATE`: base64 PKCS#12 Developer ID Application certificate,
  private key and certificate chain.
- `APPLE_CERTIFICATE_PASSWORD` and `APPLE_SIGNING_IDENTITY`.
- `KEYCHAIN_PASSWORD`: password for the temporary CI keychain.
- `APPLE_API_KEY`, `APPLE_API_ISSUER`, and `APPLE_API_PRIVATE_KEY`: notarization
  API credentials, with the private key as PEM or base64 PEM.

Credentials are available only to the signing step, imported into a temporary
keychain and removed afterward, including after failures. Logs never print
credential-bearing command arguments. Nested libraries, frameworks, plugins,
and helpers are signed before the outer app. Hardened runtime and secure
timestamps are required. The JIT entitlement applies only to the editor, which
uses QJSEngine; native TeX helpers do not receive it.

The workflow submits a temporary ZIP to Apple and requires explicit `Accepted`
status. It then staples and validates the ticket, checks Gatekeeper and strict
code signatures, exercises the signed engine, and uploads the final ZIP plus
SHA-256 checksum and matching project-source ZIP. Reusable dependency sources are
uploaded as a separate artifact. Source preparation runs before signing and
fails if a required archive or patch is missing. Failed or timed-out notarization does not publish an app.
Diagnostics are uploaded separately. Both artifacts expire after 14 days.
Ticket stapling and validation retry temporary ticket-service failures for a
bounded time, recording verbose output from each attempt in the diagnostics.

The ZIP preserves executable modes, Qt framework symlinks, and the stapled ticket.
`Contents/Resources/texpresso-release.json` identifies the source and engine
commits. Engine notices and its refreshed binary manifest live under
`Contents/Resources/texpresso`; only executable code remains in
`Contents/Helpers/texpresso`, as required by Apple's nested-code conventions.
Its `bin/` and `lib/` layout stays intact, so engine discovery is unchanged.

For local deployment checks, without importing any signing credentials:

```sh
python3 native/scripts/deploy_macos.py --app build/texstudio.app \
  --output /tmp/texstudio-deployed/texstudio.app \
  --macdeployqt "$(brew --prefix qt)/bin/macdeployqt" \
  --library-path /path/to/poppler/lib --library-path "$(brew --prefix)/lib" \
  --poppler-data /path/to/poppler/share/poppler
python3 -m unittest discover -s native/tests
```

Deployment verifies every non-system Mach-O dependency, removes build-machine
rpaths, checks internal symlinks, and tests app/engine startup. The temporary
local seal is ad-hoc; the CI signer replaces it with Developer ID signatures.
The platform plugins include `offscreen` for CI checks as well as Cocoa for users.

## Corresponding source and notices

`Contents/Resources/licenses/index.html` is linked from About. It includes the
app/engine source revisions, original license texts and third-party attribution,
per-payload dictionary provenance, and artwork/template notices. Public binary
distributions must provide clear access to matching project and dependency
sources from the binary download page. `*-sources.zip` contains the editor,
recursive engine source, dependency manifest, patches, notices and build recipes.
`dependency-sources/` contains original upstream sources under stable SHA256
filenames, plus a browsable index. Reuse those files across releases and
architectures; keep access to versions referenced by binaries still offered.
When publishing, upload the project ZIP/checksum and link the required dependency
files or equivalent exact-version sources. The two CI artifacts together hold
the complete set; the project ZIP is intentionally smaller and not self-contained.
It does not require the engine revision to be separately published as a Git branch.
See [licensing and source preparation](../docs/distribution-licensing.md).
