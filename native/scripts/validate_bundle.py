#!/usr/bin/env python3
"""Validate a supplied runtime before it enters an application package."""
import argparse
import json
import platform
from pathlib import Path
from common import pinned_engine, sha256


def validate(bundle, revision, system, architecture):
    bundle = bundle.resolve()
    manifest = json.loads((bundle / 'build-manifest.json').read_text())
    if manifest.get('engineRevision') != revision:
        raise ValueError('Bundle engine revision does not match the pinned submodule')
    aliases = {'aarch64': 'arm64', 'amd64': 'x86_64'}
    expected = aliases.get(architecture.lower(), architecture.lower())
    actual = aliases.get(manifest.get('architecture', '').lower(), manifest.get('architecture', '').lower())
    if manifest.get('platform') != system or actual != expected:
        raise ValueError('Bundle platform/architecture does not match the application target')
    capabilities = manifest.get('capabilities', {})
    if capabilities.get('previewProtocol') != 1 or 'png' not in capabilities.get('codecs', []):
        raise ValueError('Bundle requires headless preview protocol 1 with PNG')
    files = manifest.get('files', [])
    names = set()
    for entry in files:
        name = entry['path']
        path = (bundle / name).resolve()
        if not path.is_relative_to(bundle) or name in names:
            raise ValueError('Unsafe or repeated manifest file: ' + name)
        names.add(name)
        if sha256(path) != entry['sha256']:
            raise ValueError('Bundle hash mismatch: ' + name)
    for name in ('bin/texpresso', 'bin/texpresso-xetex'):
        if name not in names or not (bundle / name).stat().st_mode & 0o111:
            raise ValueError('Missing executable in bundle: ' + name)
    if not (bundle / 'notices').is_dir():
        raise ValueError('Bundle dependency notices are missing')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--platform', default=platform.system().lower())
    parser.add_argument('--architecture', default=platform.machine())
    args = parser.parse_args()
    _, revision = pinned_engine()
    validate(args.bundle, revision, args.platform, args.architecture)
    print('Validated bundled TeXpresso ' + revision)
