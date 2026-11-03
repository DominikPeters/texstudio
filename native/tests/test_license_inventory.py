import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from license_inventory import first, spdx_records, notice_files, package_metadata


class LicenseInventory(unittest.TestCase):
    def test_spdx_multiline_does_not_create_fake_package(self):
        records = spdx_records('''SPDXVersion: SPDX-2.3
PackageName: Core
SPDXID: SPDXRef-Core
PackageCopyrightText: <text>Original author
PackageName: this is notice text, not a new package
</text>
PackageLicenseConcluded: LGPL-3.0-only OR GPL-3.0-only
FileName: ./lib/QtCore.framework/Versions/A/QtCore
SPDXID: SPDXRef-CoreFile
Relationship: SPDXRef-Core CONTAINS SPDXRef-CoreFile
''')
        self.assertEqual(len(records), 3)
        self.assertIn('PackageName: this is notice text', first(records[1], 'PackageCopyrightText'))
        self.assertEqual(first(records[1], 'PackageLicenseConcluded'), 'LGPL-3.0-only OR GPL-3.0-only')

    def test_collects_lowercase_notices_and_preserves_relative_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            prefix = Path(temp) / 'hunspell/1.7'
            prefix.mkdir(parents=True)
            for name in ['license.hunspell', 'AUTHORS', 'COPYING', 'README', 'share/doc/example/NOTICE']:
                file = prefix / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text('notice')
            self.assertEqual({str(p.relative_to(prefix)) for p in notice_files(prefix)},
                             {'license.hunspell', 'AUTHORS', 'COPYING', 'share/doc/example/NOTICE'})
            self.assertEqual(package_metadata(prefix)['license'], 'NOASSERTION')


if __name__ == '__main__':
    unittest.main()
