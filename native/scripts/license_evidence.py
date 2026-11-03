"""Validate and assemble checked-in redistribution evidence without network I/O."""
import hashlib
import json
from pathlib import Path
import shutil

from common import ROOT, sha256


def copy_qt_notices(key, package, destination):
    manifest = ROOT / 'native/notices/qt/manifest.json'
    if not manifest.is_file():
        return []
    record = next((r for r in json.loads(manifest.read_text()) if r['package'] == key), None)
    if not record:
        return []
    expected = next((r['checksumValue'] for r in package.get('sourceChecksums', [])
                     if r['algorithm'] == 'SHA256'), None)
    if expected != record['sha256']:
        raise ValueError(f'Qt notice source does not match installed source checksum: {key}')
    source = manifest.parent / key
    for entry in record['files']:
        file = source / entry['path']
        if not file.resolve().is_relative_to(source.resolve()) or sha256(file) != entry['sha256']:
            raise ValueError(f'Qt notice changed or escaped its source: {key}/{entry["path"]}')
    target = destination / 'qt-source'
    shutil.copytree(source, target)
    return sorted(p for p in target.rglob('*') if p.is_file())


def copy_library_notices(key, package, destination):
    source = ROOT / 'native/notices/library-sources'
    manifest = source / 'manifest.json'
    if not manifest.is_file():
        return []
    record = next((r for r in json.loads(manifest.read_text()) if r['package'] == key), None)
    if not record:
        return []
    expected = next((r['checksumValue'] for r in package.get('sourceChecksums', [])
                     if r['algorithm'] == 'SHA256'), None)
    if expected != record['sourceSha256']:
        raise ValueError(f'Library notice source does not match installed package: {key}')
    source = source / key
    for entry in record['files']:
        file = source / entry['path']
        if not file.resolve().is_relative_to(source.resolve()) or sha256(file) != entry['sha256']:
            raise ValueError(f'Library notice changed: {key}/{entry["path"]}')
    target = destination / 'source-notices'
    shutil.copytree(source, target)
    return sorted(p for p in target.rglob('*') if p.is_file())


def dictionary_evidence(archive, records):
    """Bind a license notice to exact decompressed payloads, rejecting stale data."""
    name = archive.filename.rsplit('/', 1)[-1].removesuffix('.zip')
    record = next((r for r in records if r['name'] == name), None)
    if record is None:
        return None
    expected = {r['name']: r['sha256'] for r in record['members']}
    actual = {n: hashlib.sha256(archive.read(n)).hexdigest() for n in archive.namelist()}
    if expected != actual:
        raise ValueError(f'Dictionary changed without updating its licensing evidence: {name}')
    for notice in record['notices']:
        file = ROOT / notice['path']
        if not file.resolve().is_relative_to(ROOT.resolve()) or not file.is_file():
            raise ValueError(f'Missing or escaping dictionary notice: {notice["path"]}')
        if notice.get('sha256') and sha256(file) != notice['sha256']:
            raise ValueError(f'Dictionary notice changed: {notice["path"]}')
    return record


def copy_dictionary_notices(record, output):
    for notice in record['notices']:
        file = ROOT / notice['path']
        target = output / notice['path']
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, target)
