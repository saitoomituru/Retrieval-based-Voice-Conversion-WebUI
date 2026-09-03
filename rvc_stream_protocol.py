"""RSVC v1 の wire framing（transport 非依存）。"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

MAGIC = 0x43565352
PROTOCOL_VERSION = 1
HEADER = struct.Struct("<IHHIIIIQ")
MAX_PAYLOAD_BYTES = 1 << 20
MAX_FRAMES = 131072


class FrameType(IntEnum):
    HELLO = 0x0001
    HELLO_ACK = 0x0002
    HELLO_NAK = 0x0003
    SESSION_OPEN = 0x0010
    SESSION_ACCEPT = 0x0011
    SESSION_REJECT = 0x0012
    CONFIG_UPDATE = 0x0020
    CONFIG_ACK = 0x0021
    AUDIO_IN = 0x0030
    AUDIO_OUT = 0x0031
    AUDIO_SKIP = 0x0032
    STATE = 0x0040
    HEARTBEAT = 0x0041
    HEARTBEAT_ACK = 0x0042
    ERROR = 0x0050
    CLOSE = 0x0051


@dataclass(frozen=True)
class Frame:
    frame_type: FrameType
    payload: bytes = b""
    session_id: int = 0
    sequence: int = 0
    timestamp_ns: int = 0
    version: int = PROTOCOL_VERSION


def pack_frame(frame: Frame) -> bytes:
    payload = bytes(frame.payload)
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise ValueError("payload exceeds RSVC_MAX_PAYLOAD_BYTES")
    if not 0 <= frame.session_id <= 0xFFFFFFFF or not 0 <= frame.sequence <= 0xFFFFFFFF:
        raise ValueError("session_id and sequence must be uint32")
    return HEADER.pack(
        MAGIC,
        frame.version,
        int(frame.frame_type),
        frame.session_id,
        frame.sequence,
        len(payload),
        0,
        frame.timestamp_ns,
    ) + payload


def unpack_frame(data: bytes) -> Frame:
    if len(data) < HEADER.size:
        raise ValueError("incomplete RSVC header")
    magic, version, frame_type, session_id, sequence, payload_bytes, crc32, timestamp_ns = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ValueError("invalid RSVC magic")
    if version != PROTOCOL_VERSION:
        raise ValueError(f"unsupported RSVC version: {version}")
    if payload_bytes > MAX_PAYLOAD_BYTES:
        raise ValueError("payload exceeds RSVC_MAX_PAYLOAD_BYTES")
    if crc32 != 0:
        raise ValueError("RSVC v1 requires crc32=0")
    if len(data) != HEADER.size + payload_bytes:
        raise ValueError("RSVC payload length mismatch")
    try:
        kind = FrameType(frame_type)
    except ValueError as exc:
        raise ValueError(f"unknown RSVC frame type: {frame_type:#x}") from exc
    return Frame(kind, data[HEADER.size:], session_id, sequence, timestamp_ns, version)


AUDIO_HEADER = struct.Struct("<IHHIQI")


def pack_audio(sample_rate: int, pcm, *, channels: int = 1, timestamp_ns: int = 0, flags: int = 0) -> bytes:
    import array

    values = array.array("f", pcm)
    if channels != 1:
        raise ValueError("RSVC v1 supports mono audio only")
    return AUDIO_HEADER.pack(sample_rate, channels, 1, len(values), timestamp_ns, flags) + values.tobytes()


def unpack_audio(payload: bytes) -> tuple[int, int, int, int, bytes]:
    if len(payload) < AUDIO_HEADER.size:
        raise ValueError("incomplete RSVC audio payload")
    sample_rate, channels, sample_format, frames, timestamp_ns, flags = AUDIO_HEADER.unpack_from(payload)
    if channels != 1 or sample_format != 1:
        raise ValueError("unsupported RSVC audio format")
    pcm = payload[AUDIO_HEADER.size:]
    if len(pcm) != frames * channels * 4:
        raise ValueError("RSVC audio frame length mismatch")
    return sample_rate, frames, timestamp_ns, flags, pcm
