"""AUの固定localhost endpointと選択済みRSVC backendを結ぶ転送面。"""

from __future__ import annotations

import socket
import threading
from dataclasses import dataclass
from typing import Optional


DEFAULT_GATEWAY_HOST = "127.0.0.1"
DEFAULT_GATEWAY_PORT = 17865
DEFAULT_BACKEND_PORT = 17866


@dataclass(frozen=True)
class RuntimeTarget:
    identity: str
    label: str
    host: str
    port: int
    local: bool = False


class RsvcGateway:
    """選択した1 endpointだけへ接続する、protocol非依存のTCP gateway。"""

    def __init__(
        self,
        target: RuntimeTarget,
        *,
        host: str = DEFAULT_GATEWAY_HOST,
        port: int = DEFAULT_GATEWAY_PORT,
        connect_timeout: float = 2.0,
    ) -> None:
        self.host = host
        self.port = port
        self._target = target
        self._connect_timeout = connect_timeout
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._listener: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None

    def select(self, target: RuntimeTarget) -> None:
        """新規sessionの転送先を明示変更する。既存sessionは移動させない。"""

        with self._lock:
            self._target = target

    def target(self) -> RuntimeTarget:
        with self._lock:
            return self._target

    def snapshot(self) -> dict[str, object]:
        target = self.target()
        return {
            "endpoint": f"{self.host}:{self.port}",
            "target_identity": target.identity,
            "target": f"{target.host}:{target.port}",
            "target_label": target.label,
            "local": target.local,
            "running": self._thread is not None and self._thread.is_alive(),
        }

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop_event.clear()
        listener = socket.create_server((self.host, self.port), reuse_port=False)
        listener.settimeout(0.2)
        self._listener = listener
        self._thread = threading.Thread(
            target=self._accept_loop,
            daemon=True,
            name="rsvc-local-gateway",
        )
        self._thread.start()

    def _accept_loop(self) -> None:
        listener = self._listener
        if listener is None:
            return
        while not self._stop_event.is_set():
            try:
                client, _address = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            target = self.target()
            threading.Thread(
                target=self._forward,
                args=(client, target),
                daemon=True,
                name="rsvc-gateway-session",
            ).start()

    def _forward(self, client: socket.socket, target: RuntimeTarget) -> None:
        with client:
            try:
                backend = socket.create_connection(
                    (target.host, target.port), timeout=self._connect_timeout
                )
            except OSError:
                return
            with backend:
                client.settimeout(None)
                backend.settimeout(None)
                upload = threading.Thread(
                    target=self._pump,
                    args=(client, backend),
                    daemon=True,
                    name="rsvc-gateway-upload",
                )
                upload.start()
                self._pump(backend, client)
                upload.join(timeout=1.0)

    @staticmethod
    def _pump(source: socket.socket, destination: socket.socket) -> None:
        try:
            while True:
                data = source.recv(64 * 1024)
                if not data:
                    break
                destination.sendall(data)
        except OSError:
            pass
        finally:
            try:
                destination.shutdown(socket.SHUT_WR)
            except OSError:
                pass

    def stop(self) -> None:
        self._stop_event.set()
        listener = self._listener
        self._listener = None
        if listener is not None:
            listener.close()
        if self._thread is not None and self._thread.is_alive():
            self._thread.join(timeout=1.0)
        self._thread = None

