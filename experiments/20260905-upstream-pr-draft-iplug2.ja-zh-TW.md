# iPlug2上流PR草案（日本語・繁體中文）

状態: `DRAFT`（未送信）

対象分岐: `saitoomituru:fix/skip-midi-init-without-midi`

## 題名

APP: MIDI入出力なしではMIDI初期化をスキップする / APP：無MIDI I/O時跳過MIDI初始化

## 本文

### 日本語

`PLUG_DOES_MIDI_IN == 0`かつ`PLUG_DOES_MIDI_OUT == 0`のAPP targetで、不要なRtMidi/CoreMIDI初期化をスキップします。

変更は`IPlug/APP/IPlugAPP_host.cpp`だけです。

- `IPlugAPPHost::Init()`で`InitMidi()`、`ProbeMidiIO()`、入出力`SelectMIDIDevice()`をMIDI I/O macroでguardします。
- `IPlugAPPHost::InitMidi()`にも同じ条件の早期returnを置きます。

MIDIなしplug-inでは`mMidiIn` / `mMidiOut`が生成されないまま`SelectMIDIDevice()`が呼ばれ、device名が偶然`OFF_TEXT`と一致しない場合にnull objectを参照できます。またmacOSでは、不要な`MIDIClientCreate()`が応答を返さずAPP起動を停止した事例がありました。

MIDI入力または出力を宣言する既存APPの処理順は変更しません。

検証:

- MIDI I/OなしのRVCRealtime APP targetをIntel macOS / Xcode 16.2でclean Release build
- 同じsource treeのAUv2 targetもbuild
- APP/AU成果物のcodesign検査とAUの`auval`全項目合格

この分岐は最新`upstream/master`をmerge済みです。差分は1 file、17 insertions / 1 deletionです。

### 繁體中文

當APP target同時滿足`PLUG_DOES_MIDI_IN == 0`與`PLUG_DOES_MIDI_OUT == 0`時，跳過不必要的RtMidi/CoreMIDI初始化。

本變更只修改`IPlug/APP/IPlugAPP_host.cpp`。

- 在`IPlugAPPHost::Init()`中，以MIDI I/O macro保護`InitMidi()`、`ProbeMidiIO()`及輸入／輸出的`SelectMIDIDevice()`。
- 在`IPlugAPPHost::InitMidi()`加入相同條件的early return。

無MIDI的plug-in不會建立`mMidiIn` / `mMidiOut`；若device名稱沒有剛好等於`OFF_TEXT`，後續`SelectMIDIDevice()`可能解參照null object。此外，在macOS上曾觀察到不必要的`MIDIClientCreate()`沒有返回，導致APP啟動停住。

只要APP宣告MIDI輸入或輸出，既有初始化順序維持不變。

驗證：

- 在Intel macOS / Xcode 16.2上clean Release build一個無MIDI I/O的RVCRealtime APP target
- 同一source tree的AUv2 target亦成功build
- APP/AU成果物通過codesign檢查，AU通過`auval`全部項目

此分支已merge最新`upstream/master`。差分為1個檔案、17 additions / 1 deletion。

