from pathlib import Path

import pytest

from rvc_realtime_engine import RealtimeEngineConfig


def test_config_resolves_paths_and_preserves_worker_contract(tmp_path: Path) -> None:
    config = RealtimeEngineConfig.from_mapping(
        {
            "rvc_root": str(tmp_path),
            "model_path": str(tmp_path / "model.pth"),
            "index_path": str(tmp_path / "model.index"),
            "sample_rate": 48000,
            "block_ms": 40,
            "crossfade_ms": 8,
            "extra_ms": 12,
        }
    )

    assert config.rvc_root == tmp_path.resolve()
    assert config.as_worker_mapping()["sample_rate"] == 48000
    assert config.as_worker_mapping()["index_path"] == str(tmp_path / "model.index")


def test_config_rejects_missing_root() -> None:
    with pytest.raises(ValueError, match="rvc_root"):
        RealtimeEngineConfig.from_mapping({"model_path": "/tmp/model.pth"})


def test_config_rejects_invalid_timing(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="block_ms"):
        RealtimeEngineConfig.from_mapping(
            {"rvc_root": str(tmp_path), "model_path": str(tmp_path / "m.pth"), "block_ms": 0}
        )
