# Windows with WSL

The editor and both viewers run natively with Qt. Live compilation uses the
bundled Linux TeXpresso engine through a persistent `wsl.exe` pipe. WSLg and a
network listener are not needed. Windows TeX installations do not supply packages
to the Linux compiler: install TeX Live or MiKTeX inside the selected distribution.

## Setup and builds

Open **TeX Environment…** in the live-preview toolbar. Use the default WSL
distribution or choose any installed distribution. There is no distribution-name
or release whitelist. WSL2 is required; startup checks its actual version and
reports loader/tool failures. Ubuntu 24.04 is the initial CI environment.

Choose Automatic, TeX Live or MiKTeX. Automatic prefers a separate TeX Live
installation when both are present; MiKTeX's compatibility `kpsewhich` is not
mistaken for TeX Live. MiKTeX requires an engine advertising the `miktex` provider;
older bundles report an explicit unsupported-provider error. Its package downloads
are disabled by default and require a separate opt-in. The provider is implemented
in the shared engine, not as a TeXstudio-specific compiler.

The wizard can install an official user-local TeX Live into
`~/.local/share/texlive/espresso`. Choose full (roughly 10 GB; reserve at least
12 GB) or small, review available Linux and Windows disk space, then explicitly
click **Install TeX Live**. Curl, Perl and tar are required. Installation has
progress/logs and cancellation, does not change system PATH, and does not replace
another TeX installation. An interrupted installation is retained for repair.
The smaller scheme includes Biber, biblatex, latexmk and recommended LaTeX
packages; other missing packages need installing separately. MiKTeX itself is
installed using [its official instructions](https://miktex.org/Download).

Enable **Use this distribution for build commands and bibliography tools** to run
standard TeXstudio recipes in the same Linux environment. The existing build
manager still handles reruns, bibliography sequencing, logs, errors and refreshing
the live preview after external builds. Unrelated custom commands keep their
configured behavior. The setting defaults on when no Windows TeX installation is
found. Native Windows TeX users can retain their existing build commands.

Windows projects are accessed through WSL mounts; projects in Linux are opened
through `\\wsl.localhost\Distribution\...`. Path mappings are prepared once at
session startup. Typing does not launch `wslpath` or rewrite source contents.
Full-build source arguments become relative Linux paths so saved-PDF SyncTeX
continues to identify project files. Project mirroring has not been introduced.

The app-owned Linux supervisor releases its own compiler process group on pipe
closure, frontend exit or shutdown. The engine also watches its Linux parent.
No Windows process ID is used as a Linux process-group ID.

## Building and validation

`.github/workflows/texpresso-windows.yml` builds the pinned engine on Ubuntu 22.04
with the compact MuPDF profile and zlib-ng. It then boots actual WSL2 on
`windows-2025`, installs a test TeX environment, builds the native Qt app, and
runs the offscreen Qt integration executable against the Linux engine.

The suite covers binary PNG transport, source overlays/range edits, Unicode and
space-containing paths, inverse SyncTeX, links, invalid-source recovery,
pause/restart/shutdown, and a full XeLaTeX/Biber/XeLaTeX build followed by live
preview refresh. Pure tests cover protocol path fields, command arguments,
provider discovery/capability checks and WSL version decoding. Windows runs the
preview scenario with both Windows-hosted and Linux-native project paths.
No interactive editor or file dialog is needed.

The portable development artifact is named
**TeXstudio-Espresso-Windows-WSL-development**. CI runs its packaged executable's
version check and checks for surviving compiler workers after the integration
suite. Artifacts are unsigned, expire after seven days, and are not automatically
published as releases or installed on the developer's computer.

Initial runner validation is in progress. Physical trackpad gestures, compositor
behavior, representative interactive latency and the public-release Windows
license/source inventory remain separate checks. The engine bundles Linux
libraries but relies on the distribution's ELF loader/glibc; older or non-glibc
distributions may report actual loader errors. This is a runtime dependency,
not a distro whitelist.

For local debugging, `TEXPRESSO_WSL_DISTRIBUTION` overrides the configured distro
and `TEXPRESSO_PACKAGE_PROVIDER` overrides the provider (`auto`, `texlive`,
`miktex`). These are development overrides; normal users use the setup UI.
