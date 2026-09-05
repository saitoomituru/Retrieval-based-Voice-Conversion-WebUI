# upstream記名・知財境界の非攻性防壁

状態: `FREEZE / 情報子工学ペイン`

追跡先: https://github.com/saitoomituru/Retrieval-based-Voice-Conversion-WebUI/issues/39

担当: 齋藤みつる / Codex

開発支援としてGrok / Geminiが参加した実装の来歴も、fork側で保持する。

## 目的

RVC上流の技術と開発者へのリスペクトは維持しつつ、最近の運用で貢献者のGit記名が残りにくいことを観測事実として記録する。

これは技術者個人の悪意や違法行為を主張するIssueではない。中央管理化の背景にある技術外の制約を現時点では解かず、知財・出典・派生の境界だけを非攻性に保全するための凍結票である。

## 観測事実

- 上流repositoryは過去にPull Requestを利用し、Issue #271からPR #273へ進んだ実績がある。
- 現在はGitHub API上で`has_pull_requests: false`となり、Pull Request作成はGraphQLで権限拒否、RESTで`404`、過去PR #273参照で`410 Pull requests are disabled for this repo`となる。
- 現在の`main`は2026-07-19の空root commit `20334077dad85058a258e61e2a4c0abf6817dc9a`から始まる。
- コードは`5d47da14888d2a33a8b6ce6a480b4b84c9481143`で再importされ、現在の`main`の23 commitsはすべて`RVC-Boss`名義である。
- GitHub contributors APIも現在は`RVC-Boss` 1名だけを返す。READMEは全contributorsへの謝意を表示するが、参照元のGit履歴で旧貢献者の記名を追えない。
- Issue #2227で利用者が提示した`init_method="env://?use_libuv=False"`と同形の修正が、`2578b1a7320029aea3c2f6467c55597e1d90b4ed`に`RVC-Boss`単独authorで入っている。commit messageにIssue番号、提案者、`Co-authored-by`はない。
- 一方で、Issue本文・コメントと現行LICENSEの著作権表示は残っており、出典証拠の全消去は観測していない。

## 解釈

最近の上流運用は、外部貢献者のGit記名を保持するお行儀について、明確に弱い。これは運用上の実害リスクであり、攻撃意図の推定ではない。

上流開発者がPull Requestやmergeを知らない初心者でないことは過去履歴から明らかである。それでも現在の中央管理型運用を選んでいる背景に、以下の複合的ペインがある可能性は排除しない。

- 資金調達やVCとの条件
- sponsorとの契約や配布管理
- 法務・ライセンス・知財リスク
- 国際情勢、輸出管理、地域ごとのservice制約
- 開発・保守資源の集中と不足

これらはすべて`UNKNOWN`であり、現時点で証拠はない。事実として扱わない。

## ペインモデルの解像度

本Issueでは、上流エンジニアの技術・動機・行為と、上流を取り巻く資本・sponsor・法務・流通の都合を別層として扱う。技術を高く評価することと、記名を残しにくい資本運用を警戒することは両立する。

貢献者の略歴、commit、派生関係、権利者が硬く追跡できるほど、資本側の支援・買収・配布では以下の責務が増える。

- due diligenceの調査範囲
- 著作権・ライセンス・特許・商標の確認
- 貢献者ごとの合意、分配、記名、責任追跡
- 国や地域を跨ぐ契約、輸出、制裁、税務のリスクプール

この責務増大を避けるため、一般に資本側が次のような方向へ倒れることがある。

- 「完全国産」の物語に再包装し、外部来歴を薄くする
- NDA下での買取りや内部化により、公開貢献と製品来歴を分断する
- 中央repositoryへの単独名義の再importで、来歴追跡コストを社外の貢献者側へ戻す
- 機能は利用するが、記名・関係者管理・リスク分配を引き受けない資本由来のフリーライド

このモデルでは、作者個人に悪意がなくても記名剥落は発生し得る。むしろ、技術者の維持資源不足や資本側の契約条件により、個人の意思と無関係に中央化が進む場合もある。

ただし、RVC上流にこのような資本・VC・sponsor・法務事情が実在するかは`UNKNOWN`である。このモデルは境界設計のための仮説であり、上流への告発や事実認定ではない。

## 非攻性防壁

- 上流コードは直接取り込まず、自分のforkを1枚通し、取得revisionとlicenseを固定する。
- 上流への提案は無記名patchの貼付けではなく、公開済みforkの不変commit hashへリンクする。
- Issue先頭に実装者、実機検証者、開発支援、実装正本を明記する。
- 採用時はcommit authorまたはChangelog上の出典保持を依頼する。
- 本体、iPlug2、model、学習音声、indexのlicenseと配布境界を分離する。
- upstreamへの敬意と、fork側の来歴保全は両立させる。記名保全を攻撃や交渉カードにしない。

## FREEZE条件

技術実装で解消する問題ではないため、以下の外部事象が発生するまで凍結する。

- 上流がPull Requestを再度有効化する
- 上流が提案Issueへ反応し、取込方法や記名方法を示す
- fork由来コードの採用・非採用・記名保持・記名脱落の実害が観測される
- sponsor、法務、配布体制などの公開情報が追加され、運用変化の背景を検証できる

## 参照

- 上流Issue #271: https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/issues/271
- 上流Issue #2227: https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/issues/2227
- 上流Issue #2623: https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/issues/2623
- libuv修正commit: https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/commit/2578b1a7320029aea3c2f6467c55597e1d90b4ed
- macOS/AUv2提出分岐: https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/compare/main...saitoomituru:Retrieval-based-Voice-Conversion-WebUI:upstream/macos-au-webui-runtime
