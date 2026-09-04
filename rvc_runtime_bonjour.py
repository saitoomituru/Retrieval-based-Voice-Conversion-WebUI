"""macOS mDNSResponderを使うRSVC runtimeの広告・発見・明示選択。"""

from __future__ import annotations

import hashlib
import re
import socket
import subprocess
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

from rvc_runtime_gateway import RuntimeTarget


SERVICE_TYPE = "_rvc-realtime._tcp"
SERVICE_DOMAIN = "local"
LOCAL_CHOICE = "Localhost（このWebUI）"

_BROWSE_LINE = re.compile(
    r"\s(Add|Rmv)\s+\d+\s+\d+\s+(\S+)\s+"
    r"(_rvc-realtime\._tcp\.)\s+(.+?)\s*$"
)
_RESOLVE_LINE = re.compile(r"can be reached at\s+(.+?):(\d+)\s+")


@dataclass(frozen=True)
class DiscoveredService:
    name: str
    service_type: str = SERVICE_TYPE
    domain: str = SERVICE_DOMAIN

    @property
    def identity(self) -> str:
        return f"{self.name}.{self.service_type}.{self.domain}."

    @property
    def choice(self) -> str:
        return f"Bonjour: {self.name}"


def runtime_identity(root: Path) -> str:
    material = f"{socket.gethostname()}\0{Path(root).resolve()}".encode("utf-8")
    return hashlib.sha256(material).hexdigest()[:16]


def parse_browse_line(line: str) -> Optional[tuple[str, DiscoveredService]]:
    match = _BROWSE_LINE.search(line)
    if match is None:
        return None
    action, domain, service_type, name = match.groups()
    return action, DiscoveredService(
        name=name,
        service_type=service_type.removesuffix("."),
        domain=domain.removesuffix("."),
    )


def parse_resolve_output(output: str) -> Optional[tuple[str, int]]:
    match = _RESOLVE_LINE.search(output)
    if match is None:
        return None
    return match.group(1).removesuffix("."), int(match.group(2))


class BonjourRuntimeDirectory:
    """dns-sd processの寿命と検出集合をWebUI processへ束縛する。"""

    def __init__(
        self,
        root: Path,
        local_port: int,
        *,
        backend: str = "cpu",
        capacity: int = 4,
        popen: Callable[..., subprocess.Popen] = subprocess.Popen,
    ) -> None:
        self.root = Path(root).resolve()
        self.local_port = local_port
        self.backend = backend
        self.capacity = capacity
        self.runtime_id = runtime_identity(self.root)
        host_label = socket.gethostname().removesuffix(".local")
        self.service_name = f"RVC WebUI {host_label} {self.runtime_id[:6]}"
        self._popen = popen
        self._advertiser = None
        self._browser = None
        self._browser_thread = None
        self._services: dict[str, DiscoveredService] = {}
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._detail = "Bonjour未起動"

    def advertise_command(self) -> list[str]:
        return [
            "/usr/bin/dns-sd",
            "-R",
            self.service_name,
            SERVICE_TYPE,
            SERVICE_DOMAIN,
            str(self.local_port),
            "proto=1",
            f"backend={self.backend}",
            f"capacity={self.capacity}",
            f"runtime={self.runtime_id}",
        ]

    @staticmethod
    def browse_command() -> list[str]:
        return ["/usr/bin/dns-sd", "-B", SERVICE_TYPE, SERVICE_DOMAIN]

    def start(self) -> bool:
        if self._browser_thread is not None and self._browser_thread.is_alive():
            return True
        if not Path("/usr/bin/dns-sd").is_file():
            self._detail = "dns-sdがないためBonjourを利用できません"
            return False
        self._stop_event.clear()
        options = {
            "stdout": subprocess.PIPE,
            "stderr": subprocess.STDOUT,
            "text": True,
            "bufsize": 1,
        }
        try:
            self._advertiser = self._popen(self.advertise_command(), **options)
            self._browser = self._popen(self.browse_command(), **options)
        except OSError as error:
            self._detail = f"Bonjour起動失敗: {error}"
            self.stop()
            return False
        self._browser_thread = threading.Thread(
            target=self._read_browser,
            daemon=True,
            name="rvc-bonjour-browser",
        )
        self._browser_thread.start()
        self._detail = f"広告・探索中: {self.service_name}"
        return True

    def _read_browser(self) -> None:
        browser = self._browser
        if browser is None or browser.stdout is None:
            return
        for line in browser.stdout:
            if self._stop_event.is_set():
                return
            parsed = parse_browse_line(line)
            if parsed is None:
                continue
            action, service = parsed
            with self._lock:
                if action == "Add":
                    self._services[service.identity] = service
                else:
                    self._services.pop(service.identity, None)

    def services(self) -> list[DiscoveredService]:
        with self._lock:
            return sorted(self._services.values(), key=lambda item: item.name.casefold())

    def choices(self) -> list[str]:
        return [LOCAL_CHOICE] + [service.choice for service in self.services()]

    def find_choice(self, choice: str) -> Optional[DiscoveredService]:
        return next((service for service in self.services() if service.choice == choice), None)

    def resolve(self, choice: str, timeout: float = 3.0) -> RuntimeTarget:
        if choice == LOCAL_CHOICE:
            return self.local_target()
        service = self.find_choice(choice)
        if service is None:
            raise ValueError("選択したBonjour serviceは現在の検出集合にありません")
        if service.name == self.service_name:
            return self.local_target(label=f"Bonjour self: {service.name}")
        command = [
            "/usr/bin/dns-sd",
            "-L",
            service.name,
            service.service_type,
            service.domain,
        ]
        process = self._popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            output, _unused = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            output, _unused = process.communicate(timeout=1.0)
        resolved = parse_resolve_output(output)
        if resolved is None:
            raise RuntimeError(f"Bonjour resolveに失敗しました: {service.name}")
        host, port = resolved
        return RuntimeTarget(service.identity, service.name, host, port, local=False)

    def local_target(self, label: str = LOCAL_CHOICE) -> RuntimeTarget:
        return RuntimeTarget(
            f"local:{self.runtime_id}",
            label,
            "127.0.0.1",
            self.local_port,
            local=True,
        )

    def status_text(self) -> str:
        return f"{self._detail} | 検出 {len(self.services())}件"

    def stop(self) -> None:
        self._stop_event.set()
        for process in (self._browser, self._advertiser):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=1.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=1.0)
        if self._browser_thread is not None and self._browser_thread.is_alive():
            self._browser_thread.join(timeout=1.0)
        self._browser = None
        self._advertiser = None
        self._browser_thread = None
        self._detail = "Bonjour停止"
