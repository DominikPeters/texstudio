#!/usr/bin/env python3
"""Run dictionary integration checks offscreen with disposable settings/cache."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True, help='Real, test-enabled executable (not the Finder launcher)')
    parser.add_argument('--output', type=Path, required=True, help='New directory for logs and result.json')
    args = parser.parse_args()
    app, output = args.app.resolve(), args.output.resolve()
    if not app.is_file() or output.exists():
        parser.error('Supply a real executable and a new output directory')
    output.mkdir(parents=True)
    result = {'passed': False, 'app': str(app), 'platform': 'offscreen'}
    try:
        with tempfile.TemporaryDirectory(prefix='texstudio-dictionaries-') as directory:
            root = Path(directory)
            profile, cache = root / 'profile', root / 'cache'
            profile.mkdir()
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(('QT_', 'DYLD_', 'TEXPRESSO_TEST_', 'TEXPRESSO_UI_', 'TEXSTUDIO_TEST_'))}
            env.update(QT_QPA_PLATFORM='offscreen', SDL_VIDEODRIVER='dummy',
                       TEXSTUDIO_TEST_DICTIONARIES='1', TEXSTUDIO_TEST_DICTIONARY_CACHE=str(cache))
            snapshots = []
            # The second process verifies that selected packaged languages
            # reuse their cache rather than extracting again on each launch.
            for run in range(2):
                completed = subprocess.run([str(app), '--auto-tests', '--start-always', '--no-session',
                                            '--config', str(profile)], cwd=root, env=env,
                                           capture_output=True, timeout=120)
                log = completed.stdout + completed.stderr
                (output / f'editor-{run + 1}.log').write_bytes(log)
                completed.check_returncode()
                if b'PASS   : TexStudioTest::compressedDictionaries()' not in log:
                    raise RuntimeError('Dictionary tests did not run; use a Debug build with internal tests enabled')
                snapshots.append({str(p.relative_to(cache)): p.stat().st_mtime_ns
                                  for p in cache.rglob('ready.json')})
            if snapshots[0] != snapshots[1]:
                raise RuntimeError('Re-extracted an unchanged dictionary cache across launches')
            result.update(passed=True, launches=2, cachedPackagedDictionaries=len(snapshots[0]))
    except Exception as error:
        result['error'] = str(error)
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
