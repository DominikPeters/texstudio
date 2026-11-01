#!/usr/bin/env python3
"""Verify a packaged macOS app without activating it or touching its profile."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from deploy_macos import verify_dependencies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True, help='Installed or staged .app bundle')
    parser.add_argument('--client-build', type=Path, required=True, help='Built standalone Qt tests')
    parser.add_argument('--output', type=Path, required=True, help='New directory for logs and result.json')
    parser.add_argument('--tests', default='^texpresso-(rendering|diagnostics|scrolling|links)$',
                        help='CTest regex for the relevant background suites')
    args = parser.parse_args()
    app, build, output = args.app.resolve(), args.client_build.resolve(), args.output.resolve()
    main_exe = app / 'Contents/MacOS/texstudio'
    engine = app / 'Contents/Helpers/texpresso/bin/texpresso'
    if not main_exe.is_file() or not engine.is_file() or not (build / 'CTestTestfile.cmake').is_file():
        parser.error('Supply a packaged app and a configured standalone Qt test build')
    if output.exists():
        parser.error('--output must be a new directory')
    output.mkdir(parents=True)
    # Do not inherit GUI plugin overrides, private paper opt-ins, or soak flags.
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('QT_', 'DYLD_', 'TEXPRESSO_TEST_', 'TEXPRESSO_UI_', 'TEXSTUDIO_TEST_'))}
    env.update(QT_QPA_PLATFORM='offscreen', SDL_VIDEODRIVER='dummy',
               TEXPRESSO_TEST_ENGINE=str(engine))
    result = {'passed': False, 'app': str(app), 'platform': 'offscreen', 'tests': args.tests}

    def run(name, command, timeout=120):
        completed = subprocess.run([str(p) for p in command], env=env, cwd=output,
                                   capture_output=True, timeout=timeout)
        (output / (name + '.log')).write_bytes(completed.stdout + completed.stderr)
        completed.check_returncode()
        return completed.stdout.decode()

    try:
        run('signature', ['codesign', '--verify', '--deep', '--strict', app])
        verify_dependencies(app)
        with tempfile.TemporaryDirectory(prefix='texstudio-verify-profile-') as profile:
            run('version', [main_exe, '--config', profile, '--no-session', '--start-always', '--version'])
        capabilities = json.loads(run('capabilities', [engine, '-capabilities']))
        manifest = json.loads((app / 'Contents/Resources/texpresso/build-manifest.json').read_text())
        expected = manifest['capabilities']
        if capabilities != expected:
            raise ValueError('Packaged engine capabilities differ from its build manifest')
        result.update(engineRevision=manifest['engineRevision'], capabilities=capabilities)
        run('qt-tests', ['ctest', '--test-dir', build, '--output-on-failure',
                         '--no-tests=error', '--timeout', '120', '-R', args.tests], timeout=900)
        result['passed'] = True
    except Exception as error:
        result['error'] = str(error)
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
