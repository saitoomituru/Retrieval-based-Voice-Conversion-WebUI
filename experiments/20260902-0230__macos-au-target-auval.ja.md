# 実験票: macOS AUv2ターゲット追加とauval検証

実施時刻: 2026-09-02 02:30 JST
対象 Issue: #5
branch / HEAD: gui-macos-portability (worktree: /Users/saitoumitsuru/RVC-WebUI-gui)
実行環境: macOS 15.2, Xcode 16.2, cmake 3.31.3, Apple clang, x86_64
結果: success

## 目的

Issue #5「iPlug2でAudio Unitターゲットを追加する」の受入条件（RVC workerが未動作でも
plug-inをAudio Unitとしてbuildでき、基本validation/loadを通過する）を満たすか確認する。
先行実験 `20260902-0012__macos-gui-app-build.ja.md` でAPP形式のGUI目視確認は完了済み。

## 入力・前提

- CMakeLists.txtのRVC_FORMATS_DEFAULTはmacOSでAPPのみだった
- AUv2 (.component) はiPlug2側でApple system framework (AudioUnit/CoreAudioKit) のみを使い、
  VST2/VST3のような外部SDK取得は不要（`third_party/iPlug2/Documentation/cmake.md`で確認）
- `resources/RVCRealtime-AU-Info.plist` と `config.h` の `AUV2_*` マクロが未整備だった

## 実行コマンド

```text
# CMakeLists.txt: RVC_FORMATS_DEFAULT (非Windows) を APP から APP AU へ変更
# resources/RVCRealtime-AU-Info.plist を新規作成
# config.h に AUV2_ENTRY/AUV2_ENTRY_STR/AUV2_FACTORY/AUV2_VIEW_CLASS/AUV2_VIEW_CLASS_STR を追加

cmake -S . -B build-macos -G Xcode
cmake --build build-macos --target RVCRealtime-au --config Debug

mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -R build-macos/out/Debug/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Rvcr Rvcp
```

## 観測事実

- `cmake --build ... --target RVCRealtime-au` は `** BUILD SUCCEEDED **` で完走。
  APP形式追加時に解消したWin32依存/WorkerClientスタブの改修がそのまま流用でき、
  AU固有の追加コード修正は不要だった（Info.plist/config.hマクロの追加のみ）。
- `auval -v aufx Rvcr Rvcp` は `AU VALIDATION SUCCEEDED` で完走。
  OPEN TIMES、Channel I/O parser、AudioChannelLayout、Render Tests（複数frame数/sample rate）、
  1ch/1→2ch変換、parameter設定（AudioUnitSetParameter/ScheduleParameter）、MIDI、
  いずれも `PASS`。
- `.component` はビルド後に自動配置されないため（`IPLUG_DEPLOY_PLUGINS OFF`）、
  `~/Library/Audio/Plug-Ins/Components/` へ手動copyしてauvalへ認識させた。
- 学習プロセス（PID 55307, `/Users/saitoumitsuru/RVC-WebUI`）はauval実行中もCPU使用率・
  経過時間とも継続しており（2:22:35経過時点でも522%稼働）影響を受けていない。

## 解釈 / 仮説

Issue #5の受入条件文言（「RVC workerがまだ完全動作しなくても、plug-inをAudio Unitとして
buildでき、基本validationとloadを通過すること」）はこの実験のみで満たしたと判断できる。
ただしこれは`auval`によるヘッドレス検証であり、Logic Pro / GarageBandでの実機ロード確認
（Issue #7）はまだ別途必要。

## Recovery / 次の一手

- Issue #5をこの結果でクローズ候補としてコメント報告する。
- 次はIssue #7（Logic Pro / GarageBandのスモークテスト）へ進められる。
- `~/Library/Audio/Plug-Ins/Components/RVCRealtime.component` はローカル検証用の手動配置。
  正式な配布/インストーラ導線は別途検討する。

## unknown

- Logic Pro / GarageBand実機でのロード・操作性は未確認。
- Windows側ビルド（VST2/VST3）がGUI/WorkerClient変更で壊れていないかは依然未検証。
- Apple Silicon実機での挙動は未確認（今回もx86_64のみ）。
