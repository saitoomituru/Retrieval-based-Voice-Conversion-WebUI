"""WebUI が所有する localhost RSVC runner の死活管理。"""

from __future__ import annotations

import collections
import os
import socket
import struct
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

from rvc_stream_protocol import HEADER, MAGIC, PROTOCOL_VERSION, Frame, FrameType, pack_frame, unpack_frame


DEFAULT_STREAM_HOST = "127.0.0.1"
DEFAULT_STREAM_PORT = 17865


@dataclass(frozen=True)
class ProbeResult:
    state: str
    detail: str

    @property
    def ready(self) -> bool:
        return self.state == "ready"


def _wire_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > 1024:
        raise ValueError("RSVC identity string is too long")
    return struct.pack("<H", len(encoded)) + encoded


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("RSVC peer closed during probe")
        data.extend(chunk)
    return bytes(data)


def probe_rsvc_stream(
    host: str = DEFAULT_STREAM_HOST,
    port: int = DEFAULT_STREAM_PORT,
    timeout: float = 0.5,
) -> ProbeResult:
    """Probe the actual RSVC handshake, not just whether a port accepts TCP."""

    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            payload = struct.pack("<HHIBB", 1, 1, 0, 3, 0)
            payload += _wire_string("RVC-WebUI") + _wire_string("0.1.0")
            sock.sendall(pack_frame(Frame(FrameType.HELLO, payload)))
            header = _recv_exact(sock, HEADER.size)
            magic, version, frame_type, session_id, sequence, payload_size, crc32, _timestamp = HEADER.unpack(header)
            if magic != MAGIC or version != PROTOCOL_VERSION or crc32 != 0:
                return ProbeResult("incompatible", "RSVC magic/versionが一致しません")
            reply = unpack_frame(header + _recv_exact(sock, payload_size))
            if frame_type != int(FrameType.HELLO_ACK) or session_id or sequence:
                return ProbeResult("incompatible", f"RSVC HELLO_ACKではありません: {reply.frame_type.name}")
            return ProbeResult("ready", f"RSVC v{version} {host}:{port}")
    except (ConnectionRefusedError, TimeoutError):
        return ProbeResult("unavailable", f"{host}:{port} は未起動です")
    except OSError as error:
        return ProbeResult("unavailable", f"{host}:{port}: {error}")
    except (ConnectionError, ValueError, struct.error) as error:
        return ProbeResult("incompatible", str(error))


class RvcRuntimeSupervisor:
    """Reuse a compatible runner or own one child with bounded restart."""

    def __init__(
        self,
        root: Path,
        *,
        python_executable: str = sys.executable,
        stream_port: int = DEFAULT_STREAM_PORT,
        engine_config: Optional[Path] = None,
        probe: Callable[..., ProbeResult] = probe_rsvc_stream,
        popen: Callable[..., subprocess.Popen] = subprocess.Popen,
        monitor_interval: float = 1.0,
        max_starts: int = 3,
        restart_window: float = 60.0,
    ) -> None:
        self.root = Path(root).resolve()
        self.python_executable = python_executable
        self.stream_port = stream_port
        self.engine_config = Path(engine_config).resolve() if engine_config else None
        self._probe = probe
        self._popen = popen
        self._monitor_interval = monitor_interval
        self._max_starts = max_starts
        self._restart_window = restart_window
        self._starts = collections.deque()
        self._process = None
        self._log_handle = None
        self._state = "STOPPED"
        self._detail = "WebUI runtime supervisorは未起動です"
        self._lock = threading.Lock()
        self._operation_lock = threading.Lock()
        self._stop_event = threading.Event()
        self._monitor = None

    def snapshot(self) -> dict[str, object]:
        with self._lock:
            process = self._process
            return {
                "state": self._state,
                "detail": self._detail,
                "endpoint": f"{DEFAULT_STREAM_HOST}:{self.stream_port}",
                "owned": process is not None and process.poll() is None,
                "pid": process.pid if process is not None and process.poll() is None else None,
                "protocol_version": PROTOCOL_VERSION,
                "engine": "rvc" if self.engine_config else "passthrough",
            }

    def status_text(self) -> str:
        status = self.snapshot()
        owner = f"WebUI管理 PID {status['pid']}" if status["owned"] else "既存runner再利用"
        if status["state"] not in {"READY", "REUSED"}:
            owner = "未接続"
        return (
            f"{status['state']} | {status['endpoint']} | {owner} | "
            f"engine={status['engine']} | {status['detail']}"
        )

    def _set_status(self, state: str, detail: str) -> None:
        with self._lock:
            self._state = state
            self._detail = detail

    def _probe_now(self) -> ProbeResult:
        return self._probe(DEFAULT_STREAM_HOST, self.stream_port, timeout=0.5)

    def _record_start_allowed(self) -> bool:
        now = time.monotonic()
        while self._starts and now - self._starts[0] > self._restart_window:
            self._starts.popleft()
        if len(self._starts) >= self._max_starts:
            return False
        self._starts.append(now)
        return True

    def _spawn(self) -> None:
        if not self._record_start_allowed():
            self._set_status("ERROR", "runner再起動上限に達しました")
            return
        runtime_script = self.root / "rvc_runtime_service.py"
        if not runtime_script.is_file():
            self._set_status("ERROR", f"runnerが見つかりません: {runtime_script}")
            return
        command = [
            self.python_executable,
            str(runtime_script),
            "--no-control",
            "--stream-port",
            str(self.stream_port),
        ]
        if self.engine_config:
            command.extend(["--engine-config", str(self.engine_config)])
        log_path = self.root / "logs" / "rvc-runtime-service.log"
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self._log_handle = log_path.open("a", encoding="utf-8")
        environment = os.environ.copy()
        environment["PYTHONUNBUFFERED"] = "1"
        try:
            process = self._popen(
                command,
                cwd=str(self.root),
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=self._log_handle,
                stderr=subprocess.STDOUT,
                close_fds=True,
            )
        except OSError as error:
            self._log_handle.close()
            self._log_handle = None
            self._set_status("ERROR", f"runner起動失敗: {error}")
            return
        with self._lock:
            self._process = process
            self._state = "STARTING"
            self._detail = f"runnerを起動しました PID {process.pid}"

    def _ensure_running(self) -> ProbeResult:
        result = self._probe_now()
        if result.ready:
            with self._lock:
                owned = self._process is not None and self._process.poll() is None
            self._set_status("READY" if owned else "REUSED", result.detail)
            return result
        if result.state == "incompatible":
            self._set_status("ERROR", result.detail)
            return result
        with self._lock:
            process = self._process
        if process is not None and process.poll() is None:
            self._set_status("STARTING", result.detail)
            return result
        self._close_finished_process()
        self._spawn()
        return result

    def ensure_running(self) -> ProbeResult:
        with self._operation_lock:
            return self._ensure_running()

    def configure_engine(self, engine_config: Path) -> bool:
        """Restart only a runner owned by this WebUI with a new engine config."""

        resolved = Path(engine_config).resolve()
        if not resolved.is_file():
            self._set_status("ERROR", f"engine設定が見つかりません: {resolved}")
            return False
        with self._operation_lock:
            probe = self._probe_now()
            with self._lock:
                process = self._process
            if probe.ready and (process is None or process.poll() is not None):
                self._set_status(
                    "ERROR",
                    "既存runnerはWebUIの管理外です。停止してから音色を適用してください",
                )
                return False
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
            self._close_finished_process()
            self.engine_config = resolved
            self._ensure_running()
            return self.snapshot()["state"] != "ERROR"

    def _close_finished_process(self) -> None:
        with self._lock:
            process = self._process
            if process is not None and process.poll() is not None:
                self._process = None
                if self._log_handle is not None:
                    self._log_handle.close()
                    self._log_handle = None

    def start(self, ready_timeout: float = 10.0) -> bool:
        if self._monitor is not None and self._monitor.is_alive():
            return self.snapshot()["state"] in {"READY", "REUSED"}
        self._stop_event.clear()
        self.ensure_running()
        self._monitor = threading.Thread(target=self._monitor_loop, daemon=True, name="rvc-runtime-supervisor")
        self._monitor.start()
        deadline = time.monotonic() + ready_timeout
        while time.monotonic() < deadline:
            if self.snapshot()["state"] in {"READY", "REUSED"}:
                return True
            if self.snapshot()["state"] == "ERROR":
                return False
            time.sleep(0.05)
        self._set_status("ERROR", f"runnerは{ready_timeout:g}秒以内にREADYになりませんでした")
        return False

    def _monitor_loop(self) -> None:
        while not self._stop_event.wait(self._monitor_interval):
            self.ensure_running()

    def stop(self) -> None:
        self._stop_event.set()
        if self._monitor is not None and self._monitor.is_alive():
            self._monitor.join(timeout=max(1.0, self._monitor_interval * 2))
        with self._operation_lock:
            with self._lock:
                process = self._process
                self._process = None
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
            if self._log_handle is not None:
                self._log_handle.close()
                self._log_handle = None
        self._set_status("STOPPED", "WebUIが所有したrunnerを停止しました")
