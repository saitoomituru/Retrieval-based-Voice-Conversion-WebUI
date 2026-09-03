"""共有リアルタイム推論エンジンの設定境界。

このモジュールは WebUI、AU/VST worker、将来の LAN runtime が同じ設定を
受け渡すための小さな境界です。音声 callback、プロセス起動、Bonjour、GUI
依存はここへ持ち込みません。
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional


@dataclass(frozen=True)
class RealtimeEngineConfig:
    """RVC 推論に必要な不変設定。

    パスは worker 起動時に解決し、エンジン外からの相対パス解決を避けます。
    ``from_mapping`` は既存 worker の JSON 契約を保つための adapter です。
    """

    rvc_root: Path
    model_path: Path
    index_path: Optional[Path]
    sample_rate: int
    block_ms: float
    crossfade_ms: float
    extra_ms: float

    @classmethod
    def from_mapping(cls, value: Mapping[str, object]) -> "RealtimeEngineConfig":
        def required_path(name: str) -> Path:
            raw = value.get(name)
            if not isinstance(raw, str) or not raw:
                raise ValueError(f"missing realtime engine path: {name}")
            return Path(raw).expanduser().resolve()

        raw_index = value.get("index_path")
        index_path = None
        if isinstance(raw_index, str) and raw_index:
            index_path = Path(raw_index).expanduser().resolve()

        config = cls(
            rvc_root=required_path("rvc_root"),
            model_path=required_path("model_path"),
            index_path=index_path,
            sample_rate=int(value.get("sample_rate", 40000)),
            block_ms=float(value.get("block_ms", 50.0)),
            crossfade_ms=float(value.get("crossfade_ms", 10.0)),
            extra_ms=float(value.get("extra_ms", 0.0)),
        )
        if config.sample_rate <= 0:
            raise ValueError("sample_rate must be positive")
        if config.block_ms <= 0:
            raise ValueError("block_ms must be positive")
        if config.crossfade_ms < 0 or config.extra_ms < 0:
            raise ValueError("crossfade_ms and extra_ms must not be negative")
        return config

    def as_worker_mapping(self) -> dict[str, object]:
        """Return the legacy JSON-shaped mapping during the migration."""

        return {
            "rvc_root": str(self.rvc_root),
            "model_path": str(self.model_path),
            "index_path": str(self.index_path) if self.index_path else "",
            "sample_rate": self.sample_rate,
            "block_ms": self.block_ms,
            "crossfade_ms": self.crossfade_ms,
            "extra_ms": self.extra_ms,
        }
