#!/usr/bin/env python3
"""Run the opt-in full-editor Qt soak, sample owned processes, and check clean output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import statistics
import subprocess
import time


def processes():
    result = subprocess.run(['ps', '-axo', 'pid=,ppid=,pgid=,rss=,stat='],
                            capture_output=True, text=True, check=True, timeout=3)
    return [{'pid': int(p), 'ppid': int(parent), 'pgid': int(group), 'rssKiB': int(rss), 'state': state}
            for line in result.stdout.splitlines() if len(parts := line.split()) == 5
            for p, parent, group, rss, state in [parts]]


def owned(rows, pid, groups):
    groups.intersection_update(r['pgid'] for r in rows)
    pids = {pid}
    while True:
        children = {r['pid'] for r in rows if r['ppid'] in pids}
        if children <= pids:
            break
        pids |= children
    groups.update(r['pgid'] for r in rows if r['pid'] in pids)
    return [r for r in rows if r['pid'] in pids or r['pgid'] in groups]


def tool(name):
    paths = os.environ.get('PATH', '') + os.pathsep + os.pathsep.join([
        '/Library/TeX/texbin', '/opt/homebrew/bin', '/usr/local/bin', '/usr/bin'])
    found = shutil.which(name, path=paths)
    if not found:
        raise RuntimeError(f'Required independent oracle tool missing: {name}')
    return found


def oracle(folder, ui):
    clean = folder / 'oracle'
    shutil.copytree(folder / 'final', clean)
    checked = []
    for root in sorted({p['root'] for p in ui['probes']}):
        for iteration in (1, 2):
            result = subprocess.run([tool('xelatex'), '-no-shell-escape', '-interaction=nonstopmode',
                                     '-halt-on-error', '-synctex=1', root], cwd=clean,
                                    capture_output=True, timeout=120)
            (clean / f'{root}-{iteration}.log.txt').write_bytes(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'Independent XeLaTeX failed for {root}; see oracle logs')
        pdf = clean / Path(root).with_suffix('.pdf')
        text = subprocess.check_output([tool('pdftotext'), '-layout', '-enc', 'UTF-8', str(pdf), '-'], timeout=30).decode()
        pages = text.split('\f')
        for probe in [p for p in ui['probes'] if p['root'] == root]:
            owners = [i for i, page in enumerate(pages) if probe['token'] in page]
            if owners != [probe['page']]:
                raise RuntimeError(f"Clean PDF ownership mismatch for {probe['token']}: {owners} vs Qt {probe['page']}")
            query = f"{probe['line1']}:1:{clean / probe['file']}"
            sync = subprocess.check_output([tool('synctex'), 'view', '-i', query, '-o', str(pdf)], timeout=30).decode()
            sync_pages = sorted({int(p) - 1 for p in re.findall(r'^Page:(\d+)', sync, re.M)})
            (clean / f"{probe['token']}-synctex.txt").write_text(sync)
            if probe['page'] not in sync_pages:
                raise RuntimeError(f"Clean SyncTeX ownership mismatch for {probe['token']}: {sync_pages}")
            checked.append({**probe, 'pdfPages': owners, 'synctexPages': sync_pages})
    return checked


def summary(ui, samples):
    phases = {}
    for row in ui['inputs']:
        phases.setdefault(row['phase'], []).append(row)
    timing = []
    for name, rows in phases.items():
        latencies = sorted(r['paintCompletedMs'] - r['appliedMs'] for r in rows if 'paintCompletedMs' in r)
        lateness = max(r['latenessMs'] for r in rows)
        timing.append({'phase': name, 'inputs': len(rows), 'currentPaintObserved': len(latencies),
                       'withoutCurrentPaintObservation': len(rows) - len(latencies),
                       'maxInputLatenessMs': lateness, 'validCadence': lateness <= 20,
                       'observedMedianMs': statistics.median(latencies) if latencies else None,
                       'observedP95Ms': latencies[min(len(latencies) - 1, int(.95 * len(latencies)))] if latencies else None,
                       'observedMaxMs': max(latencies) if latencies else None})
    return {'phases': timing, 'resourceSamples': len(samples),
            'peakOwnedTasks': max((len(s['processes']) for s in samples), default=0),
            'peakOwnedRssKiB': max((sum(p['rssKiB'] for p in s['processes']) for s in samples), default=0),
            'resourceAccounting': 'RSS sum includes shared/COW pages and harness state; not unique RAM or PSS. Finite run does not prove bounded long-term memory.',
            'latencyAccounting': ui['endpoint'] + '; observed subset only; superseded/unobserved inputs remain counted. Cadence misses invalidate phase timing claims.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True, help='Debug TeXstudio executable (not the .app directory)')
    parser.add_argument('--output', type=Path, required=True, help='A new artifact directory')
    parser.add_argument('--seconds', type=int, default=600, help='Minimum workload duration; completes the final cycle (10–3600)')
    parser.add_argument('--platform', default='offscreen', help='Qt platform: offscreen, cocoa, or xcb')
    parser.add_argument('--pages', type=int, choices=(30, 100), default=30)
    args = parser.parse_args()
    if not 10 <= args.seconds <= 3600:
        parser.error('--seconds must be between 10 and 3600')
    app = args.app.resolve(); folder = args.output.resolve()
    if folder.exists():
        parser.error('--output must not exist; each run preserves its own evidence')
    for name in ('xelatex', 'synctex', 'pdftotext'):
        tool(name)
    # Fail before launching any workers if process sampling is unavailable.
    processes()
    folder.mkdir(parents=True)
    env = dict(os.environ, QT_QPA_PLATFORM=args.platform, SDL_VIDEODRIVER='dummy',
               TEXPRESSO_TEST_UI_SOAK='1', TEXPRESSO_UI_SOAK_SECONDS=str(args.seconds),
               TEXPRESSO_UI_SOAK_PAGES=str(args.pages),
               TEXPRESSO_UI_SOAK_ARTIFACTS=str(folder), QTEST_FUNCTION_TIMEOUT=str((args.seconds + 300) * 1000))
    started = time.monotonic(); samples = []; groups = set()
    result = {'passed': False, 'appSha256': hashlib.sha256(app.read_bytes()).hexdigest(),
              'requestedSeconds': args.seconds, 'targetPages': args.pages, 'platform': args.platform}
    child = None
    try:
        with (folder / 'app.log').open('wb') as log, (folder / 'processes.jsonl').open('w') as resource_log:
            child = subprocess.Popen([str(app), '--auto-tests', '--start-always', '--no-session',
                                      '--config', str(folder / 'config')], env=env, stdout=log, stderr=subprocess.STDOUT,
                                     start_new_session=True)
            while child.poll() is None:
                elapsed = time.monotonic() - started
                if elapsed > args.seconds + 300:
                    raise RuntimeError('Application test exceeded its hard deadline')
                if elapsed > 30 and not (folder / 'heartbeat.json').exists():
                    raise RuntimeError('Soak did not start: use a Debug build with application tests enabled; see app.log')
                row = {'atSeconds': elapsed, 'processes': owned(processes(), child.pid, groups)}
                try:
                    row['ui'] = json.loads((folder / 'heartbeat.json').read_text())
                except (FileNotFoundError, json.JSONDecodeError):
                    pass
                samples.append(row); resource_log.write(json.dumps(row) + '\n'); resource_log.flush()
                time.sleep(1)
        leftovers = []
        for _ in range(50):
            leftovers = owned(processes(), child.pid, groups)
            if not leftovers:
                break
            time.sleep(.1)
        result['leftoverProcesses'] = leftovers
        if child.returncode or leftovers:
            raise RuntimeError(f'Application failed or left owned processes: exit={child.returncode}; see app.log/processes.jsonl')
        ui = json.loads((folder / 'ui-result.json').read_text())
        if not ui['passed']:
            raise RuntimeError('Qt soak assertions failed; see app.log and ui-result.json')
        result.update(summary(ui, samples)); result['oracle'] = oracle(folder, ui)
        result.update(passed=True, inputs=len(ui['inputs']), elapsedSeconds=time.monotonic() - started,
                      requestedSeconds=args.seconds, platform=args.platform)
    except Exception as error:
        result['error'] = str(error)
    finally:
        if child and (child.poll() is None or not result['passed']):
            live = owned(processes(), child.pid, groups)
            cleanup_groups = {p['pgid'] for p in live}
            for group in cleanup_groups:
                try:
                    os.killpg(group, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                for group in cleanup_groups:
                    try:
                        os.killpg(group, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                child.wait(timeout=5)
            # A frontend may have exited before its own worker group. Reap the
            # app and terminate only groups still tracked as belonging to it.
            time.sleep(.2)
            for group in {p['pgid'] for p in owned(processes(), child.pid, groups)}:
                try:
                    os.killpg(group, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
