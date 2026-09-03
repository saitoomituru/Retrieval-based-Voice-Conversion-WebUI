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
するガードを追加した(**このrepositoryのsubmoduleへの直接編集、後述の
永続化課題あり**)。

## 実行コマンド

```text
sample <hung_pid> 3 -f /tmp/rvc-app-hang-sample.txt
# third_party/iPlug2/IPlug/APP/IPlugAPP_host.cppを編集
cmake --build build-macos --target RVCRealtime-app --config Debug
open build-macos/out/Debug/RVCRealtime.app
sample <new_pid> 2 -f /tmp/rvc-app-fixed-sample.txt
```

## 結果

修正後、メインスレッドは`NSApplication run`(通常のCocoaイベントループ)に
到達しており、ハングは解消された。

## 課題: submoduleへの直接編集は通常のgit commitで追跡されない

`third_party/iPlug2`はpinされたcommitを参照するsubmoduleであり、この
リポジトリの`git add`/`git commit`では中身の変更を追跡できない。
`git submodule update --init`をやり直すと消える。今回はこのセッション内の
テスト用に一時的な編集として残すが、恒久化する場合は以下のいずれかを
検討する必要がある。

- docs/macos-build.ja.mdへ手動パッチ手順として明記し、都度手動適用する
- `scripts/build-macos.sh`にsubmodule初期化後の自動パッチ適用ステップを追加する
- 上流iPlug2側へPLUG_DOES_MIDI_IN/OUTに基づくガードをPRとして提案する

## Recovery / 次の一手

この修正はissue #6のGarageBandフリッカリングの原因究明とは別件。
引き続きGarageBand実機での`diagnostic.log`確認を進める。

## unknown

- このCoreMIDI不具合がこのマシン固有か、より一般的なmacOS環境要因か
- 永続化方法の最終決定(まだ未実施)
