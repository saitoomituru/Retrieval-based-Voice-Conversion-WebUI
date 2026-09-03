# WebUI所有runtimeと推論asset routing実測

## 目的

Issue #1 / #20 / #21 / #24 / #25 / #28 / #30 / #31 / #32に対し、論理serviceと死活管理をWebUIへ統合し、audio streamだけを専用portへ分離する。またVST専用namespaceへのAU/engine責務混入と、engineによるmodel asset directory暗黙所有を解消する。

## 対象

- branch: `issue-6-macos-runtime-bootstrap`
- runtime統合時HEAD: `935cb7a`
- asset routing修正: `7a7db6e`
- WebUI/control: `127.0.0.1:7865`
- RSVC audio stream: `127.0.0.1:17865`

## 実行環境

- macOS / Intel x86_64
- Python 3.12 virtual environment: `.venv`
- C++ head: `RVCRealtime/build-macos/Debug/rvc-worker-smoke`相当
- model: `assets/weights/deltamon_singing_40k_v2_20260901.pth`（gitignore対象、repositoryへ含めない）
- index: なし

## 観測事実

1. `RVCRealtimeVST/` を `RVCRealtime/` へ改名後、AUとC++ smoke targetのfresh buildに成功した。
2. WebUI supervisor単体で、RSVC handshakeを返すrunnerを`17865`に起動し、旧health port `17864`を開かず、WebUI終了時に所有childだけを停止できた。
3. 隔離WebUIを`127.0.0.1:7870`で起動し、`rvc_runtime_health`と`rvc_runtime_configure` APIがGradio configへ登録されることを確認した。
4. source-only worktreeから実model sessionを開くと、`assets/hubert_base`がworktreeに無いため失敗した。これはgitignore assetがworktreeへ複製されないことと、engineが`rvc_root/assets`を暗黙所有していたことの再現である。
5. 途中で、runtime CLIを`Config()`がWebUI CLIとして再解釈する問題と、PyTorch interop thread設定の非冪等性も再現した。
6. `assets_root`をrunner設定の絶対pathとして追加し、engineの`os.chdir()`を廃止した。HuBERTとRMVPEはrunner指定asset rootから解決する。
7. 単一root統合後、実modelでC++ thin headからRSVC sessionを開き、1 block変換に成功した。
8. WebUIを`127.0.0.1:7865`で起動し、WebUI所有のpassthrough runner PID 68921を確認した。`rvc_runtime_configure` APIからDeltamon modelを適用すると、所有childだけがPID 69192へ交代し、`engine=rvc` / `READY`となった。
9. model適用後もlistenは`127.0.0.1:17865`だけで、旧port `17864`は開かなかった。生成設定`TEMP/rvc-runtime-engine.json`にはroot、asset、modelの絶対pathが書かれ、repositoryへ入らないようgitignoreした。
10. WebUI所有runnerへC++ thin headを接続して再度1 block変換し、非zero出力とdrop 0を確認した。
11. build済みcomponentと配備先binaryのSHA-256が一致し、`codesign --verify --deep --strict`と`auval -v aufx Rvcr Rvcp`が成功した。
12. protocol、engine config、runtime session、supervisorの`unittest` 17件が成功した。sandbox内ではlocalhost bind禁止により1件だけ環境エラーとなったため、同一suiteを許可されたlocalhost環境で再実行して17/17成功を確認した。

## 実model結果

```text
READY frames=6240 latency=12480 infer_ms=1250.92 output_rms=0.0185711 drops=0 blocks=1 status="RSVC 127.0.0.1:17865 session 2"
READY frames=6240 latency=12480 infer_ms=1295.14 output_rms=0.0188121 drops=0 blocks=1 status="RSVC 127.0.0.1:17865 session 2"
```

- sample rate: 48000 Hz
- block: 130 ms / 6240 frames
- accepted latency: 12480 frames
- output RMS: 非zero
- drop: 0
- CPU推論時間約1251 msはblock長を超えるため、低latency合格を意味しない
- WebUI所有runner経由の再測定でも約1295 msであり、接続・変換成立とrealtime deadline合格は分離する

## AU配備・検証

- build: `RVCRealtime/build-macos/out/Debug/RVCRealtime.component`
- install: `~/Library/Audio/Plug-Ins/Components/RVCRealtime.component`
- 旧component退避: `RVCRealtime.component.backup-before-webui-runtime-20260904`
- binary SHA-256: `e570ae13856d79aa62d828f8ab83eb58a1de4b4a35fe5b274c8df243fbb4394d`（build/install一致）
- code signature: `codesign --verify --deep --strict` 成功
- Audio Unit validation: `auval -v aufx Rvcr Rvcp` / `AU VALIDATION SUCCEEDED`
- GarageBandは配備前から起動中のため、旧component imageを保持している。再起動後の人手検証が必要。

## 解釈

- model、HuBERT、RMVPEの配置はengine source treeの責務ではなく、runtime runnerのdeployment/config責務である。
- VST/AU headはmodel pathを所有せず、model idとsession audio shapeをRSVCへ渡す。
- localhost版の責務境界は成立した。Bonjour/LANは同じRSVC契約の発見・選択adapterとして追加できるが未実装である。
- Windows VSTは既存INI/UI/class layoutを保ち、AU固有差分はApple条件へ閉じた。

## 結果

`success`（localhost実model経路）

## 未試験 / unknown

- GarageBandでの実音声再生と聴感
- 長時間連続再生、drop、再接続
- WebUI UI操作からのmodel適用後にGarageBandがsession再作成する実機動作
- CPU推論がrealtime deadlineを満たすparameter組合せ
- Windows VSTのWindows実機build回帰
- model weight共有による複数sessionの再load回避
- Bonjour/LAN discovery、認証、remote latency、fallback

## Recovery / 次の一手

1. GarageBandを再起動し、挿入、ENGINE ON、再生、変換音、drop、復帰を人手確認する。
2. AU parameterをruntimeへ反映する`CONFIG_UPDATE`を実装する。現時点のmodel変換は固定parameterである。
3. 複数sessionでmodel weightを共有し、sessionごとの再loadを避ける。
4. CPU realtime deadline未達は音質・block・backend別に計測し、Issueへ分離する。
