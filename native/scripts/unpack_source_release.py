#!/usr/bin/env python3
"""Restore a supplied source ZIP into editable, offline-buildable Git snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
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
    with archive.open(member) as stream, tarfile.open(fileobj=stream, mode='r:*') as tar:
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


def unpack(source_zip, output):
    if output.exists():
        raise ValueError('Source output already exists')
    output.mkdir(parents=True)
    with zipfile.ZipFile(source_zip) as archive:
        manifest = json.loads(archive.read('SOURCE-MANIFEST.json'))
        # Verify every supplied upstream input before using any of them.
        for record in manifest['inputs']:
            with archive.open(record['archiveMember']) as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != record['sha256']:
                    raise ValueError(f'Source input checksum mismatch: {record["name"]}')
        editor = output / 'texstudio'
        extract(archive, 'project/texstudio.tar.gz', editor)
        engine = editor / 'native/texpresso'
        extract(archive, 'project/texpresso.tar.gz', engine)
        for record in manifest['inputs']:
            if not record.get('extractTo'):
                continue
            relative = Path(record['extractTo'])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Unsafe Git snapshot destination')
            extract(archive, record['archiveMember'], output / relative, strip_root=True)
        for name in archive.namelist():
            if name.startswith('recipes/') and not name.endswith('/'):
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
    for directory in [output / 'mupdf/thirdparty/lcms2', output / 'mupdf', output / 'zlib-ng', engine, editor]:
        (directory / 'SOURCE-ORIGIN.json').write_text(json.dumps(origin, indent=2) + '\n')
    lcms_revision = snapshot(output / 'mupdf/thirdparty/lcms2')
    mupdf_revision = snapshot(output / 'mupdf')
    zlib_revision = snapshot(output / 'zlib-ng')
    snapshot(engine)
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
    args = parser.parse_args()
    if args.archive is None:
        parser.error('Supply the source ZIP, or run python3 source.zip --output DIRECTORY')
    editor = unpack(args.archive.resolve(), args.output.resolve())
    print('Editable source:', editor)
    print('Original dependency archives remain in the source ZIP; recipes are restored beside the editor.')
    print('Native input check (use your matching ICU prefix):')
    print(f'python3 {editor}/native/scripts/build.py --mupdf-source {editor.parent}/mupdf '
          f'--zlib-ng-source {editor.parent}/zlib-ng --icu-prefix /path/to/icu --output /path/to/new-bundle --check')


if __name__ == '__main__':
    main()
