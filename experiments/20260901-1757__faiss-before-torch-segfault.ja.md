# 実験・開発ログ: faiss-before-torch-segfault

実施時刻: 2026-09-01T17:57:03+09:00
対象 Issue: #6
branch / HEAD: issue-6-macos-runtime-bootstrap / adf33a394302480eb376695eb43b74ef89a2e212
結果: failure

## 目的

Intel macOSでnative libraryのimport順序を切り分ける

## 入力・前提

```text
git status --short:
M webui.py
?? .gitignore
?? RVCRealtimeVST/docs/
?? requirements-macos-py312.txt
?? scripts/verify_macos_runtime.py
?? start-webui.command
```

## 実行コマンド

```text
.venv/bin/python -u -c "import faiss; from infer.hubert import load_hubert_model; load_hubert_model(\"cpu\", False)"
```

## 観測事実

exit code: -11

### stdout

```text
(出力なし)
```

### stderr

```text
(出力なし)
```

## 解釈 / 仮説

`exit code -11`はPOSIXの`SIGSEGV`に対応する。Python exceptionとstderrがないため、
FAISSを先にloadした後のPyTorch native library初期化でprocessが落ちた可能性がある。
ただし、衝突した具体的なlibraryやsymbolまでは特定していない。

## Recovery / 次の一手

RVC本体の既存順序に合わせ、PyTorchをFAISSより先にimportする。同じmodelをこの順序で
再試験し、成功receiptを別票へ残す。

## unknown

macOS x86_64の他環境、別FAISS/PyTorch組合せ、Apple Siliconでも同じ順序依存が起きるかは不明。
