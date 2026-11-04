import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from unpack_source_release import unpack, command
from common import check_source, export_source
from source_release import export_editor_source
from unpack_source_release import snapshot


def tar(files, prefix=''):
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode='w:gz') as archive:
        for name, value in files.items():
            data = value.encode()
            member = tarfile.TarInfo(prefix + name)
            member.size = len(data)
            member.mode = 0o644
            archive.addfile(member, io.BytesIO(data))
    return stream.getvalue()


class SourceRestore(unittest.TestCase):
    def test_editor_export_excludes_only_unused_legacy_qt_binaries(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'source'
            (source / 'translation').mkdir(parents=True)
            (source / 'translation/qt_de.qm').write_bytes(b'legacy binary without source')
            (source / 'translation/texstudio_de.qm').write_bytes(b'app translation')
            (source / 'program.cpp').write_text('preferred editable source')
            revision = snapshot(source)
            output = root / 'source.tar.gz'
            excluded = export_editor_source(source, revision, output)
            self.assertEqual(excluded, ['translation/qt_de.qm'])
            with tarfile.open(output) as archive:
                names = archive.getnames()
                self.assertIn('program.cpp', names)
                self.assertIn('translation/texstudio_de.qm', names)
                self.assertNotIn('translation/qt_de.qm', names)

    def test_archived_snapshots_pass_native_pinned_input_contract_offline(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            file = root / 'source.zip'
            lock = {'mupdf': {'revision': 'a' * 40, 'submodules': {'thirdparty/lcms2': 'b' * 40}},
                    'zlibNg': {'revision': 'c' * 40}}
            inputs = []
            with zipfile.ZipFile(file, 'w') as archive:
                archive.writestr('project/texstudio.tar.gz', tar({'native/dependencies.lock.json': json.dumps(lock),
                                                              '.gitignore': 'generated/\n'}))
                archive.writestr('project/texpresso.tar.gz', tar({'engine.c': 'preferred editable engine source\n',
                                                               '.gitignore': 'generated/\n'}))
                for name, target in [('mupdf', 'mupdf'), ('lcms2', 'mupdf/thirdparty/lcms2'), ('zlib-ng', 'zlib-ng')]:
                    data = tar({'source.c': 'preferred editable ' + name}, prefix=name + '/')
                    member = 'upstream/' + name
                    archive.writestr(member, data)
                    inputs.append({'name': name, 'archiveMember': member,
                                   'extractTo': target, 'sha256': hashlib.sha256(data).hexdigest()})
                archive.writestr('SOURCE-MANIFEST.json', json.dumps({'editorRevision': 'd' * 40,
                    'engineRevision': 'e' * 40, 'gitSources': [], 'inputs': inputs}))
            editor = unpack(file, root / 'restored')
            restored = json.loads((editor / 'native/dependencies.lock.json').read_text())
            check_source(editor.parent / 'mupdf', restored['mupdf']['revision'])
            check_source(editor.parent / 'mupdf/thirdparty/lcms2', restored['mupdf']['submodules']['thirdparty/lcms2'])
            check_source(editor.parent / 'zlib-ng', restored['zlibNg']['revision'])
            self.assertTrue(command(editor, 'ls-files', '--stage', 'native/texpresso').startswith('160000 '))
            export_source(editor / 'native/texpresso', root / 'exported')
            self.assertEqual((root / 'exported/engine.c').read_text(), 'preferred editable engine source\n')


if __name__ == '__main__':
    unittest.main()
