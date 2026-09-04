"""WebUI process signalsを既存のbounded shutdown経路へ接続する。"""

from __future__ import annotations

import signal
import threading
from contextlib import contextmanager


@contextmanager
def sigterm_as_keyboard_interrupt():
    """Let Gradio's main-thread wait unwind through the WebUI ``finally`` block.

    The handler deliberately performs no I/O, joins, or child termination.
    Gradio already handles ``KeyboardInterrupt`` by closing its server; the
    enclosing WebUI lifecycle then owns controller/runtime cleanup.
    """

    if threading.current_thread() is not threading.main_thread():
        yield
        return

    previous = signal.getsignal(signal.SIGTERM)

    def request_shutdown(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, request_shutdown)
    try:
        yield
    finally:
        signal.signal(signal.SIGTERM, previous)
