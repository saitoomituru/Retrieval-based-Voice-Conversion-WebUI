# 実験票: スタンドアロンAPPのCoreMIDIハングを特定・回避(issue #14関連)

実施時刻: 2026-09-03 12:49 JST
対象 Issue: なし(新規issue化を検討)、issue #6のGarageBandフリッカリング調査から派生
branch / HEAD: gui-macos-portability
実行環境: macOS実機

## 目的

利用者が`RVCRealtime.app`(スタンドアロン)を起動したところ、Dockアイコンが
数分間バウンスし続け(「キョンシー状態」)、起動が完了しなかった。原因を
特定する。

## 観測事実

`sample`コマンドでプロセス(PID 91106)を3秒サンプリングしたところ、
実行時間はわずか1msで、メインスレッドは以下のスタックで完全にブロック
されていた:

```
main → NSApplicationMain → SWELLAppMain → IPlugAPPHost::Init()
  → IPlugAPPHost::InitMidi() → RtMidiIn constructor → MidiInCore::initialize
  → MIDIClientCreate(CoreMIDI) → mach_msg2_trap (無限待機)
```

## 解釈 / 仮説

`third_party/iPlug2/IPlug/APP/IPlugAPP_host.cpp`の`InitMidi()`は、
`mNoIO`/`IsScreenshotMode()`以外の条件では無条件でRtMidiIn/RtMidiOutを
生成し、CoreMIDIへ接続しようとする。このマシンではCoreMIDIサブシステムに
何らかの不具合があり(以前GarageBand起動時に見た「CoreMIDIの初期化中に
エラーが起きました」ダイアログと同じ症状と推測)、`MIDIClientCreate`が
エラーを返さずタイムアウトなしに無限ブロックする。

`RVCRealtime`は`config.h`で`PLUG_DOES_MIDI_IN`/`PLUG_DOES_MIDI_OUT`とも0
(MIDI非対応)だが、`InitMidi()`はこれを見ずに常にMIDI初期化を試みる設計
だった。

この問題はAPP(スタンドアロン)ホスト固有のコードパスであり、AUv2ホスティング
(GarageBand/Logic経由)は別のコード経路(RtMidiを使わない)のため、issue #6の
GarageBand実機フリッカリングとは無関係と判断した。

## 対応

`third_party/iPlug2/IPlug/APP/IPlugAPP_host.cpp`の`InitMidi()`へ、
`#if !PLUG_DOES_MIDI_IN && !PLUG_DOES_MIDI_OUT`の場合は即座に`return true`
するガードを追加した。修正はiPlug2 forkの
`fix/skip-midi-init-without-midi` branchへcommitし、このrepositoryの
submoduleをそのcommitへ固定した。

## 実行コマンド

```text
sample <hung_pid> 3 -f /tmp/rvc-app-hang-sample.txt
# iPlug2 forkでIPlug/APP/IPlugAPP_host.cppを編集
cmake --build build-macos --target RVCRealtime-app --config Debug
open build-macos/out/Debug/RVCRealtime.app
sample <new_pid> 2 -f /tmp/rvc-app-fixed-sample.txt
```

## 結果

修正後、メインスレッドは`NSApplication run`(通常のCocoaイベントループ)に
到達しており、ハングは解消された。

## 恒久化

修正は次へ保存した。

- fork: `https://github.com/saitoomituru/iPlug2`
- branch: `fix/skip-midi-init-without-midi`
- commit: `256bd6b62b7a2bcdc30acdd5a33b4b3e132a4d0f`
- 親repository: `.gitmodules`をfork URLへ変更し、上記commitをpin

親repository内へ一時パッチを重複保持せず、iPlug2上流へ独立してPRできる
境界にした。修正後は次のRelease buildを再実行し、いずれも成功した。

```text
cmake --build RVCRealtime/build-macos --config Release --target RVCRealtime-app
cmake --build RVCRealtime/build-macos --config Release --target RVCRealtime-au
```

## Recovery / 次の一手

この修正はissue #6のGarageBandフリッカリングの原因究明とは別件。
iPlug2上流へ提出する場合は、MIDI I/Oを持たないAPP targetでCoreMIDIを
初期化しない一般修正としてPRを作成する。

## unknown

- このCoreMIDI不具合がこのマシン固有か、より一般的なmacOS環境要因か
- iPlug2上流での受入可否
