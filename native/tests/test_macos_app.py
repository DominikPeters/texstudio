import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from common import sha256
import deploy_macos
import notarize_app


class MacOSApp(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.app = Path(self.temp.name) / 'texstudio.app'
        self.runtime = self.app / 'Contents/Helpers/texpresso'
        self.metadata = self.app / 'Contents/Resources/texpresso'
        self.metadata.mkdir(parents=True)
        self.main = self.app / 'Contents/MacOS/texstudio'
        self.engine = self.runtime / 'bin/texpresso'
        self.worker = self.runtime / 'bin/texpresso-xetex'
        self.library = self.runtime / 'lib/example.dylib'
        self.framework = self.app / 'Contents/Frameworks/QtCore.framework'
        self.qt = self.framework / 'Versions/A/QtCore'
        for file in (self.main, self.engine, self.worker, self.library, self.qt):
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(bytes.fromhex('cffaedfe') + b'unsigned')
        self.manifest = self.metadata / 'build-manifest.json'
        self.manifest.write_text(json.dumps({'engineRevision': 'pinned', 'files': [
            {'path': str(p.relative_to(self.runtime)), 'sha256': sha256(p), 'bytes': p.stat().st_size}
            for p in (self.engine, self.worker, self.library)]}))

    def test_nested_signatures_then_manifest_then_outer_app(self):
        calls = []
        def command(args, **options):
            calls.append(args)
            if '--force' in args:
                target = args[-1]
                if target == self.app:
                    manifest = json.loads(self.manifest.read_text())
                    for entry in manifest['files']:
                        self.assertEqual(entry['sha256'], sha256(self.runtime / entry['path']))
                    self.assertIn('--entitlements', args)
                    self.assertEqual(manifest['signing']['teamId'], 'TEAM')
                elif target.is_file():
                    self.assertNotIn('--entitlements', args)
                    target.write_bytes(target.read_bytes() + b'-signed')
            return subprocess.CompletedProcess(args, 0, '', 'TeamIdentifier=TEAM\nAuthority=Developer ID Application: Test\nTimestamp=now\nCodeDirectory flags=0x10000(runtime)\n')
        with patch.object(notarize_app, 'imported_identity', return_value='IDENTITY'), patch.object(notarize_app, 'command', side_effect=command):
            notarize_app.sign_app(self.app, 'identity', 'KEYCHAIN')
        signed = [c[-1] for c in calls if '--force' in c]
        self.assertEqual(signed[-1], self.app)
        self.assertLess(signed.index(self.qt), signed.index(self.framework))
        self.assertLess(signed.index(self.framework), signed.index(self.app))
        self.assertTrue(any('--verify' in c and '--deep' in c and '--strict' in c for c in calls))
        self.assertFalse(self.main.read_bytes().endswith(b'-signed'), 'Outer app signing owns the main executable')

    def test_framework_aliases_do_not_duplicate_code_and_escaping_links_fail(self):
        current = self.framework / 'Versions/Current'
        current.symlink_to('A', target_is_directory=True)
        alias = self.framework / 'QtCore'
        alias.symlink_to('Versions/Current/QtCore')
        files = deploy_macos.macho_files(self.app)
        self.assertIn(self.qt, files)
        self.assertNotIn(alias, files)
        outside = Path(self.temp.name) / 'external'
        outside.write_text('external')
        (self.app / 'Contents/escape').symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'escapes application'):
            deploy_macos.macho_files(self.app)

    def test_manifest_cannot_escape_engine_runtime(self):
        self.manifest.write_text(json.dumps({'files': [{'path': '../../../../external'}]}))
        with self.assertRaisesRegex(ValueError, 'escapes'):
            deploy_macos.refresh_engine_manifest(self.app)

    def test_dependency_verification_distinguishes_install_id_from_host_dependency(self):
        def run(args, **options):
            if args[1] == '-l': return ''
            if args[1] == '-D': return 'file:\n/opt/homebrew/lib/example.dylib'
            return 'file:\n /opt/homebrew/lib/example.dylib (compatibility version 1)\n /usr/lib/libSystem.B.dylib (compatibility version 1)'
        with patch.object(deploy_macos, 'run', side_effect=run):
            deploy_macos.verify_dependencies(self.app)
        def external(args, **options):
            value = run(args, **options)
            return value + '\n /opt/homebrew/lib/undeployed.dylib (compatibility version 1)' if args[1] == '-L' else value
        with patch.object(deploy_macos, 'run', side_effect=external), self.assertRaisesRegex(ValueError, 'External dependency'):
            deploy_macos.verify_dependencies(self.app)


if __name__ == '__main__':
    unittest.main()
