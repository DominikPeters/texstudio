from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from renderer_evidence import collect


class RendererEvidence(unittest.TestCase):
    def test_only_retained_font_payloads_are_recorded_and_unknown_static_code_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            font = root / 'resources/fonts/droid/font.ttf'
            font.parent.mkdir(parents=True)
            font.write_bytes(b'exact retained font payload')
            font.with_name('unused.ttf').write_bytes(b'unlinked font payload')
            font.with_name('NOTICE').write_text('Apache-2.0; original copyright')
            frontend = root / 'frontend'
            frontend.write_bytes(b'linked code\0exact retained font payload\0other code')
            profile = root / 'profile.mk'
            profile.write_text('tofu := yes')
            directory = root / 'build/vscode'
            directory.mkdir(parents=True)
            for name in ('libmupdf.a', 'libmupdf-third.a'):
                (directory / name).write_bytes(b'archive input')
            with patch('renderer_evidence.run', return_value='__.SYMDEF SORTED\ncmsalpha.o\n'):
                evidence, notices = collect(frontend, root, profile, 'source-pin')
            self.assertEqual([f['source'] for f in evidence['retainedFonts']], ['resources/fonts/droid/font.ttf'])
            self.assertIn('mupdf-font-droid-NOTICE', notices)
            with patch('renderer_evidence.run', return_value='cmsalpha.o\nnew-library.o\n'):
                with self.assertRaisesRegex(ValueError, 'third-party contents changed'):
                    collect(frontend, root, profile, 'source-pin')
