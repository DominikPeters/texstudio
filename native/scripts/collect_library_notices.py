#!/usr/bin/env python3
"""Collect license files from cached, verified library source archives, offline."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile

from source_release import fetch_record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = []
    for key, package in json.loads(args.inventory.read_text())['packages'].items():
        if key.startswith('qt') or key.endswith('-local'):
            continue
        digest = next((c['checksumValue'] for c in package.get('sourceChecksums', []) if c['algorithm'] == 'SHA256'), None)
        if not digest:
            raise ValueError(f'No source checksum for {key}')
        record = {'name': key, 'url': package['source'], 'sha256': digest}
        archive = fetch_record(record, args.cache, False)
        destination = args.output / key
        destination.mkdir(parents=True, exist_ok=True)
        files = []
        with tarfile.open(archive) as tar:
            for member in tar:
                if not member.isfile():
                    continue
                relative = Path(*Path(member.name).parts[1:])
                if relative.is_absolute() or '..' in relative.parts:
                    raise ValueError('Unsafe source notice path')
                if not (re.match(r'(?i)^(licen[cs]e|copying|copyright|notice|authors)([._-]|$)', relative.name)
                        or relative.name.lower() in ('ftl.txt', 'readme.ijg') or relative.name.lower().startswith('readme') or 'LICENSES' in relative.parts):
                    continue
                data = tar.extractfile(member).read()
                if relative.name.lower().startswith('readme') and not re.search(br'(?i)(copyright|licen[cs]e|permission)', data):
                    continue
                file = destination / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(data)
                files.append({'path': str(relative), 'sha256': hashlib.sha256(data).hexdigest()})
        manifest.append({'package': key, 'sourceSha256': digest, 'files': files})
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
