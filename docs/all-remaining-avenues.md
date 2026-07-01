# Sunmi V2 T5930 で Android 上げ / 永続 root を狙う全経路まとめ

作成: 2026-07-01。今日の実機実験と徹底調査で判明した「試した経路」「潰れた経路」「まだ残っている経路」を網羅的に整理する。

---

## 今日の実測で **潰れた** 経路(再挑戦価値ゼロ)

| # | 経路 | 潰れた理由 | 実測結果 |
|---|---|---|---|
| A1 | Magisk-patched boot を dd で焼く | Sunmi LK の AVB1 が boot header signature を verify | Magisk 焼き → bootloop、Recovery the system で復旧 |
| A2 | mtkclient で BROM 進入 | 物理コンボ(音量+/-/両押し/電源同時)全滅、Sunmi は BROM ボタン進入を塞いでいる | 音量+ → Recovery / 音量- → 0e8d:20ff META / 両押し → META / 電源+音量 → META |
| A3 | mtkclient で Preloader mode 通信 | Sunmi 独自プロトコル "Cyrus Technology CS 24" (0e8d:2008) と mtkclient は handshake できない | `printgpt` `crash` `plstage` `meta` 全部タイムアウト、auth ファイル渡しても同じ |
| A4 | Sunmi Recovery から adb sideload | Sunmi Recovery は adb 無効化 | Recovery 画面で `adb devices` 空、USB モードも切り替わらない |
| A5 | SP Flash Tool v5 + auth で downgrade → 純正 boot に上書きされる | **Sunmi の recovery-from-boot.p mechanism** が起動時に純正 boot を書き戻す(実測) | D-3 で焼き成功 → 電源長押しで起動 → Sunmi ROM 2.12.1 のまま復活 |

---

## 今日の実測で **一部通った** が最終的に無効化された経路

| # | 経路 | 通った所 | 潰された所 |
|---|---|---|---|
| B1 | logo パーティション書き換え | LK は logo を verify しない、書き換え可能 | 起動アニメーションが変わるだけで実質メリット無し |
| B2 | SP Flash Tool v5 + auth_sv5.auth + fishybytes 2021 firmware で BROM 経由書き込み | 認証成功、DA 転送成功、logo 書き込み成功、GPT 書き換え成功 | Sunmi 自動復旧で全部巻き戻される |
| B3 | mtk-su (CVE-2020-0069) で一時 root | uid=0 取得、SELinux permissive 化 | reboot でリセット、boot 書き換えは Sunmi Recovery に無効化される |

---

## まだ試していない **残存経路**

### 経路 1: `recovery-from-boot.p` 無効化 + Magisk 焼き

**核心**: Sunmi 自動復旧の実体は AOSP 標準の `recovery-from-boot.p` mechanism。以下の要素で構成:

- `/system/recovery-from-boot.p` — 純正 boot の patch データ(または image data)
- `/system/etc/install-recovery.sh` — 起動時に自動実行されて recovery-from-boot.p を boot パーティションに適用するスクリプト
- `/system/bin/applypatch` — patch 適用実行バイナリ

**これらを無効化(空にする or 書き換える)すれば自動復旧が止まる**。その後で Magisk 焼きを試すと reboot 後も維持される可能性。

**難点**:

- system は dm-verity で守られている(fstab に `verify` flag、system は dm-0 経由でマウント)
- system を書き換えると verity fail → 次回起動で bootloop
- **dm-verity runtime バイパスで書き換え** or **verity metadata を再計算して更新** が必要

**成功可能性**: 15-25%
**時間コスト**: 1-2 時間
**復旧経路**: Sunmi Recovery the system(前回実証、確度高い)

### 経路 2: dm-verity runtime バイパスで /system を live rw 化 + system 書き換え

**手順**:

1. mtk-su で root 取得
2. dmsetup で `/system` の dm-verity を dm-linear に reload
   ```
   dmsetup suspend vroot   # or 実際のマッパー名
   dmsetup reload vroot --table "0 <size> linear /dev/block/mmcblk0p32 0"
   dmsetup resume vroot
   ```
3. `mount -o rw,remount /system`
4. system 内のファイルを書き換え(recovery-from-boot.p 削除、install-recovery.sh 潰す、build.prop 改変)
5. reboot

**難点**: 経路 1 と同じ、書き換えた内容が次回起動時の dm-verity で verify されて fail する可能性

**しかし**: 書き換え内容によっては dm-verity のハッシュ値と一致するように調整すれば通る(ハッシュ算法の再計算が必要)

**成功可能性**: 20-30%(recovery-from-boot.p 系ファイルは Sunmi 特化のバイナリなので、変更に対する dm-verity 検出は避けがたい)

### 経路 3: `adb disable-verity` を root 経由で有効化

**手順**:

1. mtk-su で root
2. `/system/build.prop` に `ro.build.type=userdebug`, `ro.debuggable=1` を書き込む
3. Android の verify-metadata partition か boot cmdline を書き換え(通常 adb disable-verity で操作される)
4. reboot
5. `adb disable-verity && adb reboot && adb remount` で verity 恒久 disable

**難点**: 

- `adb disable-verity` は Verified Boot が enforcing だと動作を拒否する可能性
- Sunmi LK が verifiedbootstate=green を維持する仕組みだと userdebug 化に反発
- **user build を userdebug 相当にするのは system 書き換えを伴う** → 経路 1/2 と同じ壁

**成功可能性**: 5-10%

### 経路 4: kernel exploit chain(dirty pipe / Mali GPU CVE)で LK 書き換え

**手順**:

1. mtk-su で kernel 権限取得
2. kernel exploit(CVE-2023-4211 Mali GPU / dirty pipe 系)で任意カーネルメモリ書き換え
3. **AVB verify を実行するコードパスの kernel-side を無効化**
4. または /dev/block 経由で **preloader/LK を直接書き換え**
5. カスタム LK に置き換え、それが AVB verify を skip する実装

**難点**:

- MT6739 の Mali-T720 (Midgard) は CVE-2023-4211 対象、パッチ無し = **理論上刺さる**
- ただし Sunmi kernel 4.4.22 に MTK 独自パッチが当たっているかは要調査
- LK バイナリの逆解析・パッチが必要(職人技)
- 失敗するとブリック確定

**成功可能性**: 5-10%
**時間コスト**: 数日〜数週間
**リスク**: 極めて高い、ブリック確定 or SoC 破壊の可能性

### 経路 5: 物理 test point 経由 BROM 進入(唯一のハード経路)

**手順**:

1. 基板を分解
2. MT6739 SoC 周辺の test point(小さいパッド)を GND に短絡させながら USB 挿入
3. BROM (0e8d:0003) 強制進入
4. SP Flash Tool で LK 含めて全パーティション書き換え
5. カスタム LK で AVB verify 無効化

**難点**:

- Sunmi V2 の test point 位置は非公開 → SoC 周辺を電気的に探索必要
- カスタム LK を自作 or 誰かの流用(現時点で存在しない)
- 失敗すると基板破損 or SoC ダメージで物理ブリック
- 防塵防水機能は破棄

**成功可能性**: 5-10%(電気工学 + RE スキル前提)

### 経路 6: Sunmi 法人契約経由でカスタム firmware 発注

- Sunmi Business に「Android 上げ版 firmware 欲しい」と契約ベースで依頼
- 法人案件レベルの契約単価(想定数十万円〜数百万円)
- Sunmi は「Auth code」で unlock できる仕組みを持っている
- **hobby プロジェクトでは実質不可能**

### 経路 7: Sunmi OTA サーバ MITM(fake OTA 配信)

- Sunmi OTA サーバに fake 応答して custom firmware を install
- **問題**: OTA も署名検証あり = 実質不可能
- Sunmi の署名鍵がリークしていれば可能だが、そのような事例は聞かない

---

## 総合的な推奨(2026-07-01 時点)

### 最推奨: 経路 1 + 2 の組み合わせ

**「dm-verity runtime バイパス + recovery-from-boot.p 無効化」** を試す。

理由:
- ソフトのみで完結、物理破壊リスクなし
- 復旧経路(Sunmi Recovery the system)が実証済み
- 成功したら永続 root の道が開ける
- 失敗しても学びが得られる(recovery-from-boot.p の Sunmi 実装詳細が判明)

期待値:
- 成功: 15-25%(dm-verity re-hash が壁)
- 失敗: 復旧後に Flutter kiosk 化に戻る

### 次点: 経路 3(build.prop 改変 + disable-verity)

- 経路 1/2 が失敗したときの次のトライ
- 副次的な学び目的

### 撤退: 経路 4/5/6/7

- リスクとコストが hobby プロジェクトに見合わない

---

## 復旧経路(常に使えるもの)

**Sunmi Recovery の "Recovery the system"**:

1. 端末を電源長押し(現行 boot が起動失敗する状況では自動的に Recovery に落ちる or 電源長押しで復帰試行 → 純正復元)
2. 純正 Sunmi ROM 2.12.1 に factory reset される
3. userdata wipe、初期セットアップ再走 → USB debug ON → mtk-su 再 push
4. 手戻り時間: 20-30 分

これが担保されている限り、経路 1〜3 の実験は safe。
