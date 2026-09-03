# RSVC v1 hex fixture（Issue #26）

設計文書 `../stream-protocol-design.md` の test vector を、改行なし 1 行 hex で保存したもの。
本番ソースは含まない。pack 正本は後続 PR1 の `infer/rvc_stream_protocol.py`。

| ファイル | frame | バイト数 | 用途 |
| --- | --- | --- | --- |
| `hello.hex` | HELLO | 51 | T1 先頭 |
| `hello_nak_version.hex` | HELLO_NAK `error_code=1` | 49 | T2 |
| `audio_skip_seq2.hex` | AUDIO_SKIP seq=2 reason=1 | 40 | T6 補助 |
| `audio_in_8frame_48k.hex` | AUDIO_IN 8-frame @48 kHz | 88 | framing。`client_kind=fake_test` のみ |

検証（設計時点、2026-09-03）: 独立 `struct.pack("<IHHIIIIQ", …)` で 4 本とも文書と一致。
