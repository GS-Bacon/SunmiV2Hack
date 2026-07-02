# G-8 焼き試験プラン (Track F+G v2)

## Context

G-8 で生成した v2 patch(preloader Track F 8 バイト、LK Track G 10 バイト)を実 eMMC に焼いて、preloader + LK の全 auth gate 短絡状態を実測する。目的は「任意 boot.img を LK が受け入れるか」の実証。実証できれば Android バージョンアップ(GSI / LineageOS port)への capability 下段が確定する。

**前提**:
- 直前の G-9 で端末は完全復旧、全 partition が stock 状態
- 復旧経路(電源長押し → BROM → mtkclient で純正書き戻し)は確立済み、brick リスクは許容範囲
- 復旧手順は WORKLOG G-9 に明文化済み

**「慎重に」の運用ルール**:
- 各 phase 前に SHA256 verify + WORKLOG 該当箇所の再読を必ず挟む
- 焼き 1 回 = 1 checkpoint、必ず結果観察してから次に進む
- 予期しない挙動が出たら即座に復旧手順に戻る、深追いしない
- brick リスクの高い操作(preloader 焼き)は Order A で「本当に必要」と確定してから

## 目標

- **主目標**: v2 patched LK が preloader を通り、stock boot.img から Android 起動できること実測
- **副目標(達成すればより強い)**: v2 patched preloader も焼いて、preloader も含めた全 auth gate 突破を実測
- **非目標(今回はやらない)**: GSI / LineageOS 焼き、Magisk-patched boot.img の起動試験、dm-verity 無効化

## Phase 0: Preflight(brick リスクなし)

### 0-1. 焼き対象の再検証

- [ ] `sha256sum scratch/lk-patch/lk-full-shortcut.img` = `c33e893a10f6c38128aacef2912954b1ce737c67993d07af04b262823a28ec50` 確認
- [ ] `sha256sum scratch/preloader-patch/preloader-image-auth-disable.img` = `f826ed14a5fa52e96b54cfb083e4af45682a54b89c058779d82d32ec8cbac6c0` 確認
- [ ] `sha256sum dump/lk.img` = `fa9e3290...`(stock、rollback 用)確認
- [ ] `sha256sum dump/preloader-boot0.img` = `10d33a52...`(stock、rollback 用)確認

### 0-2. 復旧手順の再確認

- [ ] WORKLOG G-9 セクションを再読(電源長押し → BROM 復活 → `w lk` / `w preloader` の手順)
- [ ] `patches/apply-lk-full-shortcut.sh` の `rollback` 経路を読んで動作確認(dry-run mode があれば使う)
- [ ] `patches/apply-preloader-image-auth-disable.sh` の `rollback` 経路も同様

### 0-3. Track F v2 の適用対象 partition 確認

疑問点:preloader は `w preloader` で `preloader-boot0` に書くのか、それとも `preloader-boot0` と `preloader-boot1` の両方に書く必要があるか?

- [ ] mtkclient の partition table (`printgpt` 相当)を確認
- [ ] apply-preloader-image-auth-disable.sh のコマンドを確認
- [ ] preloader-boot1 も同時に焼くべきか判定(MediaTek は通常 boot0/boot1 の 2 面持ちで redundancy)

### 0-4. Order 判定

- **Order A(慎重、今回の default)**:LK v2 のみ焼き → boot 動作確認 → 通れば OK、bootloop なら preloader も焼き
- **Order B(即断)**:preloader v2 + LK v2 同時焼き

WORKLOG G-8 Track G の教訓:
> preloader の cert chain verify が Magisk-patched boot でも通る可能性(D-3 で bootloop したもの)

つまり **LK v2 だけで既に十分 wide な auth skip** ができている可能性がある。まず LK v2 のみで試すのが正解。

## Phase 1: LK v2 焼き(Order A の 1 発目)

### 1-1. Pre-flash

- [ ] Android が通常起動している状態から始める(端末が boot 済み)
- [ ] `adb reboot bootloader` は不可(fastboot ない)。代わりに:
  1. 電源長押しで shutdown
  2. USB 挿す → cycle 開始待ち
  3. mtkclient で BROM 掴む

または「起動中に mtkclient で reset」でも可能。挙動を観察して選ぶ。

### 1-2. Flash LK v2

**apply script は使わない**:venv broken + `--stock` 不要 + G-9 で raw invocation の動作実証済。以下のコマンドを直接使う:

```bash
sudo PYTHONPATH=/home/bacon/.local/lib/python3.14/site-packages python3 tools/mtkclient/mtk.py w lk scratch/lk-patch/lk-full-shortcut.img
sudo PYTHONPATH=/home/bacon/.local/lib/python3.14/site-packages python3 tools/mtkclient/mtk.py reset
```

**確認事項**:
- 書き込み完了メッセージ表示(`Wrote scratch/lk-patch/lk-full-shortcut.img to sector 730112 with sector count 2048.`)
- exit code 0

### 1-3. Boot 試験

```bash
sudo mtk reset
```

- USB 抜く
- 電源ボタン短押しで起動
- 観察:
  - **A. Sunmi ロゴ → Android 起動** = ★ Track G v2 が preloader を通った、大成功
  - **B. bootloop(BROM/preloader cycle)** = preloader が LK v2 を reject → Phase 2 へ
  - **C. META mode (0e8d:20ff)** = seccfg/LK 整合性で保護 mode に落ちた → 要調査
  - **D. 完全 dead(USB 認識なし)** = brick → 電源長押しで BROM 復活 → 復旧

### 1-4. Phase 1 判定

| 結果 | 意味 | 次アクション |
|---|---|---|
| A(通常起動) | LK v2 patch は preloader を通った | Phase 3(Magisk-patched boot 試験)へ or 完了 |
| B(bootloop) | preloader が LK v2 の signature を verify で reject | Phase 2(preloader v2 も焼く)へ |
| C(META mode) | seccfg unlock 相当と判定されている | 要 Ghidra 追加解析、Phase 2 は保留 |
| D(brick) | 復旧 → 原因調査 → プラン再検討 | 撤退候補 |

## Phase 2: Preloader v2 追加焼き(必要なら)

**トリガー条件**:Phase 1 で B(bootloop)発生。

### 2-1. 復旧経路の再確認

Preloader を焼き替えるのは **今回のプロジェクトで最もリスクの高い操作**。preloader が壊れると:
- Sunmi 独自の recovery が使えない(Sunmi Recovery は preloader が必要)
- BROM は生きるので mtkclient 復旧は可能だが、電源長押しからやり直しになる
- **preloader-boot0 に加えて preloader-boot1 も同時に焼くこと**を確認(片方だけ壊すと MediaTek の fallback で救われる可能性を潰す前に念のため両方)

### 2-2. Preflight

- [ ] BROM 復旧経路が今この瞬間動くか事前確認(mtkclient で `printgpt` 成功する状態から始める)
- [ ] `dump/preloader-boot0.img` の SHA256 再確認
- [ ] Phase 1 の bootloop 状態から復旧 → BROM で `printgpt` 通ることを確認 → その状態で preloader v2 焼き

### 2-3. Flash preloader v2 (boot1 + boot2 の redundancy 両面)

Phase 0-3 で確定した通り、preloader は eMMC BOOT1 / BOOT2 の 2 面持ちで redundancy。両方に同じ image を書く。

```bash
export PY_ENV="PYTHONPATH=/home/bacon/.local/lib/python3.14/site-packages"
export PRELOADER_V2="scratch/preloader-patch/preloader-image-auth-disable.img"

# BOOT1
sudo $PY_ENV python3 tools/mtkclient/mtk.py w preloader $PRELOADER_V2 --parttype=boot1
# BOOT2(同 session、DA 再利用)
sudo $PY_ENV python3 tools/mtkclient/mtk.py w preloader $PRELOADER_V2 --parttype=boot2
```

LK v2 は Phase 1 で既に焼かれているので、Phase 2 では追加で焼く必要はない(Phase 1 の bootloop = LK v2 は既に eMMC 上に存在)。

### 2-4. Boot 試験

Phase 1-3 と同じ観察軸。

### 2-5. Phase 2 判定

| 結果 | 意味 |
|---|---|
| A(通常起動) | preloader + LK v2 chain 成功、段 1-5 全 auth skip 実測 |
| B(bootloop) | Sunmi 独自 preloader 検証が別位置に存在、Ghidra 再解析必要 |
| C/D | 復旧 → 撤退判定 |

## Phase 3: Track F+G が本当に効いているかの sanity check(オプション)

Track F+G が「auth gate skip」なので、その効果を確認するには **既存の壊れた boot.img が起動できるか** を試す。

- 素材:`dump/new-boot-magisk.img`(Phase 3-3 で作った Magisk-patched、G-7 で bootloop の元凶)
- これが起動できれば Track G v2 の効果が LK boot verify にも及んでいる証拠

**ただし**:これは今回の主目標ではない。Phase 1/2 で通常起動が確認できたら、そこで区切って WORKLOG に記録して commit するのを優先する。Phase 3 は次のセッションでもよい。

## Rollback 手順(brick 時 / 撤退時)

どの Phase でも共通:

1. USB 抜く
2. 電源ボタン 30 秒長押し
3. USB 挿す
4. mtkclient で BROM handshake:
   ```bash
   sudo mtk w lk dump/lk.img
   sudo mtk w preloader dump/preloader-boot0.img   # preloader も焼いた場合のみ
   sudo mtk reset
   ```
5. 電源ボタンで通常起動確認

WORKLOG G-9 と同じ手順、追加の学習曲線なし。

## 成功判定

- **最小成功**: Phase 1 で LK v2 焼いて通常 Android 起動確認できた
- **中間成功**: Phase 2 も通って preloader v2 も焼いた状態で通常起動確認
- **完全成功**: Phase 3 まで通って Magisk-patched boot も起動できた(GSI の道が確定)

いずれの段階でも「そこで区切って WORKLOG + commit + push」が正解。全部通そうと欲張らない。

## 記録

- 各 Phase の結果を `logs/experiment-g8-phase{N}.log` に保存
- Phase 完了ごとに WORKLOG.md に短いエントリ追記
- 成功したら commit + push(WORKLOG 更新)
- brick 復旧が発生したら復旧完了時点で commit(状態を明確にしてから次へ)

## 主なリスク一覧

| リスク | 発生率 | 影響 | 対策 |
|---|---|---|---|
| LK v2 で bootloop | 中 | Phase 2 に進むだけ、復旧可 | 復旧手順は G-9 で実証済 |
| Preloader v2 で完全 dead | 低〜中 | BROM から再度復旧、時間 30 分 | Phase 2 に入る前に復旧手順を再確認 |
| BROM が再び DEAD 化 | 中 | 電源長押しから再試行、経路確立済 | 短時間の作業で 1 session 内に完結させる |
| eMMC 破損 / partition table 破壊 | 低 | 復旧困難、SP Flash Tool 経路が必要 | `w` は `--parttype=user` 前提、GPT を触らない |
| Boot 成功したが不安定 | 低 | reproducibility 検証 | 数回 reboot して安定確認 |
