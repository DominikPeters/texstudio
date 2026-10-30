#!/usr/bin/env python3
"""Serial resolution-budget comparison with synthetic public content, no paper files."""
import argparse
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark', type=Path, required=True)
    parser.add_argument('--before-engine', type=Path, required=True)
    parser.add_argument('--after-engine', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    fixture = output / 'fixture'
    fixture.mkdir()
    shutil.copy2(ROOT / 'src/texpresso/benchmarks/resolution.tex', fixture / 'resolution.tex')
    # Pure stdlib; high entropy exercises RGB encoding and the larger transfer
    # bound, unlike an all-white/text-only compression benchmark.
    width, height = 1024, 1440
    rng = random.Random(42)
    rows = b''.join(b'\0' + rng.randbytes(width * 3) for _ in range(height))
    def chunk(tag, payload):
        return struct.pack('>I', len(payload)) + tag + payload + struct.pack('>I', zlib.crc32(tag + payload))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    (fixture / 'noise.png').write_bytes(png)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    xelatex = shutil.which('xelatex') or '/Library/TeX/texbin/xelatex'
    with (output / 'fixture-build.log').open('wb') as log:
        subprocess.run([xelatex, '-no-shell-escape', '-interaction=nonstopmode', '-halt-on-error', 'resolution.tex'],
                       cwd=fixture, stdout=log, stderr=subprocess.STDOUT, env=env, check=True)
    results = []
    runs = [('before-1', args.before_engine, 4000000), ('after-8mp', args.after_engine, 8000000),
            ('after-16mp', args.after_engine, 16000000), ('before-2', args.before_engine, 4000000)]
    for name, engine, budget in runs:
        print(name, flush=True)
        command = [str(args.benchmark.resolve()), str(engine.resolve()), str(fixture / 'resolution.pdf'), str(budget), str(output / name)]
        with (output / (name + '.json')).open('wb') as stdout, (output / (name + '.log')).open('wb') as stderr:
            subprocess.run(command, stdout=stdout, stderr=stderr, env=env, check=True)
        results.append(dict(name=name, **json.loads((output / (name + '.json')).read_text())))
    (output / 'results.json').write_text(json.dumps({'runs': results}, indent=2) + '\n')
    print(output / 'results.json')


if __name__ == '__main__':
    main()
