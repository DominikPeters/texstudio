#!/usr/bin/env python3
"""Assemble a self-contained TeXstudio app before importing signing credentials."""
import argparse
import json
import os
import platform
import plistlib
import shutil
import subprocess
from pathlib import Path
from common import run, sha256
from notarize import MACHO


def macho_files(app):
    root = app.resolve()
    found = set()
    for file in app.rglob('*'):
        if file.is_symlink():
            if not file.resolve().is_relative_to(root):
                raise ValueError(f'Symlink escapes application: {file.relative_to(app)}')
            continue
        if file.is_file():
            with file.open('rb') as stream:
                if stream.read(4) in MACHO:
                    found.add(file)
    return sorted(found, key=lambda p: (-len(p.parts), str(p)))


def verify_dependencies(app):
    """Reject host-only dependencies and unresolved rpaths before signing."""
    root = app.resolve()
    main = app / 'Contents/MacOS/texstudio'
    for file in macho_files(app):
        lines = run(['otool', '-l', file], capture=True).splitlines()
        rpaths = [lines[i + 2].strip().removeprefix('path ').split(' (offset')[0]
                  for i, line in enumerate(lines) if line.strip() == 'cmd LC_RPATH']
        main_lines = run(['otool', '-l', main], capture=True).splitlines()
        rpaths += [main_lines[i + 2].strip().removeprefix('path ').split(' (offset')[0]
                   for i, line in enumerate(main_lines) if line.strip() == 'cmd LC_RPATH']
        install_id = run(['otool', '-D', file], capture=True).splitlines()[1:]
        for dependency in run(['otool', '-L', file], capture=True).splitlines()[1:]:
            name = dependency.strip().split(' (compatibility')[0]
            if name in install_id or name.startswith(('/System/Library/', '/usr/lib/')):
                continue
            def expand(value):
                return Path(value.replace('@loader_path', str(file.parent)).replace('@executable_path', str(main.parent)))
            if name.startswith('@rpath/'):
                candidates = [expand(path) / name.removeprefix('@rpath/') for path in rpaths]
                # A dylib's install ID is not a dependency on another image.
                candidates.append(file.parent / name.removeprefix('@rpath/'))
            elif name.startswith(('@loader_path/', '@executable_path/')):
                candidates = [expand(name)]
            else:
                raise ValueError(f'External dependency in {file.relative_to(app)}: {name}')
            if not any(path.is_file() and path.resolve().is_relative_to(root) for path in candidates):
                raise ValueError(f'Unresolved bundled dependency in {file.relative_to(app)}: {name}')


def refresh_engine_manifest(app, signing=None):
    runtime = app / 'Contents/Helpers/texpresso'
    path = app / 'Contents/Resources/texpresso/build-manifest.json'
    manifest = json.loads(path.read_text())
    for entry in manifest['files']:
        file = runtime / entry['path']
        if not file.resolve().is_relative_to(runtime.resolve()):
            raise ValueError('Engine manifest escapes its runtime')
        entry.update(sha256=sha256(file), bytes=file.stat().st_size)
    if signing:
        manifest['signing'] = signing
    manifest['installedBytes'] = sum(p.stat().st_size for p in runtime.rglob('*') if p.is_file() and p != path)
    path.write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--macdeployqt', required=True)
    parser.add_argument('--library-path', type=Path, action='append', default=[])
    parser.add_argument('--poppler-data', type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('macOS deployment requires macOS')
    app = args.output.resolve()
    if app.exists():
        parser.error('Output already exists')
    run(['ditto', args.app.resolve(), app])
    plist_path = app / 'Contents/Info.plist'
    with plist_path.open('rb') as stream:
        plist = plistlib.load(stream)
    plist['CFBundleIdentifier'] = 'org.texstudio.texpresso'
    plist.setdefault('CFBundleVersion', plist['CFBundleShortVersionString'])
    with plist_path.open('wb') as stream:
        plistlib.dump(plist, stream)
    runtime = app / 'Contents/Helpers/texpresso'
    metadata = app / 'Contents/Resources/texpresso'
    metadata.mkdir(parents=True)
    # Helpers is a code location: Apple rejects ordinary notices/JSON as code.
    # Leave the native bin/lib layout and its relative dylib paths intact.
    shutil.move(runtime / 'notices', metadata / 'notices')
    shutil.move(runtime / 'build-manifest.json', metadata / 'build-manifest.json')
    data = app / 'Contents/share/poppler'
    run(['ditto', args.poppler_data, data])
    plugins = Path(run([str(Path(args.macdeployqt).parent / 'qtpaths'), '--query', 'QT_INSTALL_PLUGINS'], capture=True))
    offscreen = app / 'Contents/PlugIns/platforms/libqoffscreen.dylib'
    offscreen.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(plugins / 'platforms/libqoffscreen.dylib', offscreen)
    (app / 'Contents/Resources/qt.conf').write_text('[Paths]\nPrefix = .\nPlugins = PlugIns,../PlugIns\nLibraries = Frameworks,../Frameworks\n')
    run([args.macdeployqt, app, '-verbose=2', '-no-codesign',
         '-executable=' + str(offscreen),
         *['-libpath=' + str(path.resolve()) for path in args.library_path]])
    # A release may not fall back to the runner's Homebrew/temporary prefixes.
    for file in macho_files(app):
        lines = run(['otool', '-l', file], capture=True).splitlines()
        paths = [lines[i + 2].strip().removeprefix('path ').split(' (offset')[0]
                 for i, line in enumerate(lines) if line.strip() == 'cmd LC_RPATH']
        for path in paths:
            if path.startswith('/') and not path.startswith(('/usr/lib/', '/System/Library/')):
                run(['install_name_tool', '-delete_rpath', path, file])
    main = app / 'Contents/MacOS/texstudio'
    if '@executable_path/../Frameworks' not in run(['otool', '-l', main], capture=True):
        run(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', main])
    verify_dependencies(app)
    refresh_engine_manifest(app)
    revision = run(['git', 'rev-parse', 'HEAD'], capture=True)
    runtime = json.loads((metadata / 'build-manifest.json').read_text())
    (app / 'Contents/Resources/texpresso-release.json').write_text(json.dumps({
        'sourceRevision': revision, 'sourceBranch': 'texpresso',
        'engineRevision': runtime['engineRevision'], 'architecture': platform.machine(),
        'requires': 'User-installed TeX Live',
    }, indent=2) + '\n')
    # Finish a local ad-hoc seal for deployment checks. CI replaces it with
    # timestamped Developer ID signatures and hardened runtime afterwards.
    from notarize_app import sign_app
    sign_app(app, '-', None, adhoc=True)
    # Exercise dyld without relying on developer machine library search paths.
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'QT_'))}
    environment['QT_QPA_PLATFORM'] = 'offscreen'
    subprocess.run([str(app / 'Contents/MacOS/texstudio'), '--version'], env=environment, check=True, timeout=30)
    subprocess.run([str(app / 'Contents/Helpers/texpresso/bin/texpresso'), '-capabilities'], env=environment, check=True, timeout=30)


if __name__ == '__main__':
    main()
