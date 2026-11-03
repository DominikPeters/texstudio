"""Memory accounting for owned test processes, including macOS compression.

RSS alone can hide gigabytes compressed or swapped out by macOS. Footprints
are per-task charges; summing compiler checkpoints still double-counts COW.
"""
import ctypes
from functools import lru_cache
from pathlib import Path
import sys


class RusageInfoV0(ctypes.Structure):
    _fields_ = [('uuid', ctypes.c_uint8 * 16)] + [
        (name, ctypes.c_uint64) for name in (
            'user_time', 'system_time', 'pkg_idle_wkups', 'interrupt_wkups',
            'pageins', 'wired_size', 'resident_size', 'phys_footprint',
            'proc_start_abstime', 'proc_exit_abstime')]


@lru_cache(maxsize=1)
def darwin_usage():
    # Keep one dlopen/function binding during hours-long resource sampling.
    library = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    usage = library.proc_pid_rusage
    usage.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(RusageInfoV0)]
    usage.restype = ctypes.c_int
    return usage


def memory(pid):
    if sys.platform == 'darwin':
        info = RusageInfoV0()
        usage = darwin_usage()
        if usage(pid, 0, ctypes.byref(info)):
            return {}  # Exited or access denied; never substitute a false zero.
        return {'physicalFootprintKiB': info.phys_footprint // 1024}
    if sys.platform.startswith('linux'):
        try:
            values = dict(line.split(':', 1) for line in Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines()[1:])
            return {'proportionalFootprintKiB': int(values['Pss'].split()[0]) + int(values.get('SwapPss', '0').split()[0])}
        except (OSError, KeyError, ValueError):
            return {}
    return {}
