"""Packaging must reject stale, damaged, or wrong-platform engine runtimes."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from common import sha256
from validate_bundle import validate


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / 'bin').mkdir()
        (self.root / 'notices').mkdir()
        entries = []
        for name in ('texpresso', 'texpresso-xetex'):
            path = self.root / 'bin' / name
            path.write_bytes(b'fixture executable')
            path.chmod(0o755)
            entries.append({'path': 'bin/' + name, 'sha256': sha256(path)})
        self.manifest = {'engineRevision': 'pinned', 'platform': 'linux', 'architecture': 'aarch64',
                         'capabilities': {'previewProtocol': 1, 'codecs': ['png']}, 'files': entries}

    def check(self, system='linux'):
        (self.root / 'build-manifest.json').write_text(json.dumps(self.manifest))
        return validate(self.root, 'pinned', system, 'arm64')

    def test_macos_compact_runtime(self):
        self.manifest['platform'] = 'darwin'
        self.check('darwin')

    def test_macos_rejects_general_purpose_mupdf_dependencies(self):
        self.manifest['platform'] = 'darwin'
        libraries = self.root / 'lib'
        libraries.mkdir()
        for name in ('libmupdf.dylib', 'libtesseract.5.dylib', 'libleptonica.6.dylib',
                     'libmujs.dylib', 'libgumbo.4.dylib'):
            with self.subTest(name=name):
                path = libraries / name
                path.write_bytes(b'leftover library')
                with self.assertRaisesRegex(ValueError, 'compact static MuPDF'):
                    self.check('darwin')
                path.unlink()

    def test_windows_wsl_requires_supervisor_and_accepts_ntfs_modes(self):
        (self.root / 'build-manifest.json').write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, 'executable'):
            validate(self.root, 'pinned', 'linux', 'arm64', windows_wsl=True)
        helper = self.root / 'bin/texpresso-wsl'
        helper.write_bytes(b'WSL process supervisor')
        self.manifest['files'].append({'path': 'bin/texpresso-wsl', 'sha256': sha256(helper)})
        for path in (self.root / 'bin').iterdir(): path.chmod(0o644)
        (self.root / 'build-manifest.json').write_text(json.dumps(self.manifest))
        validate(self.root, 'pinned', 'linux', 'arm64', windows_wsl=True)

    def test_matching_runtime(self):
        self.assertEqual(self.check()['engineRevision'], 'pinned')

    def test_wrong_revision(self):
        self.manifest['engineRevision'] = 'newer'
        with self.assertRaisesRegex(ValueError, 'revision'):
            self.check()

    def test_wrong_platform(self):
        self.manifest['platform'] = 'darwin'
        with self.assertRaisesRegex(ValueError, 'platform'):
            self.check()

    def test_tampered_binary(self):
        (self.root / 'bin/texpresso').write_bytes(b'modified')
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.check()

    def test_escaping_manifest(self):
        self.manifest['files'][0]['path'] = '../outside'
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            self.check()

    def test_missing_worker(self):
        self.manifest['files'].pop()
        with self.assertRaisesRegex(ValueError, 'executable'):
            self.check()

    def test_requires_preview(self):
        self.manifest['capabilities']['codecs'] = ['svg']
        with self.assertRaisesRegex(ValueError, 'PNG'):
            self.check()
