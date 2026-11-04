# Windows with WSL

The Windows editor and both viewers run natively with Qt. Live compilation uses
our pinned Linux TeXpresso bundle through a persistent `wsl.exe` pipe. No WSLg
or network listener is needed. The protocol and renderer remain shared with the
macOS/Linux clients and the VSCode extension.

## Implementation sequence

1. Add a WSL launch/path adapter and Linux process supervisor. Translate only
   protocol path fields; never rewrite document contents, links or PNG bytes.
   Reuse the extension's separation of editor/engine path namespaces and cache
   conversions. Keep the current native launch path on macOS/Linux.
2. Package the Linux runtime alongside the Windows application. Test the native
   Qt client against the actual WSL2 engine on `windows-2025`: capability probe,
   PNG decoding, edits, SyncTeX, pagination, errors/recovery and worker cleanup.
   Exercise spaces/Unicode paths and Windows-mounted and WSL-native projects.
3. Add setup UI: use the default distribution, allow any installed distribution,
   verify actual capabilities and report concrete failures. No distribution-name
   or version whitelist. Full builds/Biber should run in the selected environment
   so package versions and auxiliary files match live compilation.
4. Add TeX Live installation assistance: locate an existing installation first;
   offer small/full selections, download/installed-space estimates, guest and
   host free-space checks, an explicit installation confirmation, progress,
   cancellation and logs. Prefer an official user-local TeX Live installation
   independent of distro package managers. Verify a small compile and resume.
   The installation survives application upgrades. Do not install silently.
5. Extend regression coverage as reports arrive. Ubuntu is a CI baseline, not a
   runtime restriction. Hardware gestures and representative latency remain
   separate from runner-based functional validation.

The engine uses Linux process groups/checkpoint forks. A small app-owned Linux
supervisor must release only its own compiler group on input-pipe closure,
frontend exit or shutdown; a Windows process ID is not a Linux process-group ID.
The supervisor is separate from the shared engine, so engine code need not change.

The Windows package includes Linux libraries but uses the distribution's ELF
loader/glibc. Failures to load them must remain visible, not be masked by a distro
whitelist or silent WSL1 fallback. The app should verify the selected distribution
runs under WSL2. Windows TeX Live alone does not supply the Linux TeX environment.

Keep the engine/TeX Live in the Linux environment. Initial direct access to
Windows projects is supported through WSL mounts; benchmark before introducing
project mirroring. Native WSL files are opened through `\\wsl.localhost\...`.
Path conversion results are cached, so typing never launches `wslpath` per edit.

CI artifacts are development builds, unsigned. They are not automatically
published as releases or installed on the developer's computer. Packaging/source
inventory for a public Windows release is a separate validation step.
