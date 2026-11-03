import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import license_evidence
import source_release
from license_inventory import package_metadata


class LicenseEvidence(unittest.TestCase):
    def test_dictionary_notice_is_bound_to_exact_payload_and_members(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'NOTICE').write_text('GPL-3.0; Original Author')
            path = root / 'en_US.dic.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('en_US.dic', b'word')
                archive.writestr('en_US.aff', b'SET UTF-8')
            record = {'name': 'en_US.dic', 'license': 'GPL-3.0',
                      'members': [{'name': n, 'sha256': hashlib.sha256(b).hexdigest()}
                                  for n, b in [('en_US.dic', b'word'), ('en_US.aff', b'SET UTF-8')]],
                      'notices': [{'path': 'NOTICE'}]}
            with patch.object(license_evidence, 'ROOT', root), zipfile.ZipFile(path) as archive:
                self.assertEqual(license_evidence.dictionary_evidence(archive, [record]), record)
                record['members'][0]['sha256'] = '0' * 64
                with self.assertRaisesRegex(ValueError, 'Dictionary changed'):
                    license_evidence.dictionary_evidence(archive, [record])

    def test_old_homebrew_source_and_license_are_read_without_executing_ruby(self):
        with tempfile.TemporaryDirectory() as temp:
            prefix = Path(temp) / 'libb2/0.98.1'
            (prefix / '.brew').mkdir(parents=True)
            (prefix / '.brew/libb2.rb').write_text('class Libb2 < Formula\n  url "https://example.org/source.tar.gz"\n  sha256 "' + 'a' * 64 + '"\n  license "CC0-1.0"\nend\n')
            result = package_metadata(prefix)
            self.assertEqual(result['license'], 'CC0-1.0')
            self.assertEqual(result['sourceChecksums'][0]['checksumValue'], 'a' * 64)

    def test_missing_source_does_not_produce_partial_release_zip(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            plan = root / 'plan'
            plan.mkdir()
            (plan / 'manifest.json').write_text(json.dumps({'unresolved': [], 'gitSources': [],
                'archives': [{'name': 'required', 'url': 'https://example.org/input.tar.gz', 'sha256': 'a' * 64}]}))
            output = root / 'source.zip'
            with self.assertRaisesRegex(ValueError, 'Missing cached source'):
                source_release.assemble(plan, root / 'cache', output)
            self.assertFalse(output.exists())
            self.assertFalse(output.with_suffix('.partial').exists())

    def test_cached_source_with_wrong_checksum_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            record = {'name': 'input', 'url': 'https://example.org/input.tar.gz', 'sha256': 'a' * 64}
            (root / ('input-' + 'a' * 12)).write_bytes(b'wrong source')
            with self.assertRaisesRegex(ValueError, 'Cached source checksum mismatch'):
                source_release.fetch_record(record, root, False)


if __name__ == '__main__':
    unittest.main()
