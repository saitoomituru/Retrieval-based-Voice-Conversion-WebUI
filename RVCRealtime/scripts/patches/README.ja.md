# third_party/iPlug2への局所パッチ

`third_party/iPlug2`はpinされたcommitを参照するgit submoduleであり、この
リポジトリの通常のcommitでは中身の変更を追跡できない。`git submodule update`を
実行すると、submodule内への直接編集は失われる。

このディレクトリのパッチは`scripts/build-macos.sh`実行時に自動・冪等に適用される
(既に当たっていれば再適用しない)。手動で当てる場合:

```zsh
git -C third_party/iPlug2 apply ../../scripts/patches/<file>.patch
```

## 一覧

- `iplug2-skip-no-midi-init.patch`: `IPlugAPPHost::InitMidi()`が
  `PLUG_DOES_MIDI_IN`/`PLUG_DOES_MIDI_OUT`を見ずに無条件でRtMidiIn/RtMidiOutを
  生成し、CoreMIDIへ接続しようとする。少なくとも1台のmacOS実機で
  `MIDIClientCreate()`がエラーを返さずタイムアウトなしに無限ブロックし、
  スタンドアロンAPP起動全体がハングする現象を確認した(RVCRealtimeは
  `config.h`でMIDI非対応)。両方0の場合は`InitMidi()`を早期returnする。
  詳細: `experiments/20260903-1249__standalone-coremidi-hang.ja.md`
