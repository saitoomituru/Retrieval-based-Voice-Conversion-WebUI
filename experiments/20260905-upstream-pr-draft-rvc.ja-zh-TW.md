# RVC上流PR草案（日本語・繁體中文）

状態: `DRAFT`（未送信）

対象分岐: `saitoomituru:upstream/macos-au-webui-runtime`

## 題名

macOS / AUv2対応とWebUI所有realtime runtimeを追加する / 新增macOS、AUv2與WebUI管理的即時runtime

## 本文

### 日本語

このPRは、Windows向けWebUI・VST実装を基礎に、Intel macOSで学習・推論・GarageBand AUv2まで動作する経路を追加します。

主な変更:

- `RVCRealtimeVST`を`RVCRealtime`へ改称し、Windowsでは従来のVST2/VST3、macOSではAPP/AUv2をbuildします。
- AUを薄いC++ audio headとし、Python、PyTorch、model load、Bonjour探索をDAWのaudio threadへ持ち込みません。
- WebUIがRSVC runner、health、bounded restart、model registry、control gatewayを所有します。音声portは制御面から分離します。
- WebUIとAUの双方からmodelを選択できます。AUが明示したopaque model IDは、そのAU sessionでWebUI既定より優先されます。
- BonjourはmacOSの`dns-sd` / mDNSResponderが見せる`.local` serviceを広告・探索・解決します。application独自のCIDRやsegment制限は追加していません。
- hostのoffline render状態をAUからRSVCへ伝え、GarageBand標準offline bounceでは推論完了を待ちます。
- macOS x86_64向けPython 3.12依存とCPU学習・推論のplatform差分を追加します。
- runtime/protocol/lifecycle/Bonjour/model選択にmodel不要の自動テストを追加します。

検証（Intel Mac、macOS 15.7.7 / Xcode 16.2）:

- push済み分岐を空の一時directoryへ`git clone --recursive`し、全submoduleの固定revision取得を確認
- `python -m unittest discover -s tests`: 43/43合格（model・再学習不要）
- clean Release build: `RVCRealtime-app` / `RVCRealtime-au`成功
- APP / AUのad-hoc `codesign --verify --deep --strict`成功
- clean buildのcomponentを一時installした`auval -v aufx Rvcr Rvcp`: `AU VALIDATION SUCCEEDED`
- GarageBand実機: model切替、pitch `-12 st`、Bonjour自己route、runtime fault injection後の復旧、offline bounceを確認
- 単trackと伴奏付きmixのoffline bounceを利用者が聴取し、変換音とdropoutなしを確認

範囲と制限:

- Intel CPUのrealtime再生は推論deadlineを超え、dropoutが発生します。標準offline bounceは合格しています。
- Windows実機、Apple Silicon、Logic Pro、別Mac間Bonjour、複数clientの排他制御は未確認です。利用できる実機資源がないため、このPRでは保証しません。
- clean checkout検証ではmodelの再学習を行いません。学習・実変換の既存実機記録と、model不要の再現可能なdry-runを分離しています。
- Bonjour到達範囲とnetwork分離はOS、router、VLAN、mDNS reflectorの設定へ委ねます。このlocal-production設計はSaaSのtenant分離を目的にしません。
- iPlug2はMIDI I/OなしAPP初期化修正を含むfork revisionを参照します。この1-file修正はiPlug2へ別PRとして提出予定です。

### 繁體中文

此PR以既有的Windows WebUI與VST實作為基礎，新增可在Intel macOS上完成訓練、推論及GarageBand AUv2處理的路徑。

主要變更：

- 將`RVCRealtimeVST`重新命名為`RVCRealtime`；Windows維持VST2/VST3，macOS建置APP與AUv2。
- AU採用輕量C++ audio head，不在DAW audio thread內執行Python、PyTorch、模型載入或Bonjour探索。
- WebUI負責RSVC runner、健康檢查、有限次重啟、模型登錄與control gateway；音訊port與控制面分離。
- WebUI與AU皆可選擇模型。AU明確指定的opaque model ID只在該AU session內優先於WebUI預設值。
- Bonjour直接使用macOS的`dns-sd` / mDNSResponder來公告、探索與解析`.local`服務；應用程式不另加CIDR或網段限制。
- AU將host的offline render狀態傳至RSVC；GarageBand標準offline bounce會等待推論完成。
- 新增macOS x86_64的Python 3.12依賴，以及CPU訓練與推論的platform差異處理。
- 新增不需模型的runtime、protocol、lifecycle、Bonjour與模型選擇自動測試。

驗證（Intel Mac、macOS 15.7.7 / Xcode 16.2）：

- 將已推送分支以`git clone --recursive`取得至空白暫存目錄，確認所有submodule固定revision皆可取得
- `python -m unittest discover -s tests`：43/43通過，不需模型或重新訓練
- clean Release build：`RVCRealtime-app`與`RVCRealtime-au`成功
- APP / AU的ad-hoc `codesign --verify --deep --strict`成功
- 暫時安裝clean build component後執行`auval -v aufx Rvcr Rvcp`：`AU VALIDATION SUCCEEDED`
- GarageBand實機確認：模型切換、pitch `-12 st`、Bonjour自我route、runtime fault injection後復原及offline bounce
- 使用者實際聆聽單軌與含伴奏mix的offline bounce，確認轉換有效且無dropout

範圍與限制：

- Intel CPU即時播放會超過推論deadline並產生dropout；標準offline bounce已通過。
- Windows實機、Apple Silicon、Logic Pro、兩台Mac間Bonjour及多client互斥尚未驗證。因缺少對應實機資源，本PR不提供這些保證。
- clean checkout驗證不重做模型訓練；既有的訓練與實際轉換記錄，和不需模型、可重現的dry-run分開管理。
- Bonjour可達範圍與網路隔離交由OS、router、VLAN及mDNS reflector設定。本local-production設計不以SaaS tenant隔離為目標。
- iPlug2目前指向含「無MIDI I/O APP初始化修正」的fork revision；此單一檔案修正預計另行向iPlug2提出PR。

