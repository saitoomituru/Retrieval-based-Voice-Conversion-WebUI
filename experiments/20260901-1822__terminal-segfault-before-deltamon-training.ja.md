# 実験・開発ログ: terminal-segfault-before-deltamon-training

実施時刻: 2026-09-01T18:22:03+09:00
対象 Issue: #10（関連: #6、#8）
branch / HEAD: issue-6-macos-runtime-bootstrap / adf33a394302480eb376695eb43b74ef89a2e212
実行環境: macOS 15.7.7 / MacPro7,1 Intel x86_64 / Terminal 2.14
結果: failure

## 目的

デルタもん歌唱モデルのローカル学習準備中に消失したprocessについて、OS crash receiptと
local成果物から中断点を確定し、再開可能な境界を作る。

## 入力・前提

- local source棚: `logs/deltamon_source`（AirDrive上の音声へのsymlinkだけを格納）
- source sample: 65 files / 805.442 seconds
- 学習予定: 40k、F0あり、v2、single speaker
- base model: `assets/pretrained_v2/f0G40k.pth` / `f0D40k.pth`
- silence sample: `logs/mute`
- model、index、source audio、生成音声はGit管理対象外

## 直前に観測されたコマンド

```text
.venv/bin/hf download lj1995/VoiceConversionWebUI \
  --include "pretrained_v2/f0G40k.pth" "pretrained_v2/f0D40k.pth" \
  --revision main --local-dir assets
.venv/bin/hf download lj1995/VoiceConversionWebUI mute.zip \
  --revision main --local-dir .model-downloads
```

## 観測事実

- macOS crash reportのprocessはPythonではなく`Terminal`だった。
- incident ID: `2B1B1697-CA41-4ECF-BD29-6751DA9FEF16`
- exception: `EXC_BAD_ACCESS / SIGSEGV / KERN_INVALID_ADDRESS at 0x0000202020202038`
- faulting threadはTerminalのmain thread。
- 先頭stackは`objc_msgSend`、`-[NSStringMeasurementCacheKey hash]`、`NSCache`、
  AppKit titlebar文字列計測・更新経路だった。
- `f0G40k.pth`は73,106,273 bytes、`f0D40k.pth`は142,875,703 bytesで配置済み。
- `mute.zip`は`unzip -t`で全entryが`OK`。`logs/mute`への展開も完了済み。
- 65 source filesへのsymlinkは18:21:41から18:21:46に作成済み。
- 回収時に`logs/mute`と`logs/deltamon_source`以外のexperiment directory、
  preprocess出力、F0、HuBERT feature、checkpointは存在しなかった。
- したがって、`train/train.py`がepochを開始していた証拠はない。

## 解釈 / 仮説

今回の直接のcrash主体はTerminalであり、既知のFAISS先行importによるPython `SIGSEGV`とは
別の事故である。Terminalのtitle更新中にAppKit内で不正addressへaccessしたことはstackから
観測できるが、どのnotificationまたは表示文字列が誘発したかは未特定である。

利用者の目的上は「モデルを焼く作業中の中断」だが、artifact境界では実学習前のbootstrap中断に
分類する。未開始のepochを「途中まで学習済み」とは扱わない。

## Recovery / 次の一手

1. 完成済みbase assetsを再downloadしない。
2. preprocess、F0、HuBERT、training、indexを別processに分け、各stdout/stderrをfileへ残す。
3. 長時間processをTerminal foreground jobから切り離す。
4. 各段階の終了codeと成果物数を確認してから次段階へ進む。
5. 最終model/index/変換音声はAirDriveへ置き、repositoryへ入れない。

## unknown

- Terminal crashの再現率と具体的なtrigger
- Terminal終了時にforeground childへ送られたsignal
- Intel CPUでの学習所要時間と実用音質
- デルタもんmodelの公開、再配布、upstream先（友人経由で本家確認中）
