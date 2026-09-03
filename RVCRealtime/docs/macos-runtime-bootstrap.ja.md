# Intel macOS向け外部RVC runtimeの構築

状態: `[ISSUE #6 基盤]` `[CPU MODEL LOAD確認済み]` `[AU連携未実装]`

この文書は、`RVCRealtime`が将来macOSから利用する外部RVC runtimeを、
Intel Mac上で再現するための暫定手順である。WebUIの起動成功をAudio Unit連携や
音声変換成功とは扱わない。

## Issue #6との境界

今回確認した範囲:

- Python 3.12の`.venv`をrepository内へ分離できる
- macOS x86_64で取得可能なPyTorch、FAISS、ONNX Runtimeなどを導入できる
- `configs.config.Config`が`cpu` / `torch.float32`を選択する
- HuBERTとRMVPEのmodel fileを読み込める
- `pip check`で依存破損がない

追加観測として、macOS x86_64ではFAISSをPyTorchより先にimportしたprocessで後続の
PyTorch model loadが`exit 139`になった。既存RVC実行経路はPyTorchを先にimportしており、
同じ順序ではHuBERT/RMVPE loadが成功した。検証scriptも本体と同じimport順を明示する。

今回まだ確認していない範囲:

- Audio Unit bundleからの`worker/rvc_worker.py`起動
- macOS向けprocess/IPC adapter
- 外部`.pth` / `.index`を用いた短いvocal bufferの実変換
- plug-in processへのaudio返却
- Logic Pro / GarageBand / `auval`での実機確認
- latency、dropout、audio thread非blockの測定

したがって、これはIssue #6の受入条件を完了する変更ではなく、その前提となるruntime
構築とmodel loadのreceiptである。

## 確認環境

```text
macOS: 15.7.7 (24G720)
architecture: x86_64
Python: 3.12.7
推論device: cpu
推論dtype: torch.float32
```

Intel macOS向けPyTorch wheelは2.2.2までしか取得できなかったため、上流の
Windows/Linux向け`torch==2.4.1+cpu`指定はそのまま利用できない。ONNX Runtimeも
macOS x86_64で取得できた1.23.2へ固定した。これらはこの実機で観測した配布境界であり、
Apple Siliconや将来releaseの一般的な上限を主張しない。

## runtimeの構築

repository rootで実行する。

```zsh
/usr/local/bin/python3.12 -m venv .venv
.venv/bin/python -m pip install --upgrade 'pip>=24' 'setuptools>=75,<81' wheel
.venv/bin/python -m pip install -r requirements-macos-py312.txt
```

system PythonへRVC依存を混ぜず、`.venv`はGit追跡対象にしない。

## 推論に必要なmodelの取得

HuBERTとRMVPEはruntime依存であり、変換対象のキャラクターモデルではない。
repositoryへcommitしない。

```zsh
.venv/bin/hf download lj1995/VoiceConversionWebUI \
  --revision main \
  --include 'hubert_base/*' \
  --local-dir assets

.venv/bin/hf download lj1995/VoiceConversionWebUI rmvpe.pt \
  --revision main \
  --local-dir assets/rmvpe
```

## 検証

依存とlocal model loadをまとめて確認する。

```zsh
.venv/bin/python scripts/verify_macos_runtime.py --load-models
```

外部RVC modelを使う段階では、利用者が権限を持つ`.pth`を`assets/weights/`、
任意の`.index`を`assets/indices/`へ配置する。model、index、音声素材はこのforkへ
同梱しない。
