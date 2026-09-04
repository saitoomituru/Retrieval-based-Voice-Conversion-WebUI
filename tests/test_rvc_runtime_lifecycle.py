import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from rvc_runtime_lifecycle import sigterm_as_keyboard_interrupt


class RuntimeLifecycleTest(unittest.TestCase):
    def test_context_restores_previous_sigterm_handler(self):
        previous = signal.getsignal(signal.SIGTERM)
        with sigterm_as_keyboard_interrupt():
            self.assertIsNot(signal.getsignal(signal.SIGTERM), previous)
        self.assertIs(signal.getsignal(signal.SIGTERM), previous)

    def test_sigterm_reaches_enclosing_finally(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ready = root / "ready"
            cleaned = root / "cleaned"
            code = "\n".join(
                [
                    "import time",
                    "from pathlib import Path",
                    "from rvc_runtime_lifecycle import sigterm_as_keyboard_interrupt",
                    f"ready = Path({str(ready)!r})",
                    f"cleaned = Path({str(cleaned)!r})",
                    "try:",
                    "    with sigterm_as_keyboard_interrupt():",
                    "        ready.write_text('ready', encoding='utf-8')",
                    "        while True:",
                    "            time.sleep(0.05)",
                    "except KeyboardInterrupt:",
                    "    pass",
                    "finally:",
                    "    cleaned.write_text('cleaned', encoding='utf-8')",
                ]
            )
            process = subprocess.Popen([sys.executable, "-c", code], cwd=Path.cwd())
            self.addCleanup(lambda: process.poll() is None and process.kill())
            deadline = time.monotonic() + 2
            while not ready.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(ready.exists())
            process.terminate()
            self.assertEqual(process.wait(timeout=2), 0)
            self.assertEqual(cleaned.read_text(encoding="utf-8"), "cleaned")


if __name__ == "__main__":
    unittest.main()
