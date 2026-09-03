import unittest
from pathlib import Path

from rvc_realtime_engine import RealtimeEngineConfig


class RealtimeEngineConfigTest(unittest.TestCase):
    def test_config_resolves_paths_and_preserves_worker_contract(self) -> None:
        tmp_path = Path(__file__).parent
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

        self.assertEqual(config.rvc_root, tmp_path.resolve())
        self.assertEqual(config.assets_root, (tmp_path / "assets").resolve())
        self.assertEqual(config.as_worker_mapping()["sample_rate"], 48000)
        self.assertEqual(config.as_worker_mapping()["index_path"], str(tmp_path / "model.index"))

    def test_config_rejects_missing_root(self) -> None:
        with self.assertRaisesRegex(ValueError, "rvc_root"):
            RealtimeEngineConfig.from_mapping({"model_path": "/tmp/model.pth"})

    def test_config_rejects_invalid_timing(self) -> None:
        tmp_path = Path(__file__).parent
        with self.assertRaisesRegex(ValueError, "block_ms"):
            RealtimeEngineConfig.from_mapping(
                {"rvc_root": str(tmp_path), "model_path": str(tmp_path / "m.pth"), "block_ms": 0}
            )

    def test_config_allows_runner_owned_assets_root(self) -> None:
        tmp_path = Path(__file__).parent
        assets = tmp_path / "external-assets"
        config = RealtimeEngineConfig.from_mapping(
            {
                "rvc_root": str(tmp_path),
                "assets_root": str(assets),
                "model_path": str(tmp_path / "model.pth"),
            }
        )
        self.assertEqual(config.assets_root, assets.resolve())
        self.assertEqual(config.as_worker_mapping()["assets_root"], str(assets.resolve()))


if __name__ == "__main__":
    unittest.main()
