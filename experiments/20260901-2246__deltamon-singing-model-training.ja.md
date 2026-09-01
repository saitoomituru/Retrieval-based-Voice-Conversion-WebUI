# 実験・開発ログ: deltamon-singing-model-training

実施日: 2026-09-01
観測時点: 2026-09-01T22:46:30+09:00
対象 Issue: #8、#10、#11、#12、#13（関連: #6）
branch / commit: `issue-6-macos-runtime-bootstrap` / `a41ab03`
実行環境: macOS 15.7.7 / Intel x86_64 / Python 3.12.7 / CPU
結果: running（前処理・特徴量準備はsuccess、model学習は継続中）

## 目的

AirDriveに保管した利用権限確認中のデルタもん公式学習音声から、RVC 40k/F0あり/v2の
歌唱modelをローカル実験として学習する。公開・再配布・upstream提出は別の権利確認ゲートに置く。

## 入力と保全境界

- source: AirDrive `デルタもん公式学習音声` 65 files / 805.442 sec
- repository内 `logs/deltamon_source` はsourceへのsymlinkのみ。音声本体はcommitしない。
- base: `assets/pretrained_v2/f0G40k.pth` / `f0D40k.pth`。重みはignore対象。
- RVC mute素材: `.model-downloads/mute.zip` と `logs/mute`。ignore対象。
- model/index/変換音声/学習中間物: `logs/` またはAirDriveに保持し、repositoryへ入れない。

## 実行コマンド

```text
python -m train.preprocess logs/deltamon_source 40000 4 logs/deltamon_singing_40k_v2_20260901 False 3.0
python -m train.dataset.extract_f0 cpu logs/deltamon_singing_40k_v2_20260901 4 rmvpe
python -m train.dataset.extract_hubert_feature cpu 1 0 logs/deltamon_singing_40k_v2_20260901 v2 False
python -m train.train -e deltamon_singing_40k_v2_20260901 -sr 40k -f0 1 -bs 1 -te 20 -se 5 -pg assets/pretrained_v2/f0G40k.pth -pd assets/pretrained_v2/f0D40k.pth -l 1 -c 0 -sw 1 -v v2
```

## 観測事実

- 前処理は65 filesを処理し、無音1 fileを`未検出の非静音片段`としてskip。233 clipsを生成。
- F0はRMVPE/CPUで233/233 success、failed 0。
- HuBERTはCPUで233/233 success、各featureは768次元。
- filelistは完全な233 clips + mute 2行の235行。
- G/D底モデルは`<All keys matched successfully>`。
- CPU single-process、DataLoader worker 0で学習epoch 1へ到達。
- epoch 1は22:46:30に完了、所要24分58秒。
- epoch 1の200 step時点: `loss_disc=4.673`, `loss_gen=2.436`, `loss_mel=24.602`, `loss_kl=3.730`。
- 観測時点でPID 55307が生存し、CPU約565%で次epochを計算中。5 epoch保存指定のためcheckpointは未生成。

## 失敗とRecovery

- Terminal本体のAppKit `EXC_BAD_ACCESS/SIGSEGV` → 長時間処理を管理processで段階実行。
- script直接起動の`ModuleNotFoundError`/循環import → `python -m train...`へ変更。
- 無音clipの`UnboundLocalError(tmp_audio)` → 明示skip、sliceごとのtail出力、失敗伝播を実装。
- CPUで不要なDDP TCPStore bind → CPU single-process化。
- CPU DataLoaderの`torch_shm_manager` → worker 0化。
- filelist未作成 → 完全な成果物のintersectionから235行を再生成。

## 解釈

学習パイプラインは準備段階を通過し、初期epochのlossが取得できた。これは学習processが
動作している証拠であり、歌唱品質・過学習・実用変換の成功を意味しない。CPUでは1 epochが
約25分のため、20 epoch完了には長時間を要する。

## Recovery / 次の一手

1. PID 55307を維持し、5/10/15/20 epochのcheckpointとtrain.logを確認する。
2. 学習完了後にindexを別processで生成する。
3. `村祭りボイス_RVC入力.wav`を使った変換は、model/index存在確認後に別実験として記録する。
4. 音声・model・indexはAirDriveへ保管し、公開前に本家のupstream/再配布条件を確認する。

## unknown

- 20 epochの完了時刻と最終loss
- checkpointの実体・index生成結果
- 村祭りボイスの変換品質、F0追従、遅延、dropout
- Terminal crashの再現率
- デルタもんmodelの公開・再配布・upstream許諾
