#!/usr/bin/env python3
"""Sign a deployed TeXstudio app, notarize it, staple its ticket, and archive it."""
import argparse
import os
import platform
import re
import tempfile
from pathlib import Path
from common import sha256
from deploy_macos import macho_files, refresh_engine_manifest, verify_dependencies
from notarize import command, imported_identity, notarize, signing_credentials


def sign_app(app, identity, keychain, *, adhoc=False):
    files = macho_files(app)
    if app / 'Contents/MacOS/texstudio' not in files:
        raise ValueError('Missing TeXstudio executable')
    if not adhoc:
        identity = imported_identity(identity, keychain)
    def sign(target, entitlements=None):
        args = ['codesign', '--force', '--options', '0' if adhoc else 'runtime', '--sign', identity]
        if not adhoc:
            args += ['--timestamp', '--keychain', keychain]
        if entitlements:
            args += ['--entitlements', entitlements]
        command([*args, target])
    # Resolve symlinks rather than signing Qt's framework aliases twice.
    for file in files:
        if file != app / 'Contents/MacOS/texstudio':
            sign(file)
    containers = sorted([p for p in app.rglob('*') if p.is_dir() and not p.is_symlink()
                         and p.suffix in ('.framework', '.app', '.xpc', '.bundle', '.plugin')],
                        key=lambda p: (-len(p.parts), str(p)))
    for directory in containers:
        sign(directory)
    details = command(['codesign', '--display', '--verbose=4', app / 'Contents/Helpers/texpresso/bin/texpresso']).stderr
    team = re.search(r'^TeamIdentifier=(\S+)$', details, re.M)
    if not adhoc and (not team or team[1] == 'not set' or 'Authority=Developer ID Application:' not in details):
        raise ValueError('Expected Developer ID signatures on nested engine code')
    refresh_engine_manifest(app, {'kind': 'ad-hoc' if adhoc else 'developer-id',
                                 'teamId': team[1] if team else None,
                                 'hardenedRuntime': not adhoc, 'secureTimestamp': not adhoc})
    # QJSEngine uses JIT in the editor. Native TeX helpers do not need this.
    entitlements = Path(__file__).resolve().parents[1] / 'profiles/texstudio.entitlements'
    sign(app, entitlements)
    command(['codesign', '--verify', '--deep', '--strict', '--verbose=2', app])
    if not adhoc:
        details = command(['codesign', '--display', '--verbose=4', app]).stderr
        if not team or f'TeamIdentifier={team[1]}' not in details or 'Timestamp=' not in details or not re.search(r'^CodeDirectory .*flags=.*\bruntime\b', details, re.M):
            raise ValueError('Outer application must have the same timestamped Developer ID signature and hardened runtime')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Final ZIP with a stapled app')
    parser.add_argument('--reports', type=Path, required=True)
    parser.add_argument('--timeout', default='20m')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Signing and notarization require macOS')
    app = args.app.resolve()
    if args.output.exists() or args.output.suffix != '.zip':
        parser.error('Choose a new .zip output path')
    # Inspect all code and dependencies before importing credentials.
    verify_dependencies(app)
    if not (app / 'Contents/Resources/texpresso/build-manifest.json').is_file():
        parser.error('The application must include its engine runtime')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.reports.mkdir(parents=True, exist_ok=True)
    with signing_credentials() as (identity, keychain):
        sign_app(app, identity, keychain)
        with tempfile.TemporaryDirectory(prefix='texstudio-notary-upload-', dir=os.environ.get('RUNNER_TEMP')) as folder:
            upload = Path(folder) / 'submission.zip'
            command(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, upload])
            notarize(upload, args.reports, keychain, args.timeout)
            (args.reports / 'submitted-archive.sha256').write_text(sha256(upload) + '\n')
        command(['xcrun', 'stapler', 'staple', app])
        command(['xcrun', 'stapler', 'validate', app])
        command(['codesign', '--verify', '--deep', '--strict', '--verbose=2', app])
        command(['spctl', '--assess', '--type', 'execute', '--verbose=2', app])
        # Archive only after stapling. ditto preserves framework symlinks, modes,
        # resource forks and the ticket; upload-artifact wraps this intact ZIP.
        command(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, args.output.resolve()])
        args.output.with_suffix('.zip.sha256').write_text(f'{sha256(args.output)}  {args.output.name}\n')
    print('Signed, notarized and stapled:', args.output)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, ValueError) as error:
        raise SystemExit(str(error)) from None
