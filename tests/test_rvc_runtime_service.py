import socket
import struct
import threading
import time
import unittest

from rvc_runtime_service import Runtime, recv_frame, serve_client
from rvc_stream_protocol import (
    AUDIO_FLAG_DISCONTINUOUS,
    AUDIO_FLAG_OFFLINE,
    Frame,
    FrameType,
    pack_audio,
    pack_frame,
    unpack_audio,
)


def text(value: str) -> bytes:
    raw = value.encode()
    return struct.pack("<H", len(raw)) + raw


def open_session(client: socket.socket, *, block_frames: int = 8) -> int:
    hello = struct.pack("<HHIBBH", 1, 1, 0, 1, 0, 4) + b"test" + text("v1")
    client.sendall(pack_frame(Frame(FrameType.HELLO, hello)))
    if recv_frame(client).frame_type is not FrameType.HELLO_ACK:
        raise AssertionError("HELLO_ACK not received")
    session = struct.pack("<IIHHIIII", 1, 48000, 1, 1, block_frames, 0, 0, 0)
    session += text("active") + text("") + text("")
    client.sendall(pack_frame(Frame(FrameType.SESSION_OPEN, session)))
    accepted = recv_frame(client)
    if accepted.frame_type is not FrameType.SESSION_ACCEPT:
        raise AssertionError("SESSION_ACCEPT not received")
    return accepted.session_id


class RuntimeServiceTest(unittest.TestCase):
    def test_session_audio_shape_is_passed_to_engine_factory(self):
        calls = []

        class SessionFactory:
            def create_for_session(self, sample_rate, block_frames, crossfade, extra):
                calls.append((sample_rate, block_frames, crossfade, extra))
                return object()

        engine = Runtime(SessionFactory()).create_engine(48000, 6240, 3840, 96000)
        self.assertIs(type(engine), object)
        self.assertEqual(calls, [(48000, 6240, 3840, 96000)])

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
