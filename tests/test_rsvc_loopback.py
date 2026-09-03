import socket
import unittest

from rvc_stream_protocol import Frame, FrameType, pack_frame, unpack_frame, HEADER


class RsvcLoopbackTest(unittest.TestCase):
    def test_socketpair_round_trip(self):
        left, right = socket.socketpair()
        self.addCleanup(left.close)
        self.addCleanup(right.close)
        expected = Frame(FrameType.HEARTBEAT, b"", session_id=19, sequence=4)
        left.sendall(pack_frame(expected))
        header = right.recv(HEADER.size)
        self.assertEqual(len(header), HEADER.size)
        payload_size = int.from_bytes(header[16:20], "little")
        payload = right.recv(payload_size)
        self.assertEqual(unpack_frame(header + payload), expected)


if __name__ == "__main__":
    unittest.main()
