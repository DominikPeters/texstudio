import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('pack_dictionaries', Path(__file__).parents[1] / 'scripts/pack_dictionaries.py')
packing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packing)


class DictionaryPackaging(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.source = Path(temp.name) / 'source'
        self.source.mkdir()
        self.output = Path(temp.name) / 'output'
        for name, data in {'en.dic': b'1\nhello\n', 'en.aff': b'SET UTF-8\n',
                           'th_en.dat': b'UTF-8\nhello|1\n(noun)|greeting\n',
                           'th_en.idx': b'index', 'LICENSE.txt': b'license', 'en.stopWords': b'the'}.items():
            (self.source / name).write_bytes(data)

    def test_round_trip_and_determinism(self):
        packing.pack(self.source, self.output)
        before = {p.name: p.read_bytes() for p in self.output.iterdir()}
        packing.pack(self.source, self.output)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.output.iterdir()})
        for archive, members in [('en.dic.zip', ['en.dic', 'en.aff']), ('th_en.dat.zip', ['th_en.dat', 'th_en.idx'])]:
            with zipfile.ZipFile(self.output / archive) as zipped:
                self.assertEqual(set(zipped.namelist()), set(members))
                for member in members:
                    self.assertEqual(zipped.read(member), (self.source / member).read_bytes())
        self.assertEqual((self.output / 'LICENSE.txt').read_bytes(), b'license')
        self.assertEqual((self.output / 'en.stopWords').read_bytes(), b'the')
        self.assertFalse((self.output / 'en.aff').exists())
        self.assertEqual((self.output / 'en.dic.zip').stat().st_mode & 0o777, 0o644)

    def test_requires_affix_file(self):
        (self.source / 'en.aff').unlink()
        with self.assertRaisesRegex(ValueError, 'affix'):
            packing.pack(self.source, self.output)

    def test_prunes_only_replaced_dictionary_files(self):
        packing.pack(self.source, self.output)
        for path in self.source.iterdir():
            (self.output / path.name).write_bytes(path.read_bytes())
        packing.prune_bundle(self.source, self.output)
        self.assertFalse((self.output / 'en.dic').exists())
        self.assertFalse((self.output / 'en.aff').exists())
        self.assertFalse((self.output / 'th_en.idx').exists())
        self.assertTrue((self.output / 'LICENSE.txt').exists())


if __name__ == '__main__':
    unittest.main()
