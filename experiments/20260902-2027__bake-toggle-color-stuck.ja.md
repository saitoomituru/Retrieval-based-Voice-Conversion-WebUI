# 実験票: BAKEトグルの色が戻らない不具合の修正(issue #14)

実施時刻: 2026-09-02 20:27 JST(利用者観測), 記録は後追い
対象 Issue: #14
branch / HEAD: gui-macos-portability
実行環境: macOS実機, GarageBand, 実プロジェクト「村祭り」
結果: success (原因確定、修正、build/auval成功。実機再検証は利用者側で今後実施)

## 目的

commit ce061ecで対応したfreeze修正後、REALTIME/BAKEインジケータの表示自体は
正常に動作するようになったが、新たに「トグルの色が一度グレーになると緑に
戻らない」不具合が実機(GarageBand)で報告された。原因を特定し修正する。

## 観測事実

- 利用者提供スクリーンショット(20.28.16)で、REALTIMEラベル自体は正しく
  トグル横に表示されている(v1のfreeze/白紙問題は解消)
- トグルをクリックして色が変化した後、再度色が戻らない(グレーのまま固定)

## 解釈

`IVButtonControl`(基底`IButtonControlBase`)はモーメンタリ(一瞬だけ反応する)
制御であり、`OnMouseDown()`が`SetValue(1.)`を設定した後、内部アニメーションで
自動的に`SetValue(0.)`へ戻る設計(`IControl.cpp`の`IButtonControlBase::OnMouseDown`/
`OnEndAnimation`)。持続的なON/OFF表示を意図した使い方には本来向いておらず、
初回クリック後は常に「オフ(グレー)」相当の見た目に落ち着く。これが報告どおりの
「一度グレーになると戻らない」という挙動と一致する。

## 対応

`IVButtonControl`を、ENGINEトグルと同じ`IVToggleControl`(`ISwitchControlBase`)に
置き換えた。`ISwitchControlBase::OnMouseDown()`は`SetValue(!GetValue())`で
永続的に0/1を反転するのみで、自動リセットアニメーションを持たない。
`offText="REALTIME"`/`onText="BAKE"`をコンストラクタへ直接渡し、専用の
`ITextControl`は不要になった(削除)。`OnIdle()`では`GetRenderingOffline()`との
OR結果が現在値と異なる場合のみ`SetValue()`+`SetDirty(false)`する形にした。

## 実行コマンド

```text
cmake --build build-macos --target RVCRealtime-app --config Debug
cmake --build build-macos --target RVCRealtime-au --config Debug
cp -R build-macos/out/Debug/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Rvcr Rvcp
```

## 結果

APP/AUともbuild成功、auval成功。学習プロセス(PID 5865)は作業中も継続稼働
(11:31経過時点で501.1%)、影響なし。

## 別件: CoreMIDI初期化エラー

同じ利用者スクリーンショットにGarageBand起動時の「CoreMIDIの初期化中に
エラーが起きました。MIDIサービスが使用できません。」ダイアログが写っていたが、
これはmacOSのCoreMIDIサブシステム側のダイアログで、`RVCRealtime`は
`PLUG_DOES_MIDI_IN`/`PLUG_DOES_MIDI_OUT`ともに0でMIDI入出力を持たないため、
本プラグインとは無関係と判断した。

## unknown

- 修正後のトグル色変化の実機再確認(利用者側で今後実施)
