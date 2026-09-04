# RVC上流PR草案（日本語・繁體中文）

状態: `DRAFT`（未送信）
対象分岐: `saitoomituru:upstream/macos-au-webui-runtime`

## 題名

Intel macOSで学習からGarageBand AUv2まで登攀 / 在Intel macOS從訓練一路登上GarageBand AUv2

## 本文

WindowsのスクリーンショットしかなかったRVC Realtimeを、Intel Macで学習・推論・GarageBand AUv2まで連れてきました。

![GarageBandでRVC AUv2を使い、伴奏付きmixをoffline bounceしている実機画面](https://raw.githubusercontent.com/saitoomituru/Retrieval-based-Voice-Conversion-WebUI/main/assets/fusamofu-img/AUv2inside.png)

🎵 [実演動画：ずんだもん・デルタもん・生声で歌う「村祭」](https://youtube.com/shorts/Y91K-4o8xz0)

🔧 [実験、失敗、GarageBand fault-injectionまで全部残した開発fork](https://github.com/saitoomituru/Retrieval-based-Voice-Conversion-WebUI)

---

### 日本語

`RVCRealtimeVST`を共通の`RVCRealtime`へ整理しました。Windowsは従来のVST2/VST3を残し、macOSではAPPとAUv2をbuildします。

#### 今回できたこと

- Intel macOS x86_64のCPUで学習・推論
- 手元でずんだもん・デルタもんの2モデルを実際に学習し、WebUIとAUで変換
- GarageBandでmodel切替、pitch変更、設定slot復元、標準offline bounce
- WebUIがRVC engine、model、processの死活を管理し、AUは薄いC++ audio headとしてRSVCへ接続
- 再生中のruntimeを3回killするlive-host fault-injectionでもGarageBandは生存し、自動再生成・再接続
- WebUIとAUの双方からmodelを変更可能。AUの明示選択はそのsessionでWebUI既定より優先
- BonjourはmacOSの`dns-sd` / mDNSResponderをそのまま利用。独自のCIDRやsegment制限は生やさない
- GarageBandのoffline renderを検出し、CPU推論を待ってプチプチのないbounceを書き出す

Python、PyTorch、model load、Bonjour探索、blocking networkはDAWのaudio threadへ入れていません。制御serviceと死活管理はWebUIへ集め、realtime audio portだけを分離しています。

<details>
<summary>再現確認</summary>

- 空directoryから`git clone --recursive`成功
- model不要test: 43/43 pass
- clean Release APP/AU build成功
- APP/AU `codesign --verify --deep --strict`成功
- clean build AU: `auval -v aufx Rvcr Rvcp`成功
- GarageBand単trackと伴奏付きmixのoffline bounceを実際に聴取し、変換とdropoutなしを確認

</details>

#### 今回持っていない機材

- Intel CPUのrealtime再生は演算が間に合わずプチプチします。GarageBand標準offline bounceなら綺麗に書き出せます。
- Windows実機、Apple Silicon、Logic Pro、2台目のMacは手元にないので、そこは持っている人に遊んでもらいたいです。
- 2モデルの学習は成功しましたが、この低火力Intel Macで何度も煮直す反復trainingはしていません。
- model、学習音声、indexはそれぞれの著作権・利用条件があるのでPRには載せません。上の動画はcodeの実演で、model配布ではありません。
- iPlug2はMIDI I/OなしAPPの初期化を直したforkを参照しています。この1-file patchはiPlug2側へ別に投げます。

---

### 繁體中文

原本只看到Windows畫面的RVC Realtime，這次在Intel Mac上從訓練、推論一路跑進GarageBand AUv2。

`RVCRealtimeVST`整理為共用的`RVCRealtime`。Windows維持原本的VST2/VST3；macOS新增APP與AUv2 build。

#### 這次完成的事

- 在Intel macOS x86_64的CPU上訓練與推論
- 本地實際訓練ずんだもん與デルタもん兩個模型，並在WebUI與AU完成轉換
- 在GarageBand完成模型切換、pitch調整、設定slot還原與標準offline bounce
- WebUI管理RVC engine、模型與process生命週期；AU作為輕量C++ audio head連接RSVC
- 播放中連續三次kill runtime的live-host fault-injection，GarageBand仍然存活，runtime可自動重建並重新連線
- WebUI與AU都能切換模型；AU明確選擇在該session內優先於WebUI預設值
- Bonjour直接使用macOS的`dns-sd` / mDNSResponder，不另外長出CIDR或網段限制
- 偵測GarageBand offline render，等待CPU推論完成，輸出沒有爆音的bounce

Python、PyTorch、模型載入、Bonjour探索及blocking network都不放進DAW audio thread。控制service與process生命週期集中在WebUI，只有realtime audio使用獨立port。

<details>
<summary>重現確認</summary>

- 從空白目錄執行`git clone --recursive`成功
- 不需模型的test：43/43通過
- clean Release APP/AU build成功
- APP/AU通過`codesign --verify --deep --strict`
- clean build AU通過`auval -v aufx Rvcr Rvcp`
- 實際聆聽GarageBand單軌及含伴奏mix的offline bounce，確認轉換有效且沒有dropout

</details>

#### 這次手上沒有的機材

- Intel CPU即時播放運算不及，會出現爆音；GarageBand標準offline bounce可以平順輸出。
- 手邊沒有Windows實機、Apple Silicon、Logic Pro及第二台Mac，歡迎有機材的人繼續玩。
- 兩個模型都已成功訓練，但沒有在這台低效能Intel Mac上反覆重訓很多次。
- 模型、訓練音訊與index各有著作權及使用條款，所以不放進PR。上面的影片是程式碼實演，不是模型發行物。
- iPlug2目前指向已修正「無MIDI I/O APP初始化」的fork；這個1-file patch會另外送往iPlug2。
