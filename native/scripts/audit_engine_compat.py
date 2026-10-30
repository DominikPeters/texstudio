#!/usr/bin/env python3
"""Compare named XeTeX primitives against an installed XeLaTeX reference.

Pass the upstream WEB file AND its change files: cross-engine additions such as
partokencontext do not appear in xetex.web itself. This only probes presence and
primitive identity; it does not establish equivalent behavior.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def primitive_names(text):
    names = re.findall(r'primitive\s*\(\s*"([^"\n]+)"', text)
    if '#define XETEX_FORMAT_PRIMITIVE_INITIALIZERS' in text:
        table = text.split('#define XETEX_FORMAT_PRIMITIVE_INITIALIZERS', 1)[1]
        names += re.findall(r'\{ "([^"\n]+)",', table)
    # Control symbols (space, -, /) are not named control words and need a
    # different TeX encoding. Do not accidentally execute arbitrary source text.
    return {name for name in names if re.fullmatch(r'[A-Za-z]+', name)}


def probe(command, candidates, directory, env):
    lines = [r'\documentclass{article}', r'\begin{document}',
             r'\typeout{AUDIT-VERSION:\the\XeTeXversion\XeTeXrevision}']
    for name in candidates:
        lines.append(
            r'\ifcsname ' + name + r'\endcsname'
            r'\expandafter\ifprimitive\csname ' + name + r'\endcsname'
            r'\typeout{AUDIT:' + name + r':primitive}\else'
            r'\typeout{AUDIT:' + name + r':defined}\fi\else'
            r'\typeout{AUDIT:' + name + r':missing}\fi')
    lines.append(r'\end{document}')
    directory.mkdir()
    (directory / 'main.tex').write_text('\n'.join(lines) + '\n')
    result = subprocess.run(command, cwd=directory, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=60)
    output = result.stdout.decode(errors='replace')
    inventory = dict(re.findall(r'^AUDIT:([A-Za-z]+):(primitive|defined|missing)$', output, re.M))
    if result.returncode or set(inventory) != set(candidates):
        raise RuntimeError(f'{command[0]} failed to complete the inventory:\n{output[-4000:]}')
    version = re.search(r'^AUDIT-VERSION:(.+)$', output, re.M)
    if not version:
        raise RuntimeError('Reference must be a XeTeX engine')
    return {'version': version[1].strip(), 'commands': inventory}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--xelatex', default=shutil.which('xelatex') or '/Library/TeX/texbin/xelatex')
    parser.add_argument('--upstream', type=Path, action='append', required=True,
                        help='Upstream WEB/change file; repeat for every source file')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    engine = args.engine.resolve(strict=True)
    # xelatex is commonly a symlink to xetex: argv[0] selects the format.
    # Resolving that symlink would silently run plain XeTeX instead of LaTeX.
    reference = Path(args.xelatex).expanduser().absolute()
    reference.resolve(strict=True)
    header = ROOT / 'native/texpresso/src/engine/engine/xetex_format.h'
    candidates = primitive_names(header.read_text())
    sources = []
    for source in args.upstream:
        data = source.read_bytes()
        candidates.update(primitive_names(data.decode('utf-8')))
        sources.append({'path': str(source.resolve()), 'sha256': hashlib.sha256(data).hexdigest()})
    candidates = sorted(candidates)
    env = dict(os.environ)
    env['PATH'] = str(reference.parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='texpresso-compat-') as work:
        # Do not let another binary's cached format mask an engine difference,
        # and do not modify the interactive editor's provider/format cache.
        env['XDG_CACHE_HOME'] = str(Path(work) / 'cache')
        embedded = probe([str(engine), '-texlive', 'main.tex'], candidates, Path(work) / 'embedded', env)
        installed = probe([str(reference), '-interaction=nonstopmode', '-halt-on-error', 'main.tex'],
                          candidates, Path(work) / 'installed', env)
    differences = [{'name': name, 'reference': installed['commands'][name],
                    'embedded': embedded['commands'][name]}
                   for name in candidates if installed['commands'][name] != embedded['commands'][name]]
    missing = [d['name'] for d in differences
               if d['reference'] == 'primitive' and d['embedded'] == 'missing']
    identity = [d['name'] for d in differences
                if d['reference'] == 'primitive' and d['embedded'] == 'defined']
    report = {'candidateCount': len(candidates), 'upstreamSources': sources,
              'engine': str(engine), 'engineSha256': hashlib.sha256(engine.read_bytes()).hexdigest(),
              'reference': str(reference),
              'referenceSha256': hashlib.sha256(reference.read_bytes()).hexdigest(),
              'primitiveHeaderSha256': hashlib.sha256(header.read_bytes()).hexdigest(),
              'embedded': embedded,
              'installed': installed, 'differences': differences,
              'missingPrimitives': missing, 'primitiveIdentityDisagreements': identity}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(candidates)} named candidates; embedded XeTeX {embedded["version"]}; '
          f'reference XeTeX {installed["version"]}')
    print('Missing primitives: ' + (', '.join(missing) or '(none)'))
    print(f'{len(identity)} present commands disagree under ifprimitive; '
          'these are identity/behavior differences, not missing definitions.')


if __name__ == '__main__':
    main()
