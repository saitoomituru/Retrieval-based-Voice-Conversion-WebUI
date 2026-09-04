"""RVC WebUI 側が所有する localhost realtime runtime service。"""

from __future__ import annotations

import argparse
import copy
import json
import queue
import socket
import struct
import threading
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from rvc_stream_protocol import (
    AUDIO_FLAG_DISCONTINUOUS,
    AUDIO_FLAG_OFFLINE,
    HEADER,
    Frame,
    FrameType,
    pack_audio,
    pack_frame,
    unpack_audio,
    unpack_config_update,
    unpack_frame,
)

LOOPBACK = "127.0.0.1"
DEFAULT_CONTROL_PORT = 17864
DEFAULT_STREAM_PORT = 17865
MAX_IN_FLIGHT = 2


@dataclass(frozen=True)
class SessionConfig:
    pitch: float = 0.0
    formant: float = 0.0
    index_rate: float = 0.0
    rms_mix: float = 0.5
    threshold: float = -60.0
    f0_method: int = 0

    def engine_arguments(self) -> tuple[float, float, float, float, float, int]:
        return (
            self.pitch,
            self.formant,
            self.index_rate,
            self.rms_mix,
            self.threshold,
            self.f0_method,
        )


class SessionConfigState:
    """One session's hot parameters; never shared across clients."""

    def __init__(self) -> None:
        self._config = SessionConfig()
        self._last_sequence = 0
        self._lock = threading.Lock()

    def apply(self, sequence: int, payload: bytes) -> int:
        try:
            values = unpack_config_update(payload)
            candidate = SessionConfig(*values)
        except (ValueError, struct.error):
            return 1
        if not (
            -24.0 <= candidate.pitch <= 24.0
            and -12.0 <= candidate.formant <= 12.0
            and 0.0 <= candidate.index_rate <= 1.0
            and 0.0 <= candidate.rms_mix <= 1.0
            and -60.0 <= candidate.threshold <= 0.0
            and candidate.f0_method in (0, 1, 2)
        ):
            return 1
        with self._lock:
            if sequence == 0 or sequence <= self._last_sequence:
                return 2
            self._config = candidate
            self._last_sequence = sequence
        return 0

    def snapshot(self) -> SessionConfig:
        with self._lock:
            return self._config


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("peer closed")
        chunks.extend(chunk)
    return bytes(chunks)


def recv_frame(sock: socket.socket) -> Frame:
    header = _recv_exact(sock, HEADER.size)
    payload_size = struct.unpack_from("<I", header, 16)[0]
    return unpack_frame(header + _recv_exact(sock, payload_size))


def _read_string(payload: bytes, offset: int) -> tuple[str, int]:
    if offset + 2 > len(payload):
        raise ValueError("missing string length")
    size = struct.unpack_from("<H", payload, offset)[0]
    offset += 2
    if size > 1024 or offset + size > len(payload):
        raise ValueError("invalid string length")
    return payload[offset:offset + size].decode("utf-8"), offset + size


class PassthroughEngine:
    def process(self, audio, *_args):
        return audio


class Runtime:
    def __init__(self, engine_factory=None):
        self.engine_factory = engine_factory or PassthroughEngine
        self.state = "READY"
        self.session_counter = 0
        self.lock = threading.Lock()

    def health(self) -> dict[str, object]:
        return {"service": "rvc-realtime", "state": self.state, "protocol_version": 1}

    def next_session_id(self) -> int:
        with self.lock:
            self.session_counter += 1
            return self.session_counter

    def models(self) -> list[dict[str, str]]:
        describe_models = getattr(self.engine_factory, "models", None)
        if describe_models is not None:
            return describe_models()
        return [{"id": "active", "name": "active", "index": ""}]

    def create_engine(
        self,
        sample_rate: int,
        block_frames: int,
        crossfade: int,
        extra: int,
        model_id: str = "active",
    ):
        create_for_session = getattr(self.engine_factory, "create_for_session", None)
        if create_for_session is not None:
            return create_for_session(
                sample_rate, block_frames, crossfade, extra, model_id=model_id
            )
        return self.engine_factory()


def load_rvc_engine(config_path: str):
    """Load the existing reusable worker engine outside the AU process."""

    with open(config_path, encoding="utf-8") as handle:
        config = json.load(handle)
    from RVCRealtime.worker.rvc_worker import RVCStreamEngine

    engine = RVCStreamEngine(config)
    engine.prewarm()
    return engine


class RvcEngineFactory:
    """Build one engine per RSVC session using the AU-negotiated audio shape."""

    def __init__(self, config_path: str):
        with open(config_path, encoding="utf-8") as handle:
            self._base_config = json.load(handle)
        self._build_lock = threading.Lock()

        catalog = self._base_config.get("models")
        if not isinstance(catalog, list) or not catalog:
            catalog = [{
                "id": "legacy-active",
                "name": str(self._base_config.get("model_path", "active")),
                "model_path": self._base_config.get("model_path", ""),
                "index_path": self._base_config.get("index_path", ""),
            }]
        self._models = {
            str(entry["id"]): copy.deepcopy(entry)
            for entry in catalog
            if isinstance(entry, dict) and entry.get("id") and entry.get("model_path")
        }
        self._default_model_id = str(
            self._base_config.get("default_model_id") or next(iter(self._models), "")
        )
        if self._default_model_id not in self._models:
            raise ValueError("runtime default model is not present in model catalog")

    def models(self) -> list[dict[str, str]]:
        default = self._models[self._default_model_id]
        result = [{
            "id": "active",
            "name": f"WebUI default: {default.get('name', self._default_model_id)}",
            "index": str(default.get("index_path", "")).rsplit("/", 1)[-1],
        }]
        result.extend(
            {
                "id": model_id,
                "name": str(entry.get("name", model_id)),
                "index": str(entry.get("index_path", "")).rsplit("/", 1)[-1],
            }
            for model_id, entry in self._models.items()
        )
        return result

    def create_for_session(
        self,
        sample_rate: int,
        block_frames: int,
        crossfade: int,
        extra: int,
        *,
        model_id: str = "active",
    ):
        resolved_model_id = self._default_model_id if model_id == "active" else model_id
        if resolved_model_id not in self._models:
            raise ValueError(f"unknown runtime model id: {model_id}")
        model = self._models[resolved_model_id]
        from RVCRealtime.worker.rvc_worker import RVCStreamEngine

        config = copy.deepcopy(self._base_config)
        config.pop("models", None)
        config.pop("default_model_id", None)
        config["model_path"] = model["model_path"]
        config["index_path"] = model.get("index_path", "")
        config["sample_rate"] = sample_rate
        config["block_ms"] = 1000.0 * block_frames / sample_rate
        config["crossfade_ms"] = 1000.0 * crossfade / sample_rate
        config["extra_ms"] = 1000.0 * extra / sample_rate
        # Config and parts of the legacy engine still use process-global state
        # (cwd, sys.path, singleton device config). Serialize construction while
        # keeping inference on independent per-session threads.
        with self._build_lock:
            engine = RVCStreamEngine(config)
            engine.prewarm()
            return engine


def _hello_ack(runtime: Runtime) -> bytes:
    name = b"RVC WebUI runtime"
    caps = json.dumps({"protocol_version": 1, "backends": ["cpu"], "sample_rates": [44100, 48000],
                       "max_sessions": 4, "max_in_flight": MAX_IN_FLIGHT,
                       "audio_flags": {"offline": AUDIO_FLAG_OFFLINE},
                       "models": runtime.models()},
                      separators=(",", ":")).encode()
    payload = struct.pack("<HHIIIIIH", 1, 0, 0, 1 << 20, 131072, 1000, 4, len(name)) + name
    payload += struct.pack("<H", len(caps)) + caps
    return pack_frame(Frame(FrameType.HELLO_ACK, payload))


def _parse_session_open(payload: bytes) -> tuple[int, int, int, int, int, int, str]:
    if len(payload) < 30:
        raise ValueError("short SESSION_OPEN")
    request_id, sample_rate, channels, sample_format, block_frames, crossfade, extra, _flags = struct.unpack_from(
        "<IIHHIIII", payload
    )
    model_id, offset = _read_string(payload, 28)
    _index_id, offset = _read_string(payload, offset)
    _auth, offset = _read_string(payload, offset)
    if offset != len(payload) or not model_id:
        raise ValueError("invalid SESSION_OPEN strings")
    if sample_rate not in (44100, 48000) or channels != 1 or sample_format != 1 or block_frames <= 0:
        raise ValueError("unsupported SESSION_OPEN")
    return request_id, sample_rate, channels, block_frames, crossfade, extra, model_id


def _validate_engine_configuration(engine, sample_rate: int, block_frames: int) -> None:
    engine_sample_rate = getattr(engine, "sample_rate", sample_rate)
    if engine_sample_rate != sample_rate:
        raise ValueError(
            f"engine sample rate mismatch: requested={sample_rate}, engine={engine_sample_rate}"
        )
    engine_block_frames = getattr(engine, "block_frame", block_frames)
    if engine_block_frames != block_frames:
        raise ValueError(
            f"engine block size mismatch: requested={block_frames}, engine={engine_block_frames}"
        )


def _run_inference(
    engine, requests: queue.Queue, send_frame, sample_rate: int, slots: threading.BoundedSemaphore
) -> None:
    while True:
        item = requests.get()
        if item is None:
            return
        frame, frames, timestamp_ns, flags, pcm, config = item
        try:
            values = struct.unpack("<" + "f" * frames, pcm)
            if flags & AUDIO_FLAG_DISCONTINUOUS:
                reset = getattr(engine, "reset_stream_state", None)
                if reset is not None:
                    reset()
            output = engine.process(values, *config.engine_arguments())
            payload = pack_audio(sample_rate, output, timestamp_ns=timestamp_ns, flags=flags)
            try:
                send_frame(Frame(FrameType.AUDIO_OUT, payload, frame.session_id, frame.sequence,
                                 time.monotonic_ns()))
            except OSError:
                return
        finally:
            slots.release()


def serve_client(sock: socket.socket, runtime: Runtime) -> None:
    sock.settimeout(5.0)
    hello = recv_frame(sock)
    if hello.frame_type is not FrameType.HELLO or hello.session_id or hello.sequence:
        raise ValueError("HELLO required")
    sock.sendall(_hello_ack(runtime))
    opened = recv_frame(sock)
    if opened.frame_type is not FrameType.SESSION_OPEN:
        raise ValueError("SESSION_OPEN required")
    request_id, sample_rate, channels, block_frames, crossfade, extra, model_id = _parse_session_open(opened.payload)
    engine = runtime.create_engine(sample_rate, block_frames, crossfade, extra, model_id)
    _validate_engine_configuration(engine, sample_rate, block_frames)
    session_id = runtime.next_session_id()
    send_lock = threading.Lock()

    def send_frame(frame: Frame) -> None:
        packed = pack_frame(frame)
        with send_lock:
            sock.sendall(packed)

    accepted = struct.pack("<IIIHHIIII", request_id, session_id, sample_rate, channels, 1, block_frames,
                           crossfade, extra, block_frames * 2) + struct.pack("<II", MAX_IN_FLIGHT, 3)
    send_frame(Frame(FrameType.SESSION_ACCEPT, accepted, session_id=session_id))
    requests = queue.Queue(maxsize=MAX_IN_FLIGHT)
    slots = threading.BoundedSemaphore(MAX_IN_FLIGHT)
    session_config = SessionConfigState()
    inference = threading.Thread(
        target=_run_inference,
        args=(engine, requests, send_frame, sample_rate, slots),
        daemon=True,
        name=f"rvc-inference-{session_id}",
    )
    inference.start()
    try:
        while True:
            frame = recv_frame(sock)
            if frame.session_id != session_id:
                raise ValueError("session mismatch")
            if frame.frame_type is FrameType.HEARTBEAT:
                send_frame(Frame(FrameType.HEARTBEAT_ACK, session_id=session_id,
                                 sequence=frame.sequence, timestamp_ns=time.monotonic_ns()))
            elif frame.frame_type is FrameType.CONFIG_UPDATE:
                status = session_config.apply(frame.sequence, frame.payload)
                send_frame(Frame(FrameType.CONFIG_ACK, struct.pack("<I", status), session_id,
                                 frame.sequence, time.monotonic_ns()))
            elif frame.frame_type is FrameType.AUDIO_IN:
                rate, frames, timestamp_ns, flags, pcm = unpack_audio(frame.payload)
                if rate != sample_rate or frames != block_frames:
                    raise ValueError("audio configuration mismatch")
                if slots.acquire(blocking=False):
                    requests.put_nowait(
                        (frame, frames, timestamp_ns, flags, pcm, session_config.snapshot())
                    )
                else:
                    skipped = struct.pack("<II", frame.sequence, 1)
                    send_frame(Frame(FrameType.AUDIO_SKIP, skipped, session_id, frame.sequence,
                                     time.monotonic_ns()))
            elif frame.frame_type is FrameType.CLOSE:
                return
            else:
                raise ValueError(f"unsupported frame: {frame.frame_type.name}")
    finally:
        requests.put(None)


class _HealthHandler(BaseHTTPRequestHandler):
    runtime: Runtime

    def do_GET(self):
        if self.path != "/health":
            self.send_error(404)
            return
        body = json.dumps(self.runtime.health()).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def run_service(
    runtime: Runtime,
    control_port: int,
    stream_port: int,
    *,
    stream_host: str = LOOPBACK,
    enable_control: bool = True,
) -> None:
    http = None
    if enable_control:
        _HealthHandler.runtime = runtime
        http = ThreadingHTTPServer((LOOPBACK, control_port), _HealthHandler)
        threading.Thread(target=http.serve_forever, daemon=True).start()
    try:
        with socket.create_server((stream_host, stream_port), reuse_port=False) as listener:
            while True:
                client, _address = listener.accept()
                threading.Thread(target=_serve_and_close, args=(client, runtime), daemon=True).start()
    finally:
        if http is not None:
            http.shutdown()
            http.server_close()


def _serve_and_close(client: socket.socket, runtime: Runtime) -> None:
    with client:
        try:
            serve_client(client, runtime)
        except (ConnectionError, OSError, ValueError):
            return


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control-port", type=int, default=DEFAULT_CONTROL_PORT)
    parser.add_argument("--stream-port", type=int, default=DEFAULT_STREAM_PORT)
    parser.add_argument(
        "--stream-host",
        default=LOOPBACK,
        help="RSVC audio streamのbind先。Bonjour共有時だけ0.0.0.0を明示する",
    )
    parser.add_argument("--engine-config", help="既存 worker 互換の RVC engine JSON")
    parser.add_argument(
        "--no-control",
        action="store_true",
        help="WebUIがcontrol/healthを提供するときはaudio streamだけlistenする",
    )
    args = parser.parse_args()
    if args.engine_config:
        engine_factory = RvcEngineFactory(args.engine_config)
    else:
        engine_factory = PassthroughEngine
    run_service(
        Runtime(engine_factory),
        args.control_port,
        args.stream_port,
        stream_host=args.stream_host,
        enable_control=not args.no_control,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
