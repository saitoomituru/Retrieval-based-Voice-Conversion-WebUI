# Intel macOSで学習からGarageBand AUv2まで登攀 / 在Intel macOS從訓練一路登上GarageBand AUv2

## 作者旗 / 作者標記

- 実装・実機検証 / 實作與實機驗證: [齋藤みつる / saitoomituru](https://github.com/saitoomituru)
- 開発支援 / 開發協作: Codex / Grok / Gemini
- 実装正本 / 實作正本: [`saitoomituru:upstream/macos-au-webui-runtime`](https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/compare/main...saitoomituru:Retrieval-based-Voice-Conversion-WebUI:upstream/macos-au-webui-runtime)

現在このrepositoryでPull Requestが無効のため、実装済み分岐をIssueで共有します。採用する場合は、commit authorまたはChangelog上の出典を保持してください。

目前此repository已停用Pull Request，因此在Issue中分享已完成的實作分支。若採用此實作，請保留commit author，或在Changelog中標示來源。

![GarageBandでRVC AUv2を使い、伴奏付きmixをoffline bounceしている実機画面](https://raw.githubusercontent.com/saitoomituru/Retrieval-based-Voice-Conversion-WebUI/main/assets/fusamofu-img/AUv2inside.png)

🎵 [実演動画：ずんだもん・デルタもん・生声で歌う「村祭」](https://youtube.com/shorts/Y91K-4o8xz0)

🔧 [実験、失敗、GarageBand fault-injectionまで残したfork](https://github.com/saitoomituru/Retrieval-based-Voice-Conversion-WebUI)

## 日本語

WindowsのスクリーンショットしかなかったRVC Realtimeを、Intel Macで学習・推論・GarageBand AUv2まで連れてきました。

### 今回できたこと

- `RVCRealtimeVST`を共通の`RVCRealtime`へ整理し、WindowsはVST2/VST3を維持、macOSはAPP/AUv2をbuild
- Intel macOS x86_64のCPUで学習・推論
- 手元でずんだもん・デルタもんの2モデルを実際に学習し、WebUIとAUで変換
- GarageBandでmodel切替、pitch変更、設定slot復元、標準offline bounce
- WebUIがRVC engine、model、processの死活を管理し、AUは薄いC++ audio headとしてRSVCへ接続
- 再生中のruntimeを3回killするlive-host fault-injectionでもGarageBandは生存し、自動再生成・再接続
- WebUIとAUの双方からmodelを変更。AUの明示選択はそのsessionでWebUI既定より優先
- BonjourはmacOSの`dns-sd` / mDNSResponderをそのまま使用し、application独自のCIDRやsegment制限は追加しない
- GarageBandのoffline renderを検出し、CPU推論を待ってdropoutのないbounceを書き出す

Python、PyTorch、model load、Bonjour探索、blocking networkはDAWのaudio threadへ入れていません。制御serviceと死活管理はWebUIへ集め、realtime audio portだけを分離しています。

### 再現確認

- 空directoryから`git clone --recursive`成功
- model不要test: 43/43 pass
- clean Release APP/AU build成功
- APP/AU `codesign --verify --deep --strict`成功
- clean build AU: `auval -v aufx Rvcr Rvcp`成功
- GarageBand単trackと伴奏付きmixのoffline bounceを実際に聴取し、変換とdropoutなしを確認

### 手元にない機材

- Intel CPUのrealtime再生は演算が間に合わずプチプチします。GarageBand標準offline bounceなら綺麗に書き出せます。
- Windows実機、Apple Silicon、Logic Pro、2台目のMacは手元にありません。機材を持っている人に続きを試してもらいたいです。
- 2モデルの学習は成功しましたが、このIntel Macで反復trainingを何度も行う品質保証はしていません。
- model、学習音声、indexは著作権・利用条件があるため含めません。動画はcodeの実演であり、model配布ではありません。
- iPlug2はMIDI I/OなしAPP初期化を修正したforkを参照しています。この1-file patchはiPlug2側へ別途提出します。

## 繁體中文

原本只看到Windows畫面的RVC Realtime，這次在Intel Mac上從訓練、推論一路跑進GarageBand AUv2。

### 這次完成的事

- 將`RVCRealtimeVST`整理為共用`RVCRealtime`；Windows維持VST2/VST3，macOS建置APP/AUv2
- 在Intel macOS x86_64 CPU上進行訓練與推論
- 本地實際訓練ずんだもん與デルタもん兩個模型，並在WebUI與AU完成轉換
- 在GarageBand完成模型切換、pitch調整、設定slot還原與標準offline bounce
- WebUI管理RVC engine、模型與process生命週期；AU作為輕量C++ audio head連接RSVC
- 播放中連續三次kill runtime的live-host fault-injection，GarageBand仍然存活，runtime可自動重建並重新連線
- WebUI與AU都能切換模型；AU明確選擇在該session內優先於WebUI預設
- Bonjour直接使用macOS的`dns-sd` / mDNSResponder，不另外增加CIDR或網段限制
- 偵測GarageBand offline render，等待CPU推論完成，輸出沒有dropout的bounce

Python、PyTorch、模型載入、Bonjour探索及blocking network都不放進DAW audio thread。控制service與process生命週期集中在WebUI，只有realtime audio使用獨立port。

### 重現確認

- 從空白目錄執行`git clone --recursive`成功
- 不需模型的test：43/43通過
- clean Release APP/AU build成功
- APP/AU通過`codesign --verify --deep --strict`
- clean build AU通過`auval -v aufx Rvcr Rvcp`
- 實際聽取GarageBand單軌及含伴奏mix的offline bounce，確認轉換有效且沒有dropout

### 這次手上沒有的機材

- Intel CPU即時播放運算不及，會出現爆音；GarageBand標準offline bounce可以平順輸出。
- 手邊沒有Windows實機、Apple Silicon、Logic Pro及第二台Mac，歡迎有機材的人繼續測試。
- 兩個模型都已成功訓練，但沒有在這台低效能Intel Mac上反覆重訓多次。
- 模型、訓練音訊與index各有著作權及使用條款，因此不放進Issue或分支。上面的影片是程式碼實演，不是模型發行物。
- iPlug2目前指向已修正「無MIDI I/O APP初始化」的fork；這個1-file patch會另外送往iPlug2。
