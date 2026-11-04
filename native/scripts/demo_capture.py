#!/usr/bin/env python3
"""Capture the landing-page demo animation from the real editor, offscreen.

Copies the projects in native/demo/ into a new output directory, runs the
opt-in `liveDemoCapture` application test with offscreen widgets and a
disposable profile, and encodes the grabbed window frames into a lossless
animated WebP (each frame stores only the changed rectangle), optionally an
APNG, plus a still PNG for reduced-motion visitors.

Requires a Debug build with application tests and the bundled engine, TeX Live,
and Pillow.
"""
import argparse
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import signal
import subprocess
import time

from ui_soak import owned, processes

REPO = Path(__file__).resolve().parents[2]
DEMO = REPO / 'native' / 'demo'
DEFAULT_APP = REPO / 'build-cursor-app' / 'texstudio.app' / 'Contents' / 'MacOS' / 'texstudio'

HOLD_MS = 2200      # pause at the end of each scene
SEED = 7            # fixed, so every run produces the same timing
MODEL_TIMED_KEYS = {'Home', 'End'}  # recorded gaps for these are not used
# Recorded gaps are played faster: up to TEMPO_PIVOT ms unchanged, longer ones
# compressed by a power curve, then all scaled. The rhythm keeps its shape.
TEMPO_PIVOT, TEMPO_EXPONENT, TEMPO_SCALE = 150, 0.6, 0.65
SELECTION_LOOK_MS = 800   # minimum pause between selecting and typing over


def tempo(ms):
    if ms > TEMPO_PIVOT:
        ms = TEMPO_PIVOT * (ms / TEMPO_PIVOT) ** TEMPO_EXPONENT
    return int(ms * TEMPO_SCALE)


# Capture profile: a slightly larger editor font; no vertical formatting toolbar.
PROFILE = '''[texmaker]
Editor\\Font%20Size=13
ToolBar\\CentralVisible=false
'''
TOOLBAR_ICON_SIZE = 16   # TeXstudio's minimum; the default is 24


class Typist:
    """Inter-keystroke delays of a fluent typist (a simple model, not fitted data).

    Delays are log-normal with a median near 115 ms, faster inside words,
    slower for shifted LaTeX symbols, with pauses at word and sentence
    boundaries and occasional hesitations. Arrow-key taps are a little slower
    than letters; a deliberate selection is followed by a visible pause.
    """

    def __init__(self, seed=SEED):
        self.rng = random.Random(seed)

    def key(self, prev, c):
        r = self.rng
        delay = r.lognormvariate(math.log(115), 0.38)
        if prev and prev.isalpha() and c.isalpha():
            delay *= 0.85
        if c in '\\{}$_^[]()=' or c.isupper():
            delay *= 1.6
        if prev == ' ':
            delay += r.uniform(20, 120)
            if r.random() < 0.08:
                delay += r.uniform(350, 800)
        if prev and prev in '.,:;':
            delay += r.uniform(200, 450)
        if '\n' in (prev, c):
            delay += r.uniform(250, 500)
        return int(min(max(delay, 45), 1500))

    def arrow(self, repeat):
        # Repeated taps of the same arrow are quicker than the first one.
        median = 150 if repeat else 320
        return int(min(max(self.rng.lognormvariate(math.log(median), 0.3), 70), 900))

    def between(self, low, high):
        return int(self.rng.uniform(low, high))


class Recording:
    """Keystroke gaps recorded with record_typing.py, replayed in order."""

    def __init__(self, path):
        data = json.loads(Path(path).read_text())
        self.keys = data['keys']
        self.index = 0

    def take(self, token):
        if self.index >= len(self.keys):
            raise ValueError(f'Recording ends before key {token!r}; record it again')
        row = self.keys[self.index]
        if row['key'] != token:
            raise ValueError(f'Recording key {self.index} is {row["key"]!r}, script expects {token!r}; record it again')
        self.index += 1
        return row['delayMs']


class Script:
    """Builds the action list for the application test.

    Every step is a real key press sent to the editor. A frame is captured
    after each step, once the preview has settled; its display time is the
    pause before the next key, so the animation plays at the typist's pace.
    Each key is also listed as a token, so a recording of a person typing the
    same keys can supply the timing instead of the model.
    """

    def __init__(self, recording=None, speedup=True):
        self.speedup = speedup
        self.actions = []
        self.frames = 0
        self.marks = {}
        self.last = None
        self.typist = Typist()
        self.recording = recording
        self.tokens = []        # [{'key', 'scene', 'first'}] for the recorder
        self.scene_name = None
        self.first_in_scene = False
        self.last_key = None

    def op(self, op, **fields):
        self.actions.append({'op': op, **fields})

    def wait(self, ms):
        self.last['durationMs'] += int(ms)

    def step(self, *ops):
        for op, fields in ops:
            self.op(op, **fields)
        self.last = {'op': 'frame', 'durationMs': 0}
        self.actions.append(self.last)
        self.frames += 1

    def scene(self, name, file, **view):
        self.scene_name = name
        self.first_in_scene = True
        self.op('show', file=file, **view)

    def snapshot(self):
        self.step()

    def mark(self, name):
        self.marks[name] = self.frames - 1

    def keystroke(self, token, model_ms, *ops, min_ms=0):
        """One key press: wait the recorded (or modelled) gap, press, capture."""
        first = self.first_in_scene
        self.tokens.append({'key': token, 'scene': self.scene_name, 'first': first})
        delay = model_ms
        if self.recording:
            recorded = self.recording.take(token)
            # The first key of a scene follows a scripted pause. Home and End
            # are Ctrl-A/Ctrl-E in a terminal, so their recorded gaps measure
            # finding those shortcuts, not pressing a key in the editor.
            if not first and token not in MODEL_TIMED_KEYS:
                delay = tempo(recorded) if self.speedup else recorded
        delay = max(delay, min_ms)
        self.first_in_scene = False
        self.wait(delay)
        self.step(*ops)

    def type(self, text):
        """Type characters one key at a time; TeXstudio handles auto-closing."""
        prev = ''
        for c in text:
            self.keystroke(c, self.typist.key(prev, c), ('type', {'text': c}))
            prev = c
        self.last_key = None

    def press(self, key, times=1, shift=False, pause=None):
        for i in range(times):
            repeat = i > 0 or self.last_key == (key, shift)
            model = pause if (pause is not None and i == 0) else self.typist.arrow(repeat)
            self.keystroke(('Shift+' if shift else '') + key, model, ('key', {'key': key, 'shift': shift}))
            self.last_key = (key, shift)

    def complete(self, typed, pause=(450, 700)):
        """Type the start of a command, read the completion list, accept it."""
        self.type(typed)
        self.press('Return', pause=self.typist.between(*pause))
        self.last_key = None

    def overtype(self, char, direction):
        """Select one character with Shift+arrow, look at it, type over it."""
        self.press(direction, shift=True, pause=self.typist.between(350, 550))
        self.keystroke(char, self.typist.between(SELECTION_LOOK_MS, 1100), ('type', {'text': char}),
                       min_ms=SELECTION_LOOK_MS)
        self.last_key = None


def build_script(width, height, recording=None, speedup=True, dark=False):
    s = Script(recording, speedup)
    for name in ('article.tex', 'slides.tex', 'figure.tex'):
        s.op('open', path=f'project/{name}')
    s.op('closeOthers')

    # Scene 1: an article; a sentence, then a displayed formula. TeXstudio
    # closes \[ with \] itself; two Returns and Up leave an empty line inside.
    a = 'project/article.tex'
    s.scene('article', a, zoom='fitToWidth', continuous=True)
    s.op('find', file=a, text='n \\in \\mathbb{Z}.\n\\]\n', mode='after')
    s.snapshot(); s.wait(1200)
    s.type('If $f$ is smooth, the series converges uniformly:\n')
    s.type('\\[\n\n')
    s.press('Up', pause=350)
    s.type('f(x) = \\sum_{n=-')
    s.complete('\\inft')
    s.type('}^{')
    s.complete('\\inft')
    s.type('} c_n e^{inx}.')
    s.mark('poster'); s.wait(HOLD_MS)

    # Scene 2: a Beamer frame; add bullet points.
    b = 'project/slides.tex'
    s.scene('slides', b, zoom='fitToWindow', continuous=False)
    s.op('find', file=b, text='\\item Sort each half recursively.', mode='after')
    s.snapshot(); s.wait(900)
    s.type('\n\\item Merge the sorted halves.')
    s.wait(400)
    s.type('\n\\item Running time: $O(n \\log n)$.')
    s.wait(HOLD_MS)

    # Scene 3: a TikZ figure. The cursor starts at the top; arrow keys reach
    # vertex C at (1.6,2.4) on line 8, single digits are selected and retyped,
    # then the fill is made darker on the next line.
    f = 'project/figure.tex'
    s.scene('figure', f, zoom='fitToWindow', continuous=False)
    s.op('find', file=f, text='\\documentclass', mode='at')
    s.snapshot(); s.wait(900)
    s.press('Down', 7)
    s.press('End')
    s.press('Left', 3)              # before the 4 in (1.6,2.4);
    s.overtype('6', 'Right')        # (1.6,2.6)
    s.overtype('9', 'Left')         # (1.6,2.9)
    s.press('Left', 7)              # before the 1
    s.overtype('2', 'Right')        # (2.6,2.9)
    s.overtype('3', 'Left')         # (3.6,2.9): too far right
    s.overtype('2', 'Left')         # back to (2.6,2.9)
    s.press('Down')                 # \fill[blue!15] ... on the next line
    s.press('Home')
    s.press('Right', 11)            # before the 1 of 15
    s.overtype('3', 'Right')        # blue!35
    s.wait(HOLD_MS)

    spec = {'schema': 1, 'window': {'width': width, 'height': height}, 'hideMenuBar': True,
            'toolbarIconSize': TOOLBAR_ICON_SIZE, 'darkPalette': dark,
            'uncheckActions': ['main/view/show/outputview', 'main/view/show/structureview'],
            'actions': s.actions}
    return spec, s.marks, s.tokens


def stop(child, groups):
    if child.poll() is None:
        for group in {p['pgid'] for p in owned(processes(), child.pid, groups)}:
            try:
                os.killpg(group, signal.SIGTERM)
            except ProcessLookupError:
                pass
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill(); child.wait(timeout=5)
    time.sleep(.2)
    for group in {p['pgid'] for p in owned(processes(), child.pid, groups)}:
        try:
            os.killpg(group, signal.SIGKILL)
        except ProcessLookupError:
            pass


def capture(app, folder, spec, scale, timeout):
    (folder / 'project').mkdir(parents=True)
    (folder / 'config').mkdir()
    (folder / 'config' / 'texstudio.ini').write_text(PROFILE)
    for source in DEMO.glob('*.tex'):
        shutil.copy2(source, folder / 'project' / source.name)
    actions = folder / 'actions.json'
    actions.write_text(json.dumps(spec, indent=1) + '\n')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', SDL_VIDEODRIVER='dummy', QT_SCALE_FACTOR=str(scale),
               TEXPRESSO_TEST_DEMO_CAPTURE='1', TEXPRESSO_DEMO_OUTPUT=str(folder),
               TEXPRESSO_DEMO_ACTIONS=str(actions), QTEST_FUNCTION_TIMEOUT=str(timeout * 1000),
               PATH=os.environ.get('PATH', '') + os.pathsep + '/Library/TeX/texbin')
    groups = set()
    with (folder / 'app.log').open('wb') as log:
        child = subprocess.Popen([str(app), '--auto-tests', '--start-always', '--no-session',
                                  '--config', str(folder / 'config')], env=env, stdout=log,
                                 stderr=subprocess.STDOUT, start_new_session=True)
        started = time.monotonic()
        try:
            while child.poll() is None:
                if time.monotonic() - started > timeout + 60:
                    raise RuntimeError('Capture exceeded its deadline; see app.log')
                owned(processes(), child.pid, groups)
                time.sleep(.5)
        finally:
            stop(child, groups)
    if not (folder / 'frames.json').exists():
        raise RuntimeError('No frames were written: use a Debug build with application tests; see app.log')
    if child.returncode:
        raise RuntimeError(f'The editor exited with status {child.returncode} after capturing; see app.log')
    return json.loads((folder / 'frames.json').read_text())['frames']


def encode(folder, frames, marks, width, apng, name='demo'):
    from PIL import Image

    images, durations = [], []
    for row in frames:
        image = Image.open(folder / 'frames' / row['file']).convert('RGB')
        if width and image.width != width:
            image = image.resize((width, round(image.height * width / image.width)), Image.LANCZOS)
        if images and image.tobytes() == images[-1].tobytes():
            durations[-1] += row['durationMs']  # unchanged frame: extend the previous one
            continue
        images.append(image); durations.append(row['durationMs'])
    out = folder / 'web'
    out.mkdir()
    first, rest = images[0], images[1:]
    webp_tool = shutil.which('img2webp')
    if webp_tool:
        # libwebp's own tool at maximum effort: still lossless, about a third
        # smaller than Pillow's default (2.4 vs 1.5 MB for the demo).
        work = folder / 'web-frames'
        shutil.rmtree(work, ignore_errors=True); work.mkdir()
        cmd = [webp_tool, '-loop', '0', '-lossless', '-m', '6', '-q', '100']
        for k, (image, duration) in enumerate(zip(images, durations)):
            path = work / f'{k:04d}.png'
            image.save(path)
            cmd += ['-d', str(max(duration, 1)), str(path)]
        subprocess.run(cmd + ['-o', str(out / f'{name}.webp')], check=True, capture_output=True)
        shutil.rmtree(work)
    else:
        first.save(out / f'{name}.webp', save_all=True, append_images=rest, duration=durations, loop=0,
                   lossless=True, quality=100, method=6)
    if apng:  # larger: the changed rectangle spans both editor and preview
        first.save(out / f'{name}.png', save_all=True, append_images=rest, duration=durations, loop=0,
                   default_image=False, optimize=True)
    poster = Image.open(folder / 'frames' / frames[marks['poster']]['file']).convert('RGB')
    if width and poster.width != width:
        poster = poster.resize((width, round(poster.height * width / poster.width)), Image.LANCZOS)
    poster.save(out / f'{name}-still.png', optimize=True)
    return {'frames': len(images), 'durationMs': sum(durations),
            'width': first.width, 'height': first.height,
            'bytes': {p.name: p.stat().st_size for p in sorted(out.iterdir())}}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--app', type=Path, default=DEFAULT_APP, help='Debug TeXstudio executable (not the .app directory)')
    parser.add_argument('--output', type=Path, required=True, help='A new directory for frames, logs and encoded files')
    parser.add_argument('--window', default='1200x760', help='Window size in logical pixels')
    parser.add_argument('--scale', type=float, default=2, help='Device pixel ratio of the captured frames')
    parser.add_argument('--encode-width', type=int, default=0, help='Resize frames to this width before encoding (0: keep)')
    parser.add_argument('--timeout', type=int, default=900, help='Seconds allowed for the capture')
    parser.add_argument('--timing', type=Path, default=DEMO / 'typing.json' if (DEMO / 'typing.json').exists() else None,
                        help='Keystroke timing recorded with record_typing.py (default: native/demo/typing.json if present)')
    parser.add_argument('--model-timing', dest='timing', action='store_const', const=None,
                        help='Ignore any recording and use the timing model')
    parser.add_argument('--dark', action='store_true', help='Capture in dark mode (writes demo-dark.webp)')
    parser.add_argument('--real-speed', action='store_true', help='Replay recorded gaps without the tempo curve')
    parser.add_argument('--apng', action='store_true', help='Also write an animated PNG (much larger than WebP)')
    parser.add_argument('--encode-only', action='store_true', help='Re-encode an existing output directory')
    parser.add_argument('--preview', type=int, metavar='N', default=0,
                        help='Stop after the first N frames and skip encoding (quick look at settings)')
    args = parser.parse_args()
    folder = args.output.resolve()
    width, height = (int(v) for v in args.window.split('x'))
    recording = Recording(args.timing) if args.timing else None
    spec, marks, _ = build_script(width, height, recording, not args.real_speed, args.dark)
    if recording and recording.index != len(recording.keys):
        parser.error('The recording has more keys than the script; record it again')
    if args.encode_only:
        # Same keys, same frames: timing changes only need new durations.
        frames = json.loads((folder / 'frames.json').read_text())['frames']
        durations = [a['durationMs'] for a in spec['actions'] if a['op'] == 'frame']
        if len(durations) != len(frames):
            parser.error('The script no longer matches these frames; capture again')
        for row, duration in zip(frames, durations):
            row['durationMs'] = duration
        shutil.rmtree(folder / 'web', ignore_errors=True)
    else:
        if folder.exists():
            parser.error('--output must not exist; each run keeps its own frames and log')
        if args.preview:
            kept, seen = [], 0
            for action in spec['actions']:
                if action['op'] == 'frame':
                    if seen == args.preview:
                        break
                    seen += 1
                kept.append(action)
            spec = dict(spec, actions=kept)
        frames = capture(args.app.resolve(), folder, spec, args.scale, args.timeout)
        if args.preview:
            for row in frames:
                print(folder / 'frames' / row['file'])
            return
    result = encode(folder, frames, marks, args.encode_width, args.apng, 'demo-dark' if args.dark else 'demo')
    (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
