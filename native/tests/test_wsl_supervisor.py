"""The WSL bridge must preserve binary pipes and reap forked compiler workers."""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest


@unittest.skipIf(os.name == 'nt' or not shutil.which('cc'), 'POSIX compiler required')
class WslSupervisor(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        cls.bridge = cls.root / 'bridge'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            str(Path(__file__).resolve().parents[1] / 'wsl/supervisor.c'), '-o', str(cls.bridge)], check=True)

    def test_binary_pipe_is_not_decoded_or_newline_translated(self):
        process = subprocess.Popen([str(self.bridge), str(self.root), '--', sys.executable, '-c',
            'import sys; data=sys.stdin.buffer.read(25600); sys.stdout.buffer.write(data); sys.stdout.buffer.flush()'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.addCleanup(lambda: process.poll() is None and process.kill())
        data = bytes(range(256)) * 100
        process.stdin.write(data); process.stdin.flush()
        # Keep the editor pipe open while waiting for the engine response.
        self.assertEqual(process.stdout.read(len(data)), data)
        self.assertEqual(process.wait(timeout=5), 0)
        process.stdin.close(); process.stdout.close()

    def test_stdin_eof_kills_frontend_and_forked_worker(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); marker = root / 'workers'
            script = root / 'engine.py'
            script.write_text('''import os, signal, time
worker = os.fork()
if worker == 0:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    while True: time.sleep(1)
with open("workers", "w") as file: file.write(str(os.getpid()) + " " + str(worker))
while True: time.sleep(1)
''')
            process = subprocess.Popen([str(self.bridge), str(root), '--', sys.executable, str(script)],
                                       stdin=subprocess.PIPE, stdout=subprocess.DEVNULL)
            pids = []
            try:
                deadline = time.monotonic() + 5
                while not marker.exists() and time.monotonic() < deadline: time.sleep(.01)
                self.assertTrue(marker.exists())
                pids = [int(x) for x in marker.read_text().split()]
                process.stdin.close()
                process.wait(timeout=5)
                for pid in pids:
                    # A reparented zombie can briefly remain until init reaps it.
                    result = subprocess.run(['ps', '-o', 'stat=', '-p', str(pid)], capture_output=True, text=True)
                    self.assertTrue(result.returncode != 0 or result.stdout.strip().startswith('Z'), result.stdout)
            finally:
                if process.poll() is None: process.kill(); process.wait()
                for pid in pids:
                    try: os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError: pass

if __name__ == '__main__': unittest.main()
