#!/usr/bin/env python3
"""Compare production PNG and candidate WebP codecs serially on public content."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import struct
import subprocess
import zlib


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixture(output):
    directory = output / 'fixture'
    directory.mkdir()
    shutil.copy2(Path(__file__).with_name('resolution.tex'), directory / 'resolution.tex')
    width, height = 1024, 1440
    rng = random.Random(42)
    rows = b''.join(b'\0' + rng.randbytes(width * 3) for _ in range(height))

    def chunk(tag, payload):
        return struct.pack('>I', len(payload)) + tag + payload + struct.pack('>I', zlib.crc32(tag + payload))

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    (directory / 'noise.png').write_bytes(png)
    xelatex = shutil.which('xelatex') or '/Library/TeX/texbin/xelatex'
    with (output / 'fixture-build.log').open('wb') as log:
        subprocess.run([xelatex, '-no-shell-escape', '-interaction=nonstopmode', '-halt-on-error', 'resolution.tex'],
                       cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=True)
    return directory / 'resolution.pdf'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, help='Reuse the public three-page resolution fixture PDF')
    parser.add_argument('--engine-build', type=Path, help='Record the linked native object hashes')
    parser.add_argument('--widths', type=int, nargs='+', default=[1200, 2240])
    parser.add_argument('--codecs', nargs='+', default=['png', 'll0', 'll1', 'll0-mt', 'near80', 'near60', 'near80-l1', 'near60-l1', 'q75', 'q75-mt', 'q75-default', 'q85', 'q95', 'q95-sharp'])
    parser.add_argument('--keep-images', action='store_true', help='Keep full encoded pages; crops are always retained')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    pdf = args.fixture.resolve() if args.fixture else fixture(output)
    benchmark = args.benchmark.resolve()
    metadata = dict(platform=platform.platform(), machine=platform.machine(),
                    benchmarkSha256=digest(benchmark), fixtureSha256=digest(pdf),
                    order='serial; reverse codec order at alternating widths; PNG repeated at end of each case', runs=[])
    metadata['harnessSourceSha256'] = {name: digest(Path(__file__).with_name(name))
                                       for name in ('benchmark_codecs.py', 'codec_benchmark.cpp', 'png_compression.h')}
    if args.engine_build:
        metadata['nativeObjectSha256'] = {name: digest(args.engine_build / 'build/frontend' / name)
                                          for name in ('preview.o', 'renderer.o')}
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    for wi, width in enumerate(args.widths):
        codecs = args.codecs if wi % 2 == 0 else list(reversed(args.codecs))
        for name, page, kind in [('text', 0, 'page'), ('vectors', 1, 'page'), ('noise', 2, 'page'),
                                 ('word', 0, 'word'), ('paragraph', 0, 'paragraph')]:
            for index, codec in enumerate(codecs + ['png']):
                prefix = output / f'{width}-{name}-{index:02}-{codec}'
                command = [str(benchmark), str(pdf), str(page), str(width), kind, codec, str(prefix)]
                with prefix.with_suffix('.json').open('wb') as stdout, prefix.with_suffix('.log').open('wb') as stderr:
                    subprocess.run(command, env=env, stdout=stdout, stderr=stderr, check=True, timeout=180)
                result = json.loads(prefix.with_suffix('.json').read_text())
                if not result['quality']['inkPixels']:
                    raise RuntimeError(f'Empty benchmark image: {prefix}')
                result.update(name=name, sequence=index, artifact=prefix.name)
                metadata['runs'].append(result)
                (output / 'results.json').write_text(json.dumps(metadata, indent=2) + '\n')
                if not args.keep_images:
                    prefix.with_suffix('.png' if codec == 'png' or codec.startswith(('ng', 'deflate')) else '.webp').unlink()
                print(f'{prefix.name}: encode {result["encodeMs"]["median"]:.2f} ms, decode {result["decodeMs"]["median"]:.2f} ms, {result["encodedBytes"]} bytes', flush=True)
    print(output / 'results.json')


if __name__ == '__main__':
    main()
