import socket
import struct
import json
import sys
import tempfile
import threading
import time
import types
import unittest
from pathlib import Path
from unittest import mock

from rvc_runtime_service import RvcEngineFactory, Runtime, recv_frame, serve_client
from rvc_stream_protocol import (
    AUDIO_FLAG_DISCONTINUOUS,
    AUDIO_FLAG_OFFLINE,
    Frame,
    FrameType,
    pack_audio,
    pack_config_update,
    pack_frame,
    unpack_audio,
)


def text(value: str) -> bytes:
    raw = value.encode()
    return struct.pack("<H", len(raw)) + raw


def open_session(client: socket.socket, *, block_frames: int = 8, model_id: str = "active") -> int:
    hello = struct.pack("<HHIBBH", 1, 1, 0, 1, 0, 4) + b"test" + text("v1")
    client.sendall(pack_frame(Frame(FrameType.HELLO, hello)))
    if recv_frame(client).frame_type is not FrameType.HELLO_ACK:
        raise AssertionError("HELLO_ACK not received")
    session = struct.pack("<IIHHIIII", 1, 48000, 1, 1, block_frames, 0, 0, 0)
    session += text(model_id) + text("") + text("")
    client.sendall(pack_frame(Frame(FrameType.SESSION_OPEN, session)))
    accepted = recv_frame(client)
    if accepted.frame_type is not FrameType.SESSION_ACCEPT:
        raise AssertionError("SESSION_ACCEPT not received")
    return accepted.session_id


class RuntimeServiceTest(unittest.TestCase):
    def test_engine_factory_resolves_web_default_and_au_override_by_opaque_id(self):
        built = []

        class FakeEngine:
            def __init__(self, config):
                self.config = config
                built.append(config)

            def prewarm(self):
                pass

        with tempfile.TemporaryDirectory() as temporary:
            config_path = Path(temporary) / "engine.json"
            config_path.write_text(
                json.dumps({
                    "rvc_root": "/rvc",
                    "model_path": "/models/a.pth",
                    "index_path": "",
                    "default_model_id": "rvc-a",
                    "models": [
                        {"id": "rvc-a", "name": "a.pth", "model_path": "/models/a.pth", "index_path": ""},
                        {"id": "rvc-b", "name": "b.pth", "model_path": "/models/b.pth", "index_path": "/indices/b.index"},
                    ],
                }),
                encoding="utf-8",
            )
            factory = RvcEngineFactory(str(config_path))
            advertised = factory.models()
            self.assertEqual([model["id"] for model in advertised], ["active", "rvc-a", "rvc-b"])
            self.assertNotIn("model_path", advertised[1])
            with self.assertRaisesRegex(ValueError, "unknown runtime model id"):
                factory.create_for_session(48000, 8, 0, 0, model_id="/models/b.pth")
            fake_module = types.SimpleNamespace(RVCStreamEngine=FakeEngine)
            with mock.patch.dict(sys.modules, {"RVCRealtime.worker.rvc_worker": fake_module}):
                factory.create_for_session(48000, 8, 0, 0, model_id="active")
                factory.create_for_session(48000, 8, 0, 0, model_id="rvc-b")
        self.assertEqual(built[0]["model_path"], "/models/a.pth")
        self.assertEqual(built[1]["model_path"], "/models/b.pth")
        self.assertEqual(built[1]["index_path"], "/indices/b.index")

    def test_session_audio_shape_is_passed_to_engine_factory(self):
        calls = []

        class SessionFactory:
            def create_for_session(self, sample_rate, block_frames, crossfade, extra, *, model_id):
                calls.append((sample_rate, block_frames, crossfade, extra, model_id))
                return object()

        engine = Runtime(SessionFactory()).create_engine(48000, 6240, 3840, 96000)
        self.assertIs(type(engine), object)
        self.assertEqual(calls, [(48000, 6240, 3840, 96000, "active")])

    def test_explicit_model_id_is_scoped_to_session_factory(self):
        calls = []

        class SessionFactory:
            def create_for_session(self, *audio_shape, model_id):
                calls.append((audio_shape, model_id))
                return object()

        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        worker = threading.Thread(
            target=serve_client, args=(server, Runtime(SessionFactory())), daemon=True
        )
        worker.start()
        session_id = open_session(client, model_id="rvc-test-model")
        self.assertEqual(calls, [((48000, 8, 0, 0), "rvc-test-model")])
        client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))
        worker.join(1)

    def test_handshake_heartbeat_and_audio(self):
        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        worker = threading.Thread(target=serve_client, args=(server, Runtime()), daemon=True)
        worker.start()
        session_id = open_session(client)
        client.sendall(pack_frame(Frame(FrameType.HEARTBEAT, session_id=session_id, sequence=1)))
        self.assertEqual(recv_frame(client).frame_type, FrameType.HEARTBEAT_ACK)
        payload = pack_audio(48000, [0.0, 0.25, -0.5, 1.0, 0.0, 0.0, 0.0, 0.0])
        client.sendall(pack_frame(Frame(FrameType.AUDIO_IN, payload, session_id, 1)))
        audio = recv_frame(client)
        self.assertEqual(audio.frame_type, FrameType.AUDIO_OUT)
        self.assertEqual(unpack_audio(audio.payload)[1], 8)
        client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))
        worker.join(1)
        self.assertFalse(worker.is_alive())

    def test_offline_flag_is_preserved_in_audio_response(self):
        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        worker = threading.Thread(target=serve_client, args=(server, Runtime()), daemon=True)
        worker.start()
        session_id = open_session(client)
        payload = pack_audio(48000, [0.0] * 8, flags=AUDIO_FLAG_OFFLINE)
        client.sendall(pack_frame(Frame(FrameType.AUDIO_IN, payload, session_id, 1)))
        response = recv_frame(client)
        self.assertEqual(response.frame_type, FrameType.AUDIO_OUT)
        self.assertEqual(unpack_audio(response.payload)[3], AUDIO_FLAG_OFFLINE)
        client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))
        worker.join(1)

    def test_config_update_applies_to_next_audio_block(self):
        calls = []

        class RecordingEngine:
            def process(self, audio, *parameters):
                calls.append(parameters)
                return audio

        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        worker = threading.Thread(
            target=serve_client, args=(server, Runtime(RecordingEngine)), daemon=True
        )
        worker.start()
        session_id = open_session(client)
        config = pack_config_update(12.0, -1.5, 0.75, 0.25, -42.0, 2)
        client.sendall(pack_frame(Frame(FrameType.CONFIG_UPDATE, config, session_id, 1)))
        ack = recv_frame(client)
        self.assertEqual((ack.frame_type, ack.sequence), (FrameType.CONFIG_ACK, 1))
        self.assertEqual(struct.unpack("<I", ack.payload), (0,))
        client.sendall(
            pack_frame(
                Frame(FrameType.AUDIO_IN, pack_audio(48000, [0.0] * 8), session_id, 1)
            )
        )
        self.assertEqual(recv_frame(client).frame_type, FrameType.AUDIO_OUT)
        self.assertEqual(calls, [(12.0, -1.5, 0.75, 0.25, -42.0, 2)])
        client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))
        worker.join(1)

    def test_config_update_rejects_invalid_and_stale_sequences(self):
        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        worker = threading.Thread(target=serve_client, args=(server, Runtime()), daemon=True)
        worker.start()
        session_id = open_session(client)
        valid = pack_config_update(0.0, 0.0, 0.0, 0.5, -60.0, 0)
        invalid = pack_config_update(25.0, 0.0, 0.0, 0.5, -60.0, 0)
        client.sendall(pack_frame(Frame(FrameType.CONFIG_UPDATE, invalid, session_id, 1)))
        self.assertEqual(struct.unpack("<I", recv_frame(client).payload), (1,))
        client.sendall(pack_frame(Frame(FrameType.CONFIG_UPDATE, valid, session_id, 2)))
        self.assertEqual(struct.unpack("<I", recv_frame(client).payload), (0,))
        client.sendall(pack_frame(Frame(FrameType.CONFIG_UPDATE, valid, session_id, 2)))
        self.assertEqual(struct.unpack("<I", recv_frame(client).payload), (2,))
        client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))
        worker.join(1)

    def test_discontinuity_resets_engine_before_processing(self):
        calls = []

        class ResettableEngine:
            def reset_stream_state(self):
                calls.append("reset")

            def process(self, audio, *_args):
                calls.append("process")
                return audio

        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        worker = threading.Thread(
            target=serve_client, args=(server, Runtime(ResettableEngine)), daemon=True
        )
        worker.start()
        session_id = open_session(client)
        payload = pack_audio(48000, [0.0] * 8, flags=AUDIO_FLAG_DISCONTINUOUS)
        client.sendall(pack_frame(Frame(FrameType.AUDIO_IN, payload, session_id, 1)))
        self.assertEqual(recv_frame(client).frame_type, FrameType.AUDIO_OUT)
        self.assertEqual(calls, ["reset", "process"])
        client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))
        worker.join(1)

    def test_runtime_enforces_advertised_max_in_flight(self):
        started = threading.Event()
        release = threading.Event()

        class BlockingEngine:
            def process(self, audio, *_args):
                started.set()
                release.wait(2)
                return audio

        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        self.addCleanup(release.set)
        worker = threading.Thread(
            target=serve_client, args=(server, Runtime(BlockingEngine)), daemon=True
        )
        worker.start()
        session_id = open_session(client)
        payload = pack_audio(48000, [0.0] * 8)
        client.sendall(pack_frame(Frame(FrameType.AUDIO_IN, payload, session_id, 1)))
        self.assertTrue(started.wait(1))
        client.sendall(
            pack_frame(Frame(FrameType.AUDIO_IN, payload, session_id, 2))
            + pack_frame(Frame(FrameType.AUDIO_IN, payload, session_id, 3))
        )
        skipped = recv_frame(client)
        self.assertEqual((skipped.frame_type, skipped.sequence), (FrameType.AUDIO_SKIP, 3))
        self.assertEqual(struct.unpack("<II", skipped.payload), (3, 1))
        release.set()
        self.assertEqual(recv_frame(client).sequence, 1)
        self.assertEqual(recv_frame(client).sequence, 2)
        client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))
        worker.join(1)

    def test_heartbeat_remains_responsive_during_inference(self):
        started = threading.Event()
        release = threading.Event()

        class BlockingEngine:
            def process(self, audio, *_args):
                started.set()
                release.wait(2)
                return audio

        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        self.addCleanup(release.set)
        worker = threading.Thread(
            target=serve_client, args=(server, Runtime(BlockingEngine)), daemon=True
        )
        worker.start()
        session_id = open_session(client)
        payload = pack_audio(48000, [0.0] * 8)
        client.sendall(pack_frame(Frame(FrameType.AUDIO_IN, payload, session_id, 1)))
        self.assertTrue(started.wait(1))

        start = time.monotonic()
        client.sendall(pack_frame(Frame(FrameType.HEARTBEAT, session_id=session_id, sequence=2)))
        heartbeat = recv_frame(client)
        self.assertEqual(heartbeat.frame_type, FrameType.HEARTBEAT_ACK)
        self.assertEqual(heartbeat.sequence, 2)
        self.assertLess(time.monotonic() - start, 0.5)

        release.set()
        self.assertEqual(recv_frame(client).frame_type, FrameType.AUDIO_OUT)
        client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))
        worker.join(1)

    def test_two_sessions_have_independent_engine_state(self):
        created = []

        class StatefulEngine:
            def __init__(self):
                self.calls = 0
                created.append(self)

            def process(self, audio, *_args):
                self.calls += 1
                return [sample + self.calls for sample in audio]

        runtime = Runtime(StatefulEngine)
        connections = [socket.socketpair(), socket.socketpair()]
        workers = []
        for client, server in connections:
            self.addCleanup(client.close)
            self.addCleanup(server.close)
            worker = threading.Thread(target=serve_client, args=(server, runtime), daemon=True)
            worker.start()
            workers.append(worker)

        session_ids = [open_session(client) for client, _server in connections]
        for (client, _server), session_id in zip(connections, session_ids):
            payload = pack_audio(48000, [0.0] * 8)
            client.sendall(pack_frame(Frame(FrameType.AUDIO_IN, payload, session_id, 1)))
            audio = recv_frame(client)
            values = struct.unpack("<8f", unpack_audio(audio.payload)[4])
            self.assertEqual(values, (1.0,) * 8)
            client.sendall(pack_frame(Frame(FrameType.CLOSE, session_id=session_id)))

        for worker in workers:
            worker.join(1)
            self.assertFalse(worker.is_alive())
        self.assertEqual(len(created), 2)
        self.assertIsNot(created[0], created[1])

    def test_session_configuration_must_match_engine(self):
        class ConfiguredEngine:
            sample_rate = 44100
            block_frame = 16

        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        hello = struct.pack("<HHIBBH", 1, 1, 0, 1, 0, 4) + b"test" + text("v1")
        session = struct.pack("<IIHHIIII", 1, 48000, 1, 1, 8, 0, 0, 0)
        session += text("active") + text("") + text("")
        client.sendall(pack_frame(Frame(FrameType.HELLO, hello)) +
                       pack_frame(Frame(FrameType.SESSION_OPEN, session)))

        with self.assertRaisesRegex(ValueError, "engine sample rate mismatch"):
            serve_client(server, Runtime(ConfiguredEngine))


if __name__ == "__main__":
    unittest.main()
