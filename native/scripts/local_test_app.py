#!/usr/bin/env python3
"""Build and package a persistent, isolated macOS development app."""
import argparse
import json
import platform
import plistlib
import shutil
import sys
from pathlib import Path

from common import ROOT, run, sha256
from deploy_macos import verify_dependencies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build', help='Already configured CMake build')
    parser.add_argument('--output', type=Path, default=Path.home() / 'Applications/TeXstudio Espresso.app')
    parser.add_argument('--macdeployqt', default=shutil.which('macdeployqt'))
    parser.add_argument('--library-path', type=Path, action='append', default=[])
    parser.add_argument('--poppler-data', type=Path, required=True, help='Poppler encoding data, copied into the app')
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Local app packaging requires macOS')
    build = args.build_dir.expanduser().resolve()
    output = args.output.expanduser()
    if output.exists() or output.is_symlink():
        parser.error('Output already exists; quit and archive the previous app outside Applications before rebuilding at the same path')
    output = output.resolve()
    if output.suffix != '.app' or args.jobs < 1:
        parser.error('Choose a .app output and a positive --jobs value')
    if not (build / 'CMakeCache.txt').is_file():
        parser.error('Configure the CMake build first; see native/LOCAL_TEST_APP.md')
    if not args.macdeployqt or not Path(args.macdeployqt).is_file():
        parser.error('Supply --macdeployqt or put macdeployqt on PATH')
    if not args.poppler_data.expanduser().is_dir():
        parser.error('--poppler-data must be an existing Poppler data directory')
    icon_dir = ROOT / 'native/icons/alternatives/coffee'
    for name in ('AppIconCoffee.icns', 'Assets.car'):
        if not (icon_dir / name).is_file():
            parser.error('Missing test app icon; run native/scripts/build_app_icon.py --variant coffee')

    run(['cmake', '--build', build, '--target', 'texpresso_bundle', '--parallel', str(args.jobs)], cwd=ROOT)
    output.parent.mkdir(parents=True, exist_ok=True)
    run([sys.executable, ROOT / 'native/scripts/deploy_macos.py', '--app', build / 'texstudio.app',
         '--output', output, '--macdeployqt', args.macdeployqt,
         '--poppler-data', args.poppler_data.expanduser().resolve(),
         *[arg for path in args.library_path for arg in ('--library-path', path.expanduser().resolve())]], cwd=ROOT)

    # The deployment helper already signs all nested code and verifies that
    # every non-system dependency is inside the application. Add the isolated
    # local launcher, then reseal the changed main executable and outer bundle.
    launcher = output / 'Contents/MacOS/texstudio-test'
    launcher.write_text('''#!/bin/sh
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
config_dir="$HOME/Library/Application Support/TeXstudio TeXpresso Test"
mkdir -p "$config_dir" || exit 1
exec "$app_dir/texstudio" --start-always --disable-tests --config "$config_dir" "$@"
''')
    launcher.chmod(0o755)
    for name in ('AppIconCoffee.icns', 'Assets.car'):
        shutil.copy2(icon_dir / name, output / 'Contents/Resources' / name)
    plist_path = output / 'Contents/Info.plist'
    with plist_path.open('rb') as stream:
        plist = plistlib.load(stream)
    plist.update(CFBundleExecutable=launcher.name, CFBundleIdentifier='org.texstudio.texpresso.local-test',
                 CFBundleName='TeXstudio Espresso', CFBundleDisplayName='TeXstudio Espresso',
                 CFBundleIconName='AppIconCoffee', CFBundleIconFile='AppIconCoffee.icns')
    for document_type in plist.get('CFBundleDocumentTypes', []):
        document_type.pop('LSIsAppleDefaultForType', None)
        document_type['LSHandlerRank'] = 'Alternate'
    with plist_path.open('wb') as stream:
        plistlib.dump(plist, stream)
    metadata = output / 'Contents/Resources/texpresso-test-app.json'
    metadata.write_text(json.dumps({
        'sourceRevision': run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, capture=True),
        'trackedSourceDirty': bool(run(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, capture=True)),
        'buildDirectory': str(build), 'buildExecutableSha256': sha256(build / 'texstudio.app/Contents/MacOS/texstudio'),
        'profileDirectory': '~/Library/Application Support/TeXstudio TeXpresso Test',
        'generator': 'native/scripts/local_test_app.py',
        'iconName': 'AppIconCoffee',
    }, indent=2) + '\n')
    run(['codesign', '--force', '--options', '0', '--sign', '-', output / 'Contents/MacOS/texstudio'])
    run(['codesign', '--force', '--options', '0', '--sign', '-', '--entitlements',
         ROOT / 'native/profiles/texstudio.entitlements', output])
    run(['codesign', '--verify', '--deep', '--strict', output])
    verify_dependencies(output)
    print('Test app:', output)
    print('Settings: ~/Library/Application Support/TeXstudio TeXpresso Test')


if __name__ == '__main__':
    main()
