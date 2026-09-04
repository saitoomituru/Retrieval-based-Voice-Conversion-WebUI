"""WebUIとAU GUIが共有するlocalhost runtime選択control plane。"""

from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote

from rvc_runtime_bonjour import BonjourRuntimeDirectory, LOCAL_CHOICE
from rvc_runtime_gateway import RsvcGateway


DEFAULT_CONTROL_HOST = "127.0.0.1"
DEFAULT_CONTROL_PORT = 17864
MAX_REQUEST_BYTES = 4096


class RuntimeRouterControl:
    def __init__(
        self,
        directory: BonjourRuntimeDirectory,
        gateway: RsvcGateway,
        *,
        host: str = DEFAULT_CONTROL_HOST,
        port: int = DEFAULT_CONTROL_PORT,
    ) -> None:
        self.directory = directory
        self.gateway = gateway
        self.host = host
        self.port = port
        self._selected_choice = LOCAL_CHOICE
        self._lock = threading.Lock()
        self._server = None
        self._thread = None

    def selected_choice(self) -> str:
        with self._lock:
            return self._selected_choice

    def choices(self) -> list[str]:
        return self.directory.choices()

    def select(self, choice: str) -> dict[str, object]:
        target = self.directory.resolve(str(choice or LOCAL_CHOICE))
        self.gateway.select(target)
        with self._lock:
            self._selected_choice = str(choice or LOCAL_CHOICE)
        return self.snapshot()

    def snapshot(self) -> dict[str, object]:
        target = self.gateway.target()
        choices = self.choices()
        selected = self.selected_choice()
        available = target.local or any(
            service.identity == target.identity for service in self.directory.services()
        )
        return {
            "protocol": 1,
            "selected": selected,
            "available": available,
            "choices": choices,
            "gateway": self.gateway.snapshot(),
            "bonjour": self.directory.status_text(),
        }

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def _json(self, status: int, payload: dict[str, object]) -> None:
                body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                if self.path == "/v1/runtimes.txt":
                    snapshot = owner.snapshot()
                    lines = [
                        "RSVC-CONTROL/1",
                        "selected\t" + quote(str(snapshot["selected"]), safe=""),
                    ]
                    lines.extend("choice\t" + quote(choice, safe="") for choice in snapshot["choices"])
                    body = ("\n".join(lines) + "\n").encode("ascii")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain; charset=us-ascii")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                if self.path != "/v1/runtimes":
                    self._json(404, {"error": "not found"})
                    return
                self._json(200, owner.snapshot())

            def do_POST(self):
                if self.path == "/v1/select-text":
                    try:
                        length = int(self.headers.get("Content-Length", "0"))
                        if length <= 0 or length > MAX_REQUEST_BYTES:
                            raise ValueError("invalid request size")
                        choice = unquote(self.rfile.read(length).decode("ascii"))
                        owner.select(choice)
                        body = b"OK\n"
                        self.send_response(200)
                        self.send_header("Content-Type", "text/plain; charset=us-ascii")
                        self.send_header("Content-Length", str(len(body)))
                        self.end_headers()
                        self.wfile.write(body)
                    except (OSError, RuntimeError, UnicodeError, ValueError) as error:
                        self._json(400, {"error": str(error)})
                    return
                if self.path != "/v1/select":
                    self._json(404, {"error": "not found"})
                    return
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if length <= 0 or length > MAX_REQUEST_BYTES:
                        raise ValueError("invalid request size")
                    payload = json.loads(self.rfile.read(length).decode("utf-8"))
                    choice = payload.get("choice")
                    if not isinstance(choice, str):
                        raise ValueError("choice must be a string")
                    self._json(200, owner.select(choice))
                except (json.JSONDecodeError, OSError, RuntimeError, ValueError) as error:
                    self._json(400, {"error": str(error)})

            def log_message(self, *_args):
                pass

        self._server = ThreadingHTTPServer((self.host, self.port), Handler)
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(
            target=self._server.serve_forever,
            daemon=True,
            name="rvc-runtime-control",
        )
        self._thread.start()

    def stop(self) -> None:
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
        if self._thread is not None and self._thread.is_alive():
            self._thread.join(timeout=1.0)
        self._server = None
        self._thread = None
