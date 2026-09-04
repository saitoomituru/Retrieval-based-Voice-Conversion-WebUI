import array
import unittest

from rvc_stream_protocol import (
    AUDIO_FLAG_OFFLINE,
    AUDIO_HEADER,
    Frame,
    FrameType,
    pack_audio,
    pack_frame,
    unpack_audio,
    unpack_frame,
)


class StreamProtocolTest(unittest.TestCase):
    def test_frame_round_trip_and_little_endian_magic(self):
        raw = pack_frame(Frame(FrameType.HEARTBEAT, b"x", session_id=7, sequence=3))
        self.assertEqual(raw[:4], b"RSVC")
        self.assertEqual(unpack_frame(raw), Frame(FrameType.HEARTBEAT, b"x", 7, 3))

    def test_rejects_bad_crc_and_trailing_bytes(self):
        raw = bytearray(pack_frame(Frame(FrameType.HELLO)))
        raw[20:24] = (1).to_bytes(4, "little")
        with self.assertRaisesRegex(ValueError, "crc32"):
            unpack_frame(bytes(raw))
        with self.assertRaisesRegex(ValueError, "length"):
            unpack_frame(pack_frame(Frame(FrameType.HELLO)) + b"x")

    def test_audio_payload_round_trip(self):
        payload = pack_audio(
            48000, [0.0, 0.25, -0.5], timestamp_ns=9, flags=AUDIO_FLAG_OFFLINE
        )
        rate, frames, timestamp, flags, pcm = unpack_audio(payload)
        self.assertEqual((rate, frames, timestamp, flags), (48000, 3, 9, AUDIO_FLAG_OFFLINE))
        self.assertEqual(array.array("f", pcm).tolist(), [0.0, 0.25, -0.5])

    def test_rejects_unknown_audio_flags(self):
        with self.assertRaisesRegex(ValueError, "audio flags"):
            pack_audio(48000, [0.0], flags=1 << 31)
        payload = bytearray(pack_audio(48000, [0.0]))
        payload[20:24] = (1 << 31).to_bytes(4, "little")
        self.assertEqual(len(payload), AUDIO_HEADER.size + 4)
        with self.assertRaisesRegex(ValueError, "audio flags"):
            unpack_audio(bytes(payload))


if __name__ == "__main__":
    unittest.main()
