import importlib.util
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('process_memory', Path(__file__).parents[1] / 'scripts/process_memory.py')
accounting = importlib.util.module_from_spec(spec)
spec.loader.exec_module(accounting)


class ProcessMemory(unittest.TestCase):
    @unittest.skipUnless(sys.platform in ('darwin', 'linux'), 'Supported process accounting')
    def test_self_has_real_memory_charge(self):
        sample = accounting.memory(os.getpid())
        self.assertTrue(sample)
        self.assertGreater(next(iter(sample.values())), 0)

    def test_linux_includes_swapped_proportional_pages(self):
        text = '1000-2000 ---p [rollup]\nRss: 10 kB\nPss: 7 kB\nSwapPss: 80 kB\n'
        with patch.object(accounting.sys, 'platform', 'linux'), patch.object(accounting.Path, 'read_text', return_value=text):
            self.assertEqual(accounting.memory(123), {'proportionalFootprintKiB': 87})

    def test_missing_process_is_unavailable_not_zero(self):
        with patch.object(accounting.sys, 'platform', 'linux'), patch.object(accounting.Path, 'read_text', side_effect=FileNotFoundError):
            self.assertEqual(accounting.memory(123), {})

    def test_unsupported_platform_is_unavailable(self):
        with patch.object(accounting.sys, 'platform', 'other'):
            self.assertEqual(accounting.memory(123), {})
