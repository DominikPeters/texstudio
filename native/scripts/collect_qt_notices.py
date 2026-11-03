#!/usr/bin/env python3
"""Collect Qt notices from checksum-verified source archives (explicit --fetch)."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import tarfile
import urllib.request


def collect(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        members = {str(PurePosixPath(m.name).relative_to(PurePosixPath(m.name).parts[0])): m
                   for m in tar.getmembers() if m.isfile() and len(PurePosixPath(m.name).parts) > 1}
        selected = {n for n in members if
                    re.match(r'(?i)^(license|copying|notice|copyright)([._-]|$)', PurePosixPath(n).name)
                    or 'LICENSES' in PurePosixPath(n).parts or n.endswith('qt_attribution.json')}
        attributions = []
        for name, member in members.items():
            if not name.endswith('qt_attribution.json') or any(
                    p in PurePosixPath(name).parts for p in ('tests', 'examples', 'testdata')):
                continue
            # Qt attribution files deliberately contain literal multiline strings.
            data = json.load(tar.extractfile(member), strict=False)
            for entry in data if isinstance(data, list) else [data]:
                attributions.append({'metadataPath': name, **entry})
                for field in ('LicenseFile', 'LicenseFiles'):
                    values = entry.get(field, [])
                    for value in [values] if isinstance(values, str) else values:
                        found = False
                        for parent in (PurePosixPath(name).parent,
                                       PurePosixPath(name).parent / entry.get('Path', '')):
                            candidate = posixpath.normpath(str(parent / value))
                            if candidate in members:
                                selected.add(candidate)
                                found = True
                        if not found:
                            raise ValueError(f'Missing referenced Qt license: {name}: {value}')
        files = []
        for name in sorted(selected):
            if '..' in PurePosixPath(name).parts or PurePosixPath(name).is_absolute():
                raise ValueError(f'Unsafe archive path: {name}')
            data = tar.extractfile(members[name]).read()
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            files.append({'path': name, 'sha256': hashlib.sha256(data).hexdigest()})
        data = (json.dumps(attributions, indent=2) + '\n').encode()
        (destination / 'ATTRIBUTIONS.json').write_bytes(data)
        files.append({'path': 'ATTRIBUTIONS.json', 'sha256': hashlib.sha256(data).hexdigest()})
    return files, len(attributions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fetch', action='store_true', help='Allow downloading missing public source archives')
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    manifest = []
    for key, package in json.loads(args.inventory.read_text())['packages'].items():
        if not key.startswith('qt') or key.startswith('qtwebengine'):
            # QtPdf's installed LICENSE.Chromium is collected by the app packager.
            continue
        url = package['source']
        expected = next(x['checksumValue'] for x in package['sourceChecksums'] if x['algorithm'] == 'SHA256')
        archive = args.cache / url.rsplit('/', 1)[-1]
        if not archive.exists():
            if not args.fetch:
                parser.error(f'Missing cached source archive: {archive}; use --fetch')
            temporary = archive.with_suffix('.partial')
            urllib.request.urlretrieve(url, temporary)
            if hashlib.file_digest(temporary.open('rb'), 'sha256').hexdigest() != expected:
                temporary.unlink()
                raise ValueError(f'Source checksum mismatch: {url}')
            temporary.rename(archive)
        if hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() != expected:
            raise ValueError(f'Cached source checksum mismatch: {archive}')
        files, count = collect(archive, args.output / key)
        manifest.append({'package': key, 'url': url, 'sha256': expected, 'files': files,
                         'attributions': count,
                         'scope': 'Matching Qt source-module license and attribution superset; shipped targets remain selected by Qt SBOM'})
        print(f'{key}: {len(files)} notice files, {count} attribution records', flush=True)
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
