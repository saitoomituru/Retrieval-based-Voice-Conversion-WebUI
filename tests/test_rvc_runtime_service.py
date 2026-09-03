import socket
import struct
import threading
import unittest

from rvc_runtime_service import Runtime, recv_frame, serve_client
from rvc_stream_protocol import Frame, FrameType, pack_audio, pack_frame, unpack_audio


def text(value: str) -> bytes:
    raw = value.encode()
    return struct.pack("<H", len(raw)) + raw


class RuntimeServiceTest(unittest.TestCase):
    def test_handshake_heartbeat_and_audio(self):
        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        worker = threading.Thread(target=serve_client, args=(server, Runtime()), daemon=True)
        worker.start()
        hello = struct.pack("<HHIBBH", 1, 1, 0, 1, 0, 4) + b"test" + text("v1")
        client.sendall(pack_frame(Frame(FrameType.HELLO, hello)))
        self.assertEqual(recv_frame(client).frame_type, FrameType.HELLO_ACK)
        session = struct.pack("<IIHHIIII", 1, 48000, 1, 1, 8, 0, 0, 0) + text("active") + text("") + text("")
        client.sendall(pack_frame(Frame(FrameType.SESSION_OPEN, session)))
        accepted = recv_frame(client)
        self.assertEqual(accepted.frame_type, FrameType.SESSION_ACCEPT)
        session_id = accepted.session_id
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


if __name__ == "__main__":
    unittest.main()
