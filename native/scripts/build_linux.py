#!/usr/bin/env python3
"""Build the pinned engine and collect a relocatable Linux runtime (no downloads)."""
import argparse
import json
import os
import platform
import re
import shutil
import shlex
import subprocess
from pathlib import Path
from common import NATIVE, export_source, pinned_engine, run, sha256, check_source
import zlib_ng

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
    parser.add_argument('--mupdf-source', type=Path, help='Pinned compact renderer source, preferred over distribution MuPDF')
    parser.add_argument('--zlib-ng-source', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=NATIVE / '.bundles' / ('linux-' + platform.machine()))
    args = parser.parse_args()
    if platform.system() != 'Linux' or args.jobs < 1:
        parser.error('Requires Linux and a positive job count')
    for tool in ('cmake', 'make', 'gcc', 'g++', 'pkg-config', 'patchelf', 'ldd', 'dpkg-query'):
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
    zng_include, zng_library, zng_notice, zng_metadata = zlib_ng.build(args.zlib_ng_source.resolve(), work / '.zlib-ng', args.jobs)
    if args.mupdf_source:
        lock = json.loads((NATIVE / 'dependencies.lock.json').read_text())
        source = args.mupdf_source.resolve()
        check_source(source, lock['mupdf']['revision'])
        mupdf = work / '.mupdf'
        export_source(source, mupdf)
        for relative, revision in lock['mupdf']['submodules'].items():
            check_source(source / relative, revision)
            (mupdf / relative).rmdir()
            export_source(source / relative, mupdf / relative)
        shutil.copyfile(NATIVE / 'profiles/mupdf.mk', mupdf / 'user.make')
        packages = ['freetype2', 'libjpeg', 'libopenjp2', 'libbrotlidec', 'libbrotlienc', 'sdl2']
        cflags = shlex.split(run(['pkg-config', '--cflags', *packages], capture=True))
        run(['make', '-j' + str(args.jobs), 'libs', 'OUT=build/compact',
             'XCFLAGS=' + shlex.join(cflags + ['-fPIC'])], cwd=mupdf)
        libraries = [str(mupdf / 'build/compact/libmupdf.a'), str(mupdf / 'build/compact/libmupdf-third.a')]
        libraries += shlex.split(run(['pkg-config', '--libs', *packages], capture=True))
        libraries += ['-ljbig2dec', '-lz', '-lm']
        (work / 'Makefile.config').write_text(
            'CFLAGS=' + shlex.join(['-O2', '-fPIC', '-I.', '-I' + str(mupdf / 'include')]) + '\n' +
            'CC=gcc $(CFLAGS)\nLDCC=g++ $(CFLAGS) -Wl,--gc-sections\nLIBS=' + shlex.join(libraries) + '\n')
    else:
        run(['make', 'config'], cwd=work)
    with (work / 'Makefile.config').open('a') as config:
        config.write('CFLAGS+= ' + zlib_ng.make_flags(['-DTXP_ZLIB_NG', '-I' + str(zng_include)]) + '\n')
        config.write('LIBS+= ' + zlib_ng.make_flags([zng_library]) + '\n')
    run(['make', '-j' + str(args.jobs), 'all'], cwd=work)
    run(['make', 'test-preview-png'], cwd=work)
    (destination / 'bin').mkdir(parents=True)
    (destination / 'lib').mkdir()
    run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
         NATIVE / 'wsl/supervisor.c', '-o', destination / 'bin/texpresso-wsl'])
    notices = destination / 'notices'
    notices.mkdir()
    shutil.copyfile(engine / 'LICENSE', notices / 'texpresso-LICENSE')
    shutil.copyfile(NATIVE.parent / 'COPYING', notices / 'texstudio-wsl-COPYING')
    shutil.copyfile(zng_notice, notices / 'zlib-ng-LICENSE')
    if args.mupdf_source:
        shutil.copyfile(mupdf / 'COPYING', notices / 'mupdf-COPYING')
        shutil.copyfile(mupdf / 'thirdparty/lcms2/LICENSE', notices / 'lcms2-LICENSE')
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
    # MuPDF is statically linked by the distro development package.
    for package in ('libmupdf-dev', 'libmujs-dev', 'libgumbo-dev'):
        copyright_file = Path('/usr/share/doc') / package / 'copyright'
        if copyright_file.is_file():
            shutil.copyfile(copyright_file, notices / (package + '-copyright'))
    for relative in ('bin', 'lib'):
        for binary in (destination / relative).iterdir():
            run(['patchelf', '--set-rpath', '$ORIGIN/../lib' if relative == 'bin' else '$ORIGIN', binary])
    for binary in (destination / 'bin').iterdir():
        for soname, resolved in dependencies(binary).items():
            if not resolved.is_relative_to(destination):
                raise ValueError('Unbundled dependency: ' + soname)
    capabilities = json.loads(run([destination / 'bin/texpresso', '-capabilities'], capture=True))
    zlib_ng.verify_capabilities(capabilities)
    files = sorted(p for directory in ('bin', 'lib') for p in (destination / directory).iterdir())
    manifest = {'mupdfRevision': lock['mupdf']['revision'] if args.mupdf_source else None,
                'rendererProfile': sha256(NATIVE / 'profiles/mupdf.mk') if args.mupdf_source else 'distribution',
                'schema': 1, 'platform': 'linux', 'architecture': platform.machine(),
                'engineRevision': revision, 'capabilities': capabilities, 'zlibNg': zng_metadata,
                'hostPackages': sorted(packages), 'libc': list(platform.libc_ver()),
                'files': [{'path': str(p.relative_to(destination)), 'sha256': sha256(p),
                           'bytes': p.stat().st_size} for p in files]}
    (destination / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Bundle:', destination)


if __name__ == '__main__':
    main()
