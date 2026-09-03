# Sphere-DOS for RVC macOS / AU port

状態: `[PLI WORKSPACE CONTRACT]` `[LOCAL DEVELOPMENT]` `[NO STANDALONE RUNTIME CLAIM]`

この文書は `saitoomituru/Retrieval-based-Voice-Conversion-WebUI` fork に対する、SphereOS Atlantis Prompt Line Interface 版の作業机契約です。
目的は `RVCRealtime` の macOS / Audio Unit 移植を、複数 agent / session で同じ制約と証拠境界を保ったまま進めることです。

## 1. Boot prompt

新しい coding agent / session は、最初に次を解決します。

```text
Repository: saitoomituru/Retrieval-based-Voice-Conversion-WebUI
Primary goal: RVCRealtime の macOS + Audio Unit 対応
Roadmap: Issue #1 と関連 child issue
Local policy: AGENTS.md
Upstream: RVC-Project/Retrieval-based-Voice-Conversion-WebUI
Interface: Prompt Line Interface
Execution Envelope: 現在利用できる shell / GitHub / CI / DAW 実機
```

その後、対象 Issue と対象コードを読み、作業範囲を一文で宣言してから変更します。

## 2. PLI command vocabulary

自然言語による次の操作を、この fork の PLI 語彙として扱います。

- `監査 <issue>`: Windows 固有依存、platform 境界、license、未知を列挙する
- `登る <issue>`: acceptance criteria まで最小差分で実装する
- `追試 <対象>`: 同一条件または明示差分で再検証し、receipt を残す
- `実機確認 <host>`: Logic Pro / GarageBand / Audio Unit validator 等で確認する
- `実験記録 <slug>`: `experiments/` に観測と結果を保存する
- `上流化`: fork 固有差分を除き upstream PR 単位へ整理する
- `停止`: Semantic Stop を発火し、詳細 Issue を積んで変更を止める

これは shell command の模倣ではありません。実際の build / test / Git 操作は利用可能な Execution Envelope で実行し、receipt を分離します。

## 3. 作業状態

各 task は次のいずれかを明示します。

- `OPEN`: 着手可能
- `WORKING`: 現在作業中
- `RESOURCE-WAIT`: 実機、SDK、権限、計算資源など待ち
- `REVIEW-WANTED`: 機械検証済みで人間 / upstream review 待ち
- `BLOCKED`: 致命的または前提未解決
- `DONE`: acceptance criteria と必要検証を満たした

`build succeeded` と `DAW で使える` を同じ状態にしません。

## 4. 証拠レベル

優先する receipt:

1. commit SHA / diff
2. test / validator / CI の実行結果
3. Logic / GarageBand / AU validator の実機観測
4. worker log / crash log / latency measurement
5. agent の解釈・仮説

解釈から上位の実測 receipt を捏造・逆算しません。

## 5. 縮退運転

利用できないものがあっても、使える層だけで続行できます。

例:

```text
macOS build unavailable -> source audit / CMake portability review は続行可能
GarageBand unavailable -> auval / compile 検証まで進め、DAW 実機は RESOURCE-WAIT
RVC model unavailable -> plugin load / IPC smoke test まで進め、E2E VC は未試験
GPU acceleration unavailable -> CPU 経路で contract 検証し、速度 claim はしない
```

不足を成功へ丸めず、receipt に欠損を残します。

## 6. 自動ログ

反復実験は `scripts/record_experiment.py` で `experiments/` へ記録できます。

```bash
python3 scripts/record_experiment.py \
  --slug au-build \
  --issue 4 \
  --result not-tested \
  --summary "macOS build 手順の初期確認"
```

コマンド実行を伴う場合:

```bash
python3 scripts/record_experiment.py \
  --slug cmake-configure \
  --issue 4 \
  --run 'cmake -S RVCRealtime -B RVCRealtime/build-macos' \
  --result auto
```

生成物は自動で正解へ昇格しません。観測と仮説を分け、人間確認が必要なものは明示します。

## 7. Semantic Stop

`AGENTS.md` の停止条件に該当した場合:

1. 破壊的変更を広げない
2. 現在の diff と receipt を保持する
3. 日本語 Issue を作る
4. `BLOCKED` と再開条件を記す
5. user / maintainer の判断を待つ

停止は失敗の隠蔽ではなく、未知を既知へ偽装しないための制御です。
