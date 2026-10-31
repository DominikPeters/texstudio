# Local testing and app updates

- Routine verification must run in the background. Use native unit/protocol
  tests, Qt tests with `QT_QPA_PLATFORM=offscreen`, and `SDL_VIDEODRIVER=dummy`.
- Do not use computer control, Finder, `open`, macOS file dialogs, or the user's
  running editor to load fixtures or inspect ordinary test results.
- Run the real test executable directly with a disposable configuration and
  disposable projects. Do not use the interactive app launcher/profile.
- Use `native/scripts/verify_local_app.py` for packaged macOS app verification.
  For full-editor tests use `native/scripts/ui_soak.py` with its offscreen default.
- Keep build, verification, installation, and interactive launch separate.
  Stage new app builds inside the ignored repository build area; do not quit,
  replace, restart, or reopen documents in the running app for routine testing.
  Install/restart only when the user explicitly requests it, or when the app is
  closed and the user has explicitly requested installation.
- Visible-window checks require an explicit user request. Use them only for
  behavior that background tests cannot establish, such as physical trackpad
  gestures, macOS focus/cursors, Dock icons, or compositor presentation.
- See `native/TESTING.md` for the verification tiers and limitations.
