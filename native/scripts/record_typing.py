#!/usr/bin/env python3
"""Record your own keystroke timing for the landing-page demo.

Shows the keys of each demo scene (from demo_capture.py) and records the gap
before each key as you type them in this terminal. demo_capture.py then
replays those gaps instead of its timing model. Nothing is sent to the editor;
only key names and millisecond gaps are saved (default: native/demo/typing.json).

Symbols in the prompt: ⏎ Return, ↑ ↓ ← → arrow keys, ⇧ held Shift,
⇤ Home (or Ctrl-A), ⇥ End (or Ctrl-E). A ⏎ right after a partly typed command
accepts TeXstudio's completion list, so press Return there as you would in the
editor. Wrong keys beep and are ignored; Ctrl-R restarts the current scene,
Ctrl-C quits without saving.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import statistics
import sys
import termios
import time
import tty

from demo_capture import DEMO, build_script

SYMBOL = {'Return': '⏎', 'Up': '↑', 'Down': '↓', 'Left': '←', 'Right': '→', 'Home': '⇤', 'End': '⇥'}
SEQUENCES = {
    '\x1b[A': 'Up', '\x1b[B': 'Down', '\x1b[C': 'Right', '\x1b[D': 'Left',
    '\x1bOA': 'Up', '\x1bOB': 'Down', '\x1bOC': 'Right', '\x1bOD': 'Left',
    '\x1b[1;2A': 'Shift+Up', '\x1b[1;2B': 'Shift+Down', '\x1b[1;2C': 'Shift+Right', '\x1b[1;2D': 'Shift+Left',
    '\x1b[H': 'Home', '\x1bOH': 'Home', '\x1b[1~': 'Home', '\x01': 'Home',
    '\x1b[F': 'End', '\x1bOF': 'End', '\x1b[4~': 'End', '\x05': 'End',
    '\r': 'Return', '\n': 'Return', '\x7f': 'Backspace',
    '\x03': 'Quit', '\x12': 'Restart',
}
MAX_GAP_MS = 2500   # a longer gap (distraction, a mistake) is shortened to this


def label(token):
    if token == '\n':
        return '⏎'
    if token.startswith('Shift+'):
        return '⇧' + SYMBOL[token[6:]]
    return SYMBOL.get(token, token)


def matches(pressed, expected):
    return pressed == expected or (pressed == 'Return' and expected == '\n')


def keys_from(text):
    """Split terminal input into key names; returns (keys, unparsed rest)."""
    keys = []
    while text:
        for seq in sorted(SEQUENCES, key=len, reverse=True):
            if text.startswith(seq):
                keys.append(SEQUENCES[seq]); text = text[len(seq):]
                break
        else:
            if text[0] == '\x1b':
                if len(text) < 6:
                    return keys, text  # wait for the rest of an escape sequence
                text = text[1:]        # unknown sequence: drop the escape
                continue
            keys.append(text[0]); text = text[1:]
    return keys, ''


def show(tokens, done, wrong=False):
    shown = [label(t['key']) for t in tokens]
    before = ''.join(shown[max(0, done - 30):done])
    current = shown[done] if done < len(shown) else ''
    after = ''.join(shown[done + 1:done + 50])
    colour = '\x1b[41m' if wrong else '\x1b[7m'
    sys.stdout.write(f'\r\x1b[K\x1b[32m{before}\x1b[0m{colour}{current}\x1b[0m\x1b[2m{after}\x1b[0m')
    sys.stdout.flush()


def record_scene(fd, name, tokens):
    print(f'\n\x1b[1m{name}\x1b[0m: {len(tokens)} keys. Start when ready; the first key starts the clock.')
    print(''.join(label(t['key']) for t in tokens))
    while True:
        rows, done, last, pending = [], 0, None, ''
        show(tokens, done)
        restart = False
        while done < len(tokens):
            chunk = os.read(fd, 64).decode('utf-8', 'replace')
            now = time.monotonic()
            keys, pending = keys_from(pending + chunk)
            for key in keys:
                if key == 'Quit':
                    raise KeyboardInterrupt
                if key == 'Restart':
                    restart = True
                    break
                expected = tokens[done]['key']
                if not matches(key, expected):
                    sys.stdout.write('\a'); show(tokens, done, wrong=True)
                    continue
                gap = 0 if last is None else min(int((now - last) * 1000), MAX_GAP_MS)
                rows.append({'key': expected, 'delayMs': gap})
                last = now; done += 1
                show(tokens, done)
                if done == len(tokens):
                    break
            if restart:
                break
        if restart:
            print('\n  restarting this scene')
            continue
        gaps = [r['delayMs'] for r in rows[1:]]
        print(f'\n  done: median gap {statistics.median(gaps)} ms, total {sum(gaps) / 1000:.1f} s')
        return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, default=DEMO / 'typing.json')
    args = parser.parse_args()
    if not sys.stdin.isatty():
        parser.error('Run this in an interactive terminal')
    _, _, tokens = build_script(1200, 760)
    scenes = []
    for token in tokens:
        if not scenes or scenes[-1][0] != token['scene']:
            scenes.append((token['scene'], []))
        scenes[-1][1].append(token)
    print(__doc__.split('\n\n', 1)[1])
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd)
    rows = []
    try:
        tty.setcbreak(fd)  # no echo, keys arrive immediately; Ctrl-C still quits
        for name, scene_tokens in scenes:
            rows += record_scene(fd, name, scene_tokens)
    except KeyboardInterrupt:
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)
        print('\nStopped; nothing saved.')
        return 1
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)
    args.output.write_text(json.dumps({'schema': 1, 'recordedAt': datetime.now(timezone.utc).isoformat(timespec='seconds'),
                                       'keys': rows}, indent=1) + '\n')
    print(f'\nSaved {len(rows)} keys to {args.output}. Next:')
    print('  python3 native/scripts/demo_capture.py --output <new directory>')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
