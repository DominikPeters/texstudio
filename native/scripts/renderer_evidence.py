"""Record retained font payloads and the static renderer's actual build inputs."""
from pathlib import Path

from common import run, sha256


def collect(frontend, source, profile, revision):
    binary = frontend.read_bytes()
    fonts = []
    notices = {}
    for file in sorted((source / 'resources/fonts').rglob('*')):
        if file.suffix not in ('.cff', '.ttf', '.otf') or file.read_bytes() not in binary:
            continue
        relative = str(file.relative_to(source))
        fonts.append({'source': relative, 'sha256': sha256(file),
                      'license': 'Apache-2.0' if '/droid/' in relative else 'OFL-1.1'})
        for name in ('NOTICE', 'COPYING', 'OFL.txt'):
            if (file.parent / name).is_file():
                notices['mupdf-font-' + file.parent.name + '-' + name] = file.parent / name
    archives = {name: sha256(source / 'build/vscode' / name)
                for name in ('libmupdf.a', 'libmupdf-third.a')}
    members = [line for line in run(['ar', '-t', source / 'build/vscode/libmupdf-third.a'], capture=True).splitlines()
               if line.endswith('.o')]
    if not members or any(not Path(name).name.startswith('cms') for name in members):
        raise ValueError('Renderer static third-party contents changed; review their license and notice requirements')
    return {'mupdfRevision': revision, 'profileSha256': sha256(profile), 'archives': archives,
            'retainedFonts': fonts, 'staticThirdPartyMembers': members,
            'method': 'Exact source font bytes found in unstripped linked frontend; actual archive hashes and member table'}, notices
