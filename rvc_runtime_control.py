"""WebUIとAU GUIが共有するlocalhost runtime選択control plane。"""

from __future__ import annotations

import json
import os
import socket
import struct
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote

from rvc_runtime_bonjour import BonjourRuntimeDirectory, LOCAL_CHOICE
from rvc_runtime_gateway import RsvcGateway
from rvc_stream_protocol import HEADER, Frame, FrameType, pack_frame, unpack_frame


DEFAULT_CONTROL_HOST = "127.0.0.1"
DEFAULT_CONTROL_PORT = 17864
MAX_REQUEST_BYTES = 4096


def _recv_exact(connection: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise ConnectionError("runtime closed during model discovery")
        data.extend(chunk)
    return bytes(data)


def _probe_runtime_models(host: str, port: int) -> list[dict[str, str]]:
    """Read the selected runtime's public model catalog through RSVC HELLO."""

    name = b"WebUI control"
    hello = struct.pack("<HHIBBH", 1, 1, 0, 1, 0, len(name)) + name
    version = b"v1"
    hello += struct.pack("<H", len(version)) + version
    with socket.create_connection((host, port), timeout=0.75) as connection:
        connection.settimeout(0.75)
        connection.sendall(pack_frame(Frame(FrameType.HELLO, hello)))
        header = _recv_exact(connection, HEADER.size)
        payload_size = struct.unpack_from("<I", header, 16)[0]
        response = unpack_frame(header + _recv_exact(connection, payload_size))
    if response.frame_type is not FrameType.HELLO_ACK:
        raise ValueError("runtime rejected model discovery")
    if len(response.payload) < 26:
        raise ValueError("short HELLO_ACK")
    name_size = struct.unpack_from("<H", response.payload, 24)[0]
    offset = 26 + name_size
    if offset + 2 > len(response.payload):
        raise ValueError("HELLO_ACK has no capabilities")
    caps_size = struct.unpack_from("<H", response.payload, offset)[0]
    offset += 2
    if offset + caps_size != len(response.payload):
        raise ValueError("invalid HELLO_ACK capabilities")
    caps = json.loads(response.payload[offset:offset + caps_size].decode("utf-8"))
    models = caps.get("models", [])
    return [
        {
            "id": str(model["id"]),
            "name": str(model.get("name", model["id"])),
            "index": str(model.get("index", "")),
        }
        for model in models
        if isinstance(model, dict) and model.get("id")
    ]


class RuntimeRouterControl:
    def __init__(
        self,
        directory: BonjourRuntimeDirectory,
        gateway: RsvcGateway,
        *,
        host: str = DEFAULT_CONTROL_HOST,
        port: int = DEFAULT_CONTROL_PORT,
        engine_config_path=None,
    ) -> None:
        self.directory = directory
        self.gateway = gateway
        self.host = host
        self.port = port
        self.engine_config_path = engine_config_path
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
        engine = {"model": "", "index": ""}
        models = []
        if self.engine_config_path is not None:
            try:
                with open(self.engine_config_path, encoding="utf-8") as handle:
                    config = json.load(handle)
                engine = {
                    "model": os.path.basename(str(config.get("model_path", ""))),
                    "index": os.path.basename(str(config.get("index_path", ""))),
                }
                catalog = config.get("models", [])
                default_id = str(config.get("default_model_id", ""))
                default = next(
                    (entry for entry in catalog if str(entry.get("id", "")) == default_id),
                    {},
                )
                models = [{
                    "id": "active",
                    "name": f"WebUI default: {default.get('name', engine['model'])}",
                    "index": os.path.basename(str(default.get("index_path", engine["index"]))),
                }]
                models.extend(
                    {
                        "id": str(entry["id"]),
                        "name": str(entry.get("name", entry["id"])),
                        "index": os.path.basename(str(entry.get("index_path", ""))),
                    }
                    for entry in catalog
                    if isinstance(entry, dict) and entry.get("id")
                )
            except (OSError, TypeError, ValueError, json.JSONDecodeError):
                pass
        try:
            probed_models = _probe_runtime_models(target.host, target.port)
            if probed_models:
                models = probed_models
                active = next(
                    (model for model in models if model["id"] == "active"), models[0]
                )
                engine = {
                    "model": active["name"].removeprefix("WebUI default: "),
                    "index": active.get("index", ""),
                }
        except (ConnectionError, OSError, TypeError, ValueError, json.JSONDecodeError):
            pass
        return {
            "protocol": 1,
            "selected": selected,
            "available": available,
            "choices": choices,
            "gateway": self.gateway.snapshot(),
            "bonjour": self.directory.status_text(),
            "engine": engine,
            "models": models,
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
                        "model\t" + quote(str(snapshot["engine"]["model"]), safe=""),
                        "index\t" + quote(str(snapshot["engine"]["index"]), safe=""),
                    ]
                    lines.extend("choice\t" + quote(choice, safe="") for choice in snapshot["choices"])
                    lines.extend(
                        "model-choice\t{}\t{}\t{}".format(
                            quote(model["id"], safe=""),
                            quote(model["name"], safe=""),
                            quote(model.get("index", ""), safe=""),
                        )
                        for model in snapshot["models"]
                    )
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
