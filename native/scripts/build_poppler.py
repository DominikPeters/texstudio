#!/usr/bin/env python3
"""Build locked Poppler and record exact source/configuration/binary provenance."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

from common import ROOT, run, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--fetch', action='store_true')
    args = parser.parse_args()
    lock = json.loads((ROOT / 'native/macos-dependencies.lock.json').read_text())['poppler']
    args.cache.mkdir(parents=True, exist_ok=True)
    archive = args.cache / lock['url'].rsplit('/', 1)[-1]
    if not archive.exists():
        if not args.fetch:
            parser.error('Missing source archive; supply it in --cache or use --fetch')
        temporary = archive.with_suffix('.partial')
        urllib.request.urlretrieve(lock['url'], temporary)
        if sha256(temporary) != lock['sha256']:
            temporary.unlink()
            raise ValueError('Poppler source checksum mismatch')
        temporary.rename(archive)
    if sha256(archive) != lock['sha256']:
        raise ValueError('Cached Poppler source checksum mismatch')
    work = args.work.resolve()
    prefix = args.prefix.resolve()
    if prefix.exists() or work.exists():
        parser.error('Use new work and prefix directories; do not overwrite active dependencies')
    source = work / 'source'
    build = work / 'build'
    source.mkdir(parents=True)
    with tarfile.open(archive) as tar:
        # Locked archive has one top directory. Reject links/traversal through
        # Python's data filter and retain the original source paths for notices.
        tar.extractall(source, filter='data')
    source = next(p for p in source.iterdir() if p.is_dir())
    options = ['-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
               '-DCMAKE_PREFIX_PATH=' + str(args.qt_prefix.resolve()),
               '-DENABLE_QT6=ON', '-DENABLE_QT5=OFF', '-DENABLE_CPP=ON',
               '-DENABLE_UNSTABLE_API_ABI_HEADERS=ON', '-DENABLE_GLIB=OFF',
               '-DENABLE_GOBJECT_INTROSPECTION=OFF', '-DENABLE_BOOST=OFF',
               '-DENABLE_LIBCURL=OFF', '-DENABLE_NSS3=OFF', '-DENABLE_GPGME=OFF',
               '-DENABLE_PGP_SIGNATURES=OFF', '-DENABLE_UTILS=OFF',
               '-DBUILD_GTK_TESTS=OFF', '-DBUILD_QT5_TESTS=OFF', '-DBUILD_QT6_TESTS=OFF',
               '-DBUILD_CPP_TESTS=OFF', '-DBUILD_MANUAL_TESTS=OFF']
    run(['cmake', '-S', source, '-B', build, '-G', 'Ninja', *options])
    run(['cmake', '--build', build, '--parallel', str(args.jobs)])
    run(['cmake', '--install', build])
    metadata = prefix / 'share/texstudio-build'
    metadata.mkdir(parents=True)
    for name in ('COPYING', 'COPYING3', 'AUTHORS', 'README.md'):
        if (source / name).is_file():
            shutil.copyfile(source / name, metadata / name)
    shutil.copyfile(build / 'CMakeCache.txt', metadata / 'CMakeCache.txt')
    files = {str(p.relative_to(prefix)): sha256(p) for p in (prefix / 'lib').glob('*.dylib')
             if p.is_file() and not p.is_symlink()}
    (prefix / 'share/texstudio-build-provenance.json').write_text(json.dumps({
        'schema': 1, 'package': 'poppler', 'version': lock['version'],
        'license': 'GPL-2.0-only OR GPL-3.0-only', 'source': lock,
        'configuration': options, 'generator': 'native/scripts/build_poppler.py',
        'cmakeVersion': run(['cmake', '--version'], capture=True).splitlines()[0],
        'compiler': run(['clang', '--version'], capture=True).splitlines()[0],
        'files': files,
    }, indent=2) + '\n')
    print('Poppler prefix with verified build provenance:', prefix)


if __name__ == '__main__':
    main()
