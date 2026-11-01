#!/usr/bin/env python3
"""Package dictionary/affix pairs and thesauri as deterministic, lazy-load ZIPs."""
import argparse
from pathlib import Path
import os
import shutil
import tempfile
import zipfile


def pack(source, output):
    output.mkdir(parents=True, exist_ok=True)
    for file in sorted(source.iterdir()):
        if not file.is_file():
            continue
        if file.suffix == '.aff' and file.with_suffix('.dic').is_file():
            continue
        if file.suffix == '.idx' and file.with_suffix('.dat').is_file():
            continue
        if file.suffix not in ('.dic', '.dat'):
            shutil.copyfile(file, output / file.name)
            continue
        companion = file.with_suffix('.aff' if file.suffix == '.dic' else '.idx')
        if file.suffix == '.dic' and not companion.is_file():
            raise ValueError(f'Missing affix file for {file}')
        members = [file] + ([companion] if companion.is_file() else [])
        # Replace atomically; an interrupted build never leaves a partial ZIP.
        with tempfile.NamedTemporaryFile(dir=output, delete=False) as temporary:
            staged = Path(temporary.name)
        try:
            with zipfile.ZipFile(staged, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for member in members:
                    info = zipfile.ZipInfo(member.name, (1980, 1, 1, 0, 0, 0))
                    info.create_system = 3
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    archive.writestr(info, member.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
            staged.chmod(0o644)
            os.replace(staged, output / (file.name + '.zip'))
        finally:
            staged.unlink(missing_ok=True)


def prune_bundle(source, resources):
    # Incremental CMake builds may still contain the previous uncompressed files.
    # Touch only build-output dictionaries whose compressed replacement exists.
    for file in source.iterdir():
        if file.suffix not in ('.dic', '.dat'):
            continue
        archive = resources / (file.name + '.zip')
        if not archive.is_file():
            raise ValueError(f'Missing bundled dictionary archive: {archive}')
        (resources / file.name).unlink(missing_ok=True)
        (resources / file.with_suffix('.aff' if file.suffix == '.dic' else '.idx').name).unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--output', type=Path)
    action.add_argument('--prune-bundle', type=Path)
    args = parser.parse_args()
    if args.output:
        pack(args.source.resolve(), args.output.resolve())
    else:
        prune_bundle(args.source.resolve(), args.prune_bundle.resolve())
