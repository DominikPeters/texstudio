"""Exercise the installer with an offline fake CTAN archive and isolated HOME."""
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


@unittest.skipIf(os.name == 'nt' or not shutil.which('tar'), 'POSIX shell/tar required')
class TexLiveSetup(unittest.TestCase):
    def run_install(self, scheme, existing=False):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        root = Path(temporary.name); home = root / 'home'; home.mkdir()
        tools = root / 'bin'; tools.mkdir(); work = root / 'work'; work.mkdir()
        archive = root / 'installer.tar.gz'
        with tarfile.open(archive, 'w:gz') as tar:
            data = b'Official installer fixture\n'; item = tarfile.TarInfo('install-tl-fixture/install-tl')
            item.size = len(data); tar.addfile(item, io.BytesIO(data))
        curl = tools / 'curl'
        curl.write_text('#!' + sys.executable + '\nimport os, shutil, sys\nshutil.copyfile(os.environ["FIXTURE_ARCHIVE"], sys.argv[sys.argv.index("--output")+1])\nprint("https://example.org/tlnet/install-tl-unx.tar.gz", end="")\n')
        perl = tools / 'perl'
        perl.write_text('#!' + sys.executable + '''
import json, os, pathlib, sys
profile = dict(line.split(' ',1) for line in pathlib.Path(sys.argv[sys.argv.index('--profile')+1]).read_text().splitlines())
assert '--no-interaction' in sys.argv
assert sys.argv[sys.argv.index('--repository')+1] == 'https://example.org/tlnet'
pathlib.Path(os.environ['PROFILE_DUMP']).write_text(json.dumps(profile))
bin = pathlib.Path(profile['TEXDIR'])/'bin/test-linux'; bin.mkdir(parents=True)
for tool in ('kpsewhich','tlmgr','xelatex'):
    file=bin/tool
    file.write_text('#!/bin/sh\\nprintf "%s\\\\n" "' + tool + ' $*" >> "$EVENTS"\\n')
    file.chmod(0o755)
''')
        curl.chmod(0o755); perl.chmod(0o755)
        destination = home / '.local/share/texlive/espresso'
        if existing: destination.mkdir(parents=True); (destination / 'KEEP').write_text('existing user installation')
        environment = {**os.environ, 'HOME':str(home), 'TMPDIR':str(work),
            'PATH':str(tools)+os.pathsep+os.environ['PATH'], 'FIXTURE_ARCHIVE':str(archive),
            'PROFILE_DUMP':str(root/'profile.json'), 'EVENTS':str(root/'events')}
        result = subprocess.run(['/bin/sh',str(Path(__file__).resolve().parents[1]/'wsl/install-texlive.sh'),scheme],
            env=environment, capture_output=True, text=True, timeout=15)
        self.assertEqual(list(work.iterdir()), [], 'Temporary installer files must be cleaned')
        return root, destination, result

    def test_small_user_install_adds_bibliography_tools_and_verifies_compile(self):
        root, destination, result = self.run_install('small')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        profile = json.loads((root/'profile.json').read_text())
        self.assertEqual(profile['TEXDIR'],str(destination))
        self.assertEqual(profile['selected_scheme'],'scheme-small')
        events=(root/'events').read_text()
        self.assertIn('tlmgr --repository https://example.org/tlnet install biber biblatex latexmk',events)
        self.assertIn('xelatex -interaction=nonstopmode -halt-on-error main.tex',events)

    def test_full_install_uses_full_profile(self):
        root, _, result = self.run_install('full')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(json.loads((root/'profile.json').read_text())['selected_scheme'],'scheme-full')
        self.assertNotIn('tlmgr', (root/'events').read_text())

    def test_existing_installation_is_preserved(self):
        root, destination, result = self.run_install('full',existing=True)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual((destination/'KEEP').read_text(),'existing user installation')
        self.assertFalse((root/'profile.json').exists())

if __name__ == '__main__': unittest.main()
