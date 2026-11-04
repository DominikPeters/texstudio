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


class SplitSourceRelease(unittest.TestCase):
    def prepare(self, root):
        import source_release
        from unittest.mock import patch
        source = root / 'project'
        engine = source / 'native/texpresso'
        engine.mkdir(parents=True)
        (source / 'native/scripts').mkdir()
        (source / 'native/scripts/unpack_source_release.py').write_text(
            (Path(__file__).resolve().parents[1] / 'scripts/unpack_source_release.py').read_text())
        (engine / 'engine.c').write_text('editable engine\n')
        engine_revision = snapshot(engine)
        lock = {'mupdf': {'revision': 'a' * 40, 'submodules': {'thirdparty/lcms2': 'b' * 40}},
                'zlibNg': {'revision': 'c' * 40}}
        (source / 'native/dependencies.lock.json').write_text(json.dumps(lock))
        (source / 'editor.cpp').write_text('editable editor\n')
        editor_revision = snapshot(source)
        plan = root / 'plan'
        (plan / 'recipes').mkdir(parents=True)
        (plan / 'recipes/example.rb').write_text('original build recipe')
        cache = root / 'cache'
        cache.mkdir()
        records = []
        git_sources = []
        for name, target, revision in [('mupdf', 'mupdf', 'a' * 40),
                                       ('lcms2', 'mupdf/thirdparty/lcms2', 'b' * 40),
                                       ('zlib-ng', 'zlib-ng', 'c' * 40)]:
            data = tar({'source.c': 'editable ' + name}, prefix=name + '-' + revision + '/')
            (cache / (name + '-' + revision[:12])).write_bytes(data)
            git_sources.append({'name': name, 'revision': revision, 'repository': 'example/' + name,
                                'extractTo': target})
        for name, data, suffix in [('qt-example', tar({'source.cpp': 'complete upstream source'}), '.tar.gz'),
                                    ('build-patch', b'exact build patch\n', '.patch')]:
            digest = hashlib.sha256(data).hexdigest()
            (cache / (name + '-' + digest[:12])).write_bytes(data)
            records.append({'name': name, 'url': 'https://example.org/' + name + suffix, 'sha256': digest})
        (plan / 'manifest.json').write_text(json.dumps({'unresolved': [], 'archives': records,
            'gitSources': git_sources, 'editorRevision': editor_revision, 'engineRevision': engine_revision}))
        output = root / 'release/project-sources.zip'
        with patch.object(source_release, 'ROOT', source):
            source_release.assemble(plan, cache, output)
        return source, plan, cache, output, output.parent / 'dependency-sources'

    def test_project_only_restore_and_offline_native_dependencies_and_mirror_reuse(self):
        import source_release
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, plan, cache, output, dependencies = self.prepare(root)
            with zipfile.ZipFile(output) as archive:
                manifest = json.loads(archive.read('SOURCE-MANIFEST.json'))
                self.assertEqual(manifest['schema'], 2)
                self.assertFalse(any(n.startswith('upstream/') for n in archive.namelist()))
                self.assertEqual(len([n for n in archive.namelist() if n.startswith('patches/')]), 1)
                self.assertEqual(len([r for r in manifest['inputs'] if 'sourceFile' in r]), 5)
            editor = unpack(output, root / 'project-only')
            self.assertEqual((editor / 'editor.cpp').read_text(), 'editable editor\n')
            self.assertFalse((editor.parent / 'mupdf').exists())
            self.assertEqual(json.loads((editor / 'native/dependencies.lock.json').read_text())['mupdf']['revision'], 'a' * 40)
            editor = unpack(output, root / 'full', dependencies)
            lock = json.loads((editor / 'native/dependencies.lock.json').read_text())
            check_source(editor.parent / 'mupdf', lock['mupdf']['revision'])
            check_source(editor.parent / 'mupdf/thirdparty/lcms2', lock['mupdf']['submodules']['thirdparty/lcms2'])
            check_source(editor.parent / 'zlib-ng', lock['zlibNg']['revision'])
            self.assertEqual(len(list((editor.parent / 'patches').iterdir())), 1)
            previous = {p.name: p.stat().st_mtime_ns for p in dependencies.glob('*.tar.gz')}
            import shutil
            shutil.rmtree(cache)
            with patch.object(source_release, 'ROOT', source):
                source_release.assemble(plan, cache, output.with_name('next-release.zip'))
            self.assertEqual(previous, {p.name: p.stat().st_mtime_ns for p in dependencies.glob('*.tar.gz')})
            self.assertEqual(len(json.loads((dependencies / 'index.json').read_text())), 5)

    def test_missing_or_corrupt_external_dependency_fails_before_restoration(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, _, _, output, dependencies = self.prepare(root)
            with patch('unpack_source_release.urllib.request.urlopen') as network:
                with self.assertRaisesRegex(ValueError, 'Missing dependency source'):
                    unpack(output, root / 'missing', root / 'empty')
                network.assert_not_called()
            self.assertFalse((root / 'missing').exists())
            with zipfile.ZipFile(output) as archive:
                record = next(r for r in json.loads(archive.read('SOURCE-MANIFEST.json'))['inputs'] if r.get('extractTo'))
            (dependencies / record['sourceFile']).write_bytes(b'corrupted')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                unpack(output, root / 'corrupt', dependencies)
            self.assertFalse((root / 'corrupt').exists())

    def test_explicit_fetch_verifies_checksum_and_cleans_failed_download(self):
        from unittest.mock import patch
        from unpack_source_release import dependency_source
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            data = b'exact upstream source'
            record = {'name': 'example', 'sourceFile': 'source.tar.gz', 'url': 'https://example.org/source',
                      'sha256': hashlib.sha256(data).hexdigest()}
            with patch('unpack_source_release.urllib.request.urlopen', return_value=io.BytesIO(b'wrong source')):
                with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                    dependency_source(record, directory, True)
            self.assertEqual(list(directory.iterdir()), [])
            with patch('unpack_source_release.urllib.request.urlopen', return_value=io.BytesIO(data)):
                self.assertEqual(dependency_source(record, directory, True).read_bytes(), data)

    def test_corrupt_existing_mirror_is_not_overwritten(self):
        import source_release
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, plan, cache, output, dependencies = self.prepare(root)
            file = next(dependencies.glob('*.tar.gz'))
            file.write_bytes(b'corrupted')
            with patch.object(source_release, 'ROOT', source):
                with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                    source_release.assemble(plan, cache, output.with_name('rejected.zip'))
            self.assertEqual(file.read_bytes(), b'corrupted')
            self.assertFalse(output.with_name('rejected.zip').exists())
            self.assertFalse(output.with_name('rejected.partial').exists())


if __name__ == '__main__':
    unittest.main()
