# 実験・開発ログ: macos-runtime-model-load

実施時刻: 2026-09-01T17:57:20+09:00
対象 Issue: #6
branch / HEAD: issue-6-macos-runtime-bootstrap / adf33a394302480eb376695eb43b74ef89a2e212
結果: success

## 目的

PyTorchを先にimportするRVC本体同等順序で依存とHuBERT/RMVPE loadを検証する

## 入力・前提

```text
git status --short:
M webui.py
?? .gitignore
?? RVCRealtimeVST/docs/
?? experiments/20260901-1757__faiss-before-torch-segfault.ja.md
?? requirements-macos-py312.txt
?? scripts/verify_macos_runtime.py
?? start-webui.command
```

## 実行コマンド

```text
.venv/bin/python -u scripts/verify_macos_runtime.py --load-models
```

## 観測事実

事前に`.venv/bin/python -m pip check`を実行し、`No broken requirements found.`を確認した。

exit code: 0

### stdout

```text
platform=macOS-15.7.7-x86_64-i386-64bit
machine=x86_64
python=3.12.7
torch=2.2.2
torchaudio=2.2.2
torchvision=0.17.2
faiss=1.15.0
onnxruntime=1.23.2
gradio=3.14.0
librosa=0.10.2.post1
parselmouth=0.4.7
soundfile=0.14.0
device=cpu
dtype=torch.float32
ffmpeg=/usr/local/bin/ffmpeg
hubert_model=HubertModelWithFinalProj
hubert_normalize=False
rmvpe_model=E2E
result=success
```

### stderr

```text
<repo>/.venv/lib/python3.12/site-packages/gradio/routes.py:23: UserWarning: pkg_resources is deprecated as an API. See https://setuptools.pypa.io/en/latest/pkg_resources.html. The pkg_resources package is slated for removal as early as 2025-11-30. Refrain from using this package or pin to Setuptools<81.
  import pkg_resources
```

## 解釈 / 仮説

RVC本体と同じくPyTorchをFAISSより先にimportするprocessでは、依存import、CPU設定、
HuBERT load、RMVPE loadが完了した。Gradio 3.14の`pkg_resources`警告は出たが、
上流依存定義どおり`setuptools<81`でAPIは残っており、今回のmodel loadを妨げなかった。

## Recovery / 次の一手

利用権限のある外部RVC `.pth` / `.index`を使い、`infer.rtrvc.RVC`の短いbuffer実変換を
別実験として行う。その後、Issue #3のmacOS process/IPC adapterとIssue #6のworker起動へ接続する。

## unknown

外部voice modelのload、変換音声、処理時間、latency、dropout、Audio Unitからの起動、
Logic Pro / GarageBandでの挙動は未確認。この票の`success`はruntime依存とbase model loadだけを指す。
