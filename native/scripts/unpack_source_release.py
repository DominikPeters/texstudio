#!/usr/bin/env python3
"""Restore project sources, optionally with separately supplied dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile


def command(directory, *args):
    result = subprocess.run(['git', '-C', str(directory), *args], check=True,
                            capture_output=True, text=True)
    return result.stdout.strip()


def snapshot(directory):
    # Plumbing avoids commit hooks or the distributor's signing credentials.
    command(directory, 'init', '--quiet')
    command(directory, 'add', '--force', '--all')
    tree = command(directory, 'write-tree')
    revision = command(directory, '-c', 'user.name=Source Snapshot', '-c', 'user.email=source@localhost',
                       '-c', 'commit.gpgSign=false', 'commit-tree', tree, '-m',
                       'Imported corresponding-source snapshot; upstream revisions recorded in SOURCE-ORIGIN.json')
    command(directory, 'update-ref', 'refs/heads/source-snapshot', revision)
    command(directory, 'symbolic-ref', 'HEAD', 'refs/heads/source-snapshot')
    return revision


def extract(archive, member, destination, strip_root=False):
    destination.mkdir(parents=True, exist_ok=True)
    with (archive.open(member) if archive else Path(member).open('rb')) as stream, tarfile.open(fileobj=stream, mode='r:*') as tar:
        if not strip_root:
            tar.extractall(destination, filter='data')
            return
        with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
            temporary = Path(temporary)
            tar.extractall(temporary, filter='data')
            roots = list(temporary.iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError('Expected one root in upstream Git source archive')
            for file in roots[0].iterdir():
                target = destination / file.name
                if target.exists():
                    raise ValueError(f'Source extraction would overwrite {target}')
                shutil.move(file, target)


def dependency_source(record, directory, fetch=False):
    name = record['sourceFile']
    if Path(name).name != name or name in ('.', '..'):
        raise ValueError('Unsafe dependency source filename')
    file = directory / name
    if not file.exists():
        if not fetch:
            raise ValueError(f'Missing dependency source: {record["name"]}; supply {file} or explicitly use --fetch')
        if not record['url'].startswith('https://'):
            raise ValueError('Dependency source requires HTTPS')
        directory.mkdir(parents=True, exist_ok=True)
        temporary = file.with_name(file.name + '.partial')
        try:
            deadline = time.monotonic() + 900
            with urllib.request.urlopen(record['url'], timeout=60) as response, temporary.open('wb') as stream:
                while chunk := response.read(1024 * 1024):
                    if time.monotonic() > deadline:
                        raise TimeoutError(f'Source download exceeded 15 minutes: {record["name"]}')
                    stream.write(chunk)
            with temporary.open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != record['sha256']:
                    raise ValueError(f'Source input checksum mismatch: {record["name"]}')
            temporary.replace(file)
        finally:
            temporary.unlink(missing_ok=True)
    with file.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != record['sha256']:
            raise ValueError(f'Source input checksum mismatch: {record["name"]}')
    return file


def unpack(source_zip, output, dependencies=None, fetch=False):
    if output.exists():
        raise ValueError('Source output already exists')
    with zipfile.ZipFile(source_zip) as archive:
        manifest = json.loads(archive.read('SOURCE-MANIFEST.json'))
        with_dependencies = dependencies is not None or fetch or manifest.get('schema', 1) < 2
        dependencies = dependencies or source_zip.parent / 'dependency-sources'
        external = {}
        # Verify embedded patches and requested snapshots before extracting.
        for record in manifest['inputs']:
            if record.get('archiveMember'):
                with archive.open(record['archiveMember']) as stream:
                    if hashlib.file_digest(stream, 'sha256').hexdigest() != record['sha256']:
                        raise ValueError(f'Source input checksum mismatch: {record["name"]}')
            elif record.get('extractTo') and with_dependencies:
                external[record['name']] = dependency_source(record, dependencies, fetch)
        output.mkdir(parents=True)
        editor = output / 'texstudio'
        extract(archive, 'project/texstudio.tar.gz', editor)
        engine = editor / 'native/texpresso'
        extract(archive, 'project/texpresso.tar.gz', engine)
        for record in manifest['inputs']:
            if not record.get('extractTo') or not with_dependencies:
                continue
            relative = Path(record['extractTo'])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Unsafe Git snapshot destination')
            if record.get('archiveMember'):
                extract(archive, record['archiveMember'], output / relative, strip_root=True)
            else:
                extract(None, external[record['name']], output / relative, strip_root=True)
        for name in archive.namelist():
            if name.startswith(('recipes/', 'patches/')) and not name.endswith('/'):
                relative = Path(name)
                if '..' in relative.parts:
                    raise ValueError('Unsafe recipe destination')
                target = output / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(name))
    origin = {'editorRevision': manifest['editorRevision'], 'engineRevision': manifest['engineRevision'],
              'gitSources': manifest['gitSources'],
              'note': 'Program/source bytes retain the supplied revisions. New local commits identify imported snapshots; only lock references are rebound to those local commits. This is not a claim of identical Git commit IDs.'}
    (output / 'SOURCE-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
    directories = [engine, editor]
    if with_dependencies:
        directories += [output / 'mupdf/thirdparty/lcms2', output / 'mupdf', output / 'zlib-ng']
    for directory in directories:
        (directory / 'SOURCE-ORIGIN.json').write_text(json.dumps(origin, indent=2) + '\n')
    if with_dependencies:
        lcms_revision = snapshot(output / 'mupdf/thirdparty/lcms2')
        mupdf_revision = snapshot(output / 'mupdf')
        zlib_revision = snapshot(output / 'zlib-ng')
    snapshot(engine)
    if with_dependencies:
        lock_path = editor / 'native/dependencies.lock.json'
        lock = json.loads(lock_path.read_text())
        lock['mupdf']['revision'] = mupdf_revision
        lock['mupdf']['submodules']['thirdparty/lcms2'] = lcms_revision
        lock['zlibNg']['revision'] = zlib_revision
        lock_path.write_text(json.dumps(lock, indent=2) + '\n')
    snapshot(editor)
    return editor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', nargs='?', type=Path,
                        default=Path(sys.argv[0]) if sys.argv[0].endswith('.zip') else None)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--dependencies', type=Path, help='Also restore native snapshots from this separate dependency-source directory')
    parser.add_argument('--fetch', action='store_true', help='Explicitly fetch missing native snapshots over HTTPS; checksums are required')
    args = parser.parse_args()
    if args.archive is None:
        parser.error('Supply the source ZIP, or run python3 source.zip --output DIRECTORY')
    editor = unpack(args.archive.resolve(), args.output.resolve(), args.dependencies, args.fetch)
    print('Editable source:', editor)
    print('Recipes and patches are restored beside the editor. SOURCE-MANIFEST.json identifies other dependency sources.')
    if (editor.parent / 'mupdf').exists():
        print('Native input check (use your matching ICU prefix):')
        print(f'python3 {editor}/native/scripts/build.py --mupdf-source {editor.parent}/mupdf '
              f'--zlib-ng-source {editor.parent}/zlib-ng --icu-prefix /path/to/icu --output /path/to/new-bundle --check')
    else:
        print('For native engine dependency snapshots, restore to a new output with --dependencies DIRECTORY; add --fetch only if downloads are wanted.')


if __name__ == '__main__':
    main()
