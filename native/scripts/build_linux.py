#!/usr/bin/env python3
"""Build the pinned engine and collect a relocatable Linux runtime (no downloads)."""
import argparse
import json
import os
import platform
import re
import shutil
from pathlib import Path
from common import NATIVE, export_source, pinned_engine, run, sha256

# These libraries and the matching ELF interpreter belong to the host glibc.
SYSTEM = re.compile(r'^(ld-linux[^/]*|lib(c|m|pthread|dl|rt|util|resolv)\.so(?:\..*)?)$')


def dependencies(binary):
    output = run(['ldd', binary], capture=True)
    result = {}
    for line in output.splitlines():
        if 'not found' in line:
            raise ValueError('Unresolved ELF dependency: ' + line)
        match = re.match(r'\s*(\S+) => (/\S+) \(', line)
        if match and not SYSTEM.match(match[1]):
            result[match[1]] = Path(match[2]).resolve()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 8))
    parser.add_argument('--output', type=Path, default=NATIVE / '.bundles' / ('linux-' + platform.machine()))
    args = parser.parse_args()
    if platform.system() != 'Linux' or args.jobs < 1:
        parser.error('Requires Linux and a positive job count')
    for tool in ('make', 'gcc', 'g++', 'pkg-config', 'patchelf', 'ldd', 'dpkg-query'):
        if not shutil.which(tool):
            parser.error('Missing build tool: ' + tool)
    engine, revision = pinned_engine()
    destination = args.output.resolve()
    if destination.exists():
        parser.error('Output already exists; choose a new --output directory')
    work = NATIVE / '.work' / ('linux-' + revision)
    if work.exists():
        parser.error('Work directory exists; remove it before rebuilding: ' + str(work))
    export_source(engine, work)
    run(['make', '-j' + str(args.jobs), 'all'], cwd=work)
    (destination / 'bin').mkdir(parents=True)
    (destination / 'lib').mkdir()
    notices = destination / 'notices'
    notices.mkdir()
    shutil.copyfile(engine / 'LICENSE', notices / 'texpresso-LICENSE')
    mapping = {}
    for name in ('texpresso', 'texpresso-xetex'):
        source = work / 'build' / name
        shutil.copy2(source, destination / 'bin' / name)
        for soname, library in dependencies(source).items():
            if soname in mapping and mapping[soname] != library:
                raise ValueError('Conflicting ELF dependencies: ' + soname)
            mapping[soname] = library
    packages = set()
    for soname, source in mapping.items():
        shutil.copy2(source, destination / 'lib' / soname)
        # On Debian/Ubuntu the package copyright file includes license notices.
        import subprocess
        owners = subprocess.run(['dpkg-query', '-S', str(source)], capture_output=True, text=True)
        if owners.returncode:
            owners = subprocess.run(['dpkg-query', '-S', '*/' + source.name], capture_output=True, text=True)
        for line in owners.stdout.splitlines():
            package = line.split(': ')[0]
            if '/' in package:
                continue
            copyright_file = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
            if copyright_file.is_file():
                shutil.copyfile(copyright_file, notices / (package.replace(':', '-') + '-copyright'))
                packages.add(package)
    for relative in ('bin', 'lib'):
        for binary in (destination / relative).iterdir():
            run(['patchelf', '--set-rpath', '$ORIGIN/../lib' if relative == 'bin' else '$ORIGIN', binary])
    for binary in (destination / 'bin').iterdir():
        for soname, resolved in dependencies(binary).items():
            if not resolved.is_relative_to(destination):
                raise ValueError('Unbundled dependency: ' + soname)
    capabilities = json.loads(run([destination / 'bin/texpresso', '-capabilities'], capture=True))
    files = sorted(p for directory in ('bin', 'lib') for p in (destination / directory).iterdir())
    manifest = {'schema': 1, 'platform': 'linux', 'architecture': platform.machine(),
                'engineRevision': revision, 'capabilities': capabilities,
                'hostPackages': sorted(packages), 'libc': list(platform.libc_ver()),
                'files': [{'path': str(p.relative_to(destination)), 'sha256': sha256(p),
                           'bytes': p.stat().st_size} for p in files]}
    (destination / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Bundle:', destination)


if __name__ == '__main__':
    main()
