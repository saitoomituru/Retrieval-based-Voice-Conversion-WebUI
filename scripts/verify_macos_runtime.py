#!/usr/bin/env python3
"""Intel macOS向けRVC runtimeの依存とmodel loadを確認する。"""

from __future__ import annotations

import argparse
import platform
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_VERSIONS = {
    "torch": "2.2.2",
    "torchaudio": "2.2.2",
    "torchvision": "0.17.2",
    "onnxruntime": "1.23.2",
}


def require_file(path: Path) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"必要なfileがありません: {path.relative_to(ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="macOS x86_64向けRVC runtimeを検証します。"
    )
    parser.add_argument(
        "--load-models",
        action="store_true",
        help="localのHuBERTとRMVPEを実際に読み込みます。",
    )
    args = parser.parse_args()

    # configs.configはRVC本体の引数をparseするため、検証scriptの引数を渡さない。
    sys.argv = [sys.argv[0]]
    sys.path.insert(0, str(ROOT))

    # macOS x86_64ではFAISSを先に読み込むと後続のPyTorch model loadが
    # segmentation faultになった。RVC本体と同じ順序を検証でも固定する。
    import torch
    import faiss
    import gradio
    import librosa
    import onnxruntime
    import parselmouth
    import soundfile
    import torchaudio
    import torchvision
    from configs.config import Config

    modules = {
        "torch": torch,
        "torchaudio": torchaudio,
        "torchvision": torchvision,
        "onnxruntime": onnxruntime,
    }
    for name, expected in EXPECTED_VERSIONS.items():
        actual = modules[name].__version__
        if actual != expected:
            raise RuntimeError(f"{name}: 期待={expected}, 実際={actual}")

    config = Config()
    if config.device != "cpu" or config.dtype != torch.float32:
        raise RuntimeError(
            f"想定外の推論設定です: device={config.device}, dtype={config.dtype}"
        )
    if not shutil.which("ffmpeg"):
        raise RuntimeError("ffmpegがPATHにありません。")

    print(f"platform={platform.platform()}")
    print(f"machine={platform.machine()}")
    print(f"python={platform.python_version()}")
    print(f"torch={torch.__version__}")
    print(f"torchaudio={torchaudio.__version__}")
    print(f"torchvision={torchvision.__version__}")
    print(f"faiss={faiss.__version__}")
    print(f"onnxruntime={onnxruntime.__version__}")
    print(f"gradio={gradio.__version__}")
    print(f"librosa={librosa.__version__}")
    print(f"parselmouth={parselmouth.__version__}")
    print(f"soundfile={soundfile.__version__}")
    print(f"device={config.device}")
    print(f"dtype={config.dtype}")
    print(f"ffmpeg={shutil.which('ffmpeg')}")

    if args.load_models:
        hubert_dir = ROOT / "assets" / "hubert_base"
        rmvpe_path = ROOT / "assets" / "rmvpe" / "rmvpe.pt"
        require_file(hubert_dir / "config.json")
        require_file(hubert_dir / "preprocessor_config.json")
        require_file(hubert_dir / "pytorch_model.bin")
        require_file(rmvpe_path)

        from infer.hubert import (
            hubert_audio_requires_normalization,
            load_hubert_model,
        )
        from infer.rmvpe import RMVPE

        hubert = load_hubert_model("cpu", False)
        rmvpe = RMVPE(str(rmvpe_path), False, "cpu")
        print(f"hubert_model={hubert.__class__.__name__}")
        print(f"hubert_normalize={hubert_audio_requires_normalization()}")
        print(f"rmvpe_model={rmvpe.model.__class__.__name__}")

    print("result=success")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
