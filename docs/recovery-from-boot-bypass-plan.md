# Sunmi V2 T5930: LK バイナリパッチで AVB verify 無効化(経路 C1)

> このプランは以前の `recovery-from-boot` 無効化プランを撤回して置き換えたもの。E-0 の実測で「install-recovery.sh は AOSP 標準の recovery 復元スクリプトで boot を守っていない、Sunmi の自動 boot 復旧は LK レベル実装」と判明したため、根本の LK を直接パッチする方針に切り替える。

---

## Context(なぜやる)

Sunmi V2 T5930 の 3 段防御(AVB1 / dm-verity / 自動 boot 復旧)は全て **LK (bootloader) 内部の実装**に由来する。**LK 自体をパッチすれば全防御を一気に無効化できる**。

**前提(今日実測)**:

- SP Flash Tool v5 + auth_sv5.auth + BROM 進入 = **確立済み**(D-2 実測)
- BROM 経由で LK パーティション (mmcblk0p23) を焼ける
- **preloader は絶対に触らない**(BROM verify 破壊 = 復旧不能ブリック)
- dump/lk.img と `tools/firmware/Firmware/lk.bin` の 2 バージョンあり(2.12.1 現行と 2021 版)

**成功時の得るもの**:
- Magisk 永続 root(dafish7 手順が動く)
- system 書き換え自由(dm-verity 無効化含む)
- GSI 焼きの現実性(dm-verity 突破)
- 端末の完全な自由度

**投資対効果**: 成功率 20-30%、時間 半日〜1 日、副次価値 大(LK バイナリ解析スキル、Sunmi 実装の深い理解)。

**復旧経路**: 純正 LK を SP Flash Tool で焼き戻し(BROM 進入は電源 OFF + USB のみ、実証済み)。

**関連ドキュメント**: `docs/all-remaining-avenues.md`, `docs/downgrade-magisk-plan.md`, `WORKLOG.md`。

---

## 前提整理

### 手元にあるもの

- **dump/lk.img** (1MB) — 現行 2.12.1 の Sunmi 純正 LK
- **tools/firmware/Firmware/lk.bin** (1MB) — 2021 版 fishybytes LK
- **dump/preloader-boot0.img** (4MB) — 純正 preloader(参考、触らない)
- SP Flash Tool v5.1916 + auth_sv5.auth + MT6739-DA.bin(焼き経路)
- `tools/firmware/Firmware/MT6739_Android_scatter_emmc.txt`(パーティション定義)

### Sunmi V2 の secure boot chain 推定

```
BROM (SoC 内蔵、書き換え不可)
  ↓ verify (eFuse の公開鍵)
Preloader (mmcblk0boot0)  ← 触らない、鉄則
  ↓ verify (preloader 内キー)
LK (bootloader, mmcblk0p23)  ← パッチする対象
  ↓ verify (LK 内キー = AVB1)
boot.img
```

**このプランの成否は「preloader が LK を verify するか否か」で決まる**:
- verify しない → パッチした LK が起動、AVB1 突破、全防御崩壊
- verify する → パッチした LK が preloader に蹴られる、詰み(C4 に進むしかない、hobby 範囲外)

**Sunmi V2 の secure boot 完全度は不明**、実測で判定するしかない。

---

## リスクと復旧経路

### 復旧経路(確認済み)

失敗時のパス:
1. 端末を電源長押し 20 秒で強制シャットダウン
2. USB 抜いた状態で数秒
3. USB を PC に挿す(ボタン不要、電源 OFF から自動 BROM 進入 = 今日実測済)
4. SP Flash Tool CLI で LK パーティションに **純正 LK (dump/lk.img)** を焼く
5. reboot → 純正状態復活

**復旧時間: 5-10 分**(SP Flash Tool + auth 経路が動く前提、実証済み)

### 復旧できないシナリオ(絶対に避ける)

- **preloader (mmcblk0boot0) 書き換え** → BROM verify 破壊 = 物理ブリック
- **eFuse 書き込み** → SoC の永久変更、不可逆
- **物理分解**

### Not-to-do リスト

- preloader は絶対に触らない
- lk 以外のパーティションを同時に書き換えない(1 変数実験原則)
- 焼く前に必ず「純正 lk.img が dump に存在すること」を再確認
- パッチした LK バイナリの SHA256 を必ず記録(万一の書き戻し用)

---

## Phase F-0: 逆解析環境準備

### 目的

LK バイナリを逆解析するツール一式の導入。

### 手順

1. **Ghidra インストール**:
   - Ubuntu 26.04 で `sudo apt install ghidra` があれば apt、なければ公式 zip
   - 代替: `radare2`(軽量 CLI)、`Cutter`(radare2 の GUI)
2. **Ghidra のロード確認**: 空プロジェクトで起動確認
3. **ARM 用の JDK 確認**: Ghidra は Java 21+ 要求
4. **既存 dump の再確認**: `dump/lk.img` の SHA256, サイズ 1MB
5. **fishybytes 2021 版 LK も参考として展開**: `tools/firmware/Firmware/lk.bin`

### 検証

- Ghidra 起動、Little Kernel の binary を disassemble できる
- ARM32 の decompile が動く

### 中止判断 F-0

- Ghidra が起動しない → Radare2 に切り替え

---

## Phase F-1: LK dump の構造把握

### 目的

Sunmi LK のロードアドレス、entry point、主要関数を把握。

### 手順

1. **`file dump/lk.img`** で ELF or raw binary か判定
2. **`strings dump/lk.img | head`** で読める文字列一覧(通常 "MTK bootloader", "Little Kernel", バージョン等)
3. **hexdump 先頭 256 バイト**: MediaTek LK は通常特定の magic (`0x58881688` = MT loader magic)
4. **Ghidra にロード**:
   - Language: ARM v7 Little-Endian
   - Base address: 0x41E00000(MediaTek LK 標準)、実際の値は strings + hexdump から推定
5. **Auto-analyze 実行**、entry point 特定
6. **重要文字列の検索**:
   - "AVB0"(AVB マジック)
   - "verify"
   - "signature"
   - "boot"
   - "hash mismatch"
   - "Verification failed"

### 検証

- entry point が確定
- 主要な文字列とそれを参照する関数が判明

### 中止判断 F-1

- MediaTek 特有の暗号化 or 圧縮がされていて逆解析不能 → 中止

---

## Phase F-2: AVB verify 関数の特定

### 目的

boot.img を verify する関数を Ghidra のコールグラフで辿って特定。

### 手順

1. F-1 で見つけた "AVB0" や "verify boot" 文字列を参照する関数を特定
2. その関数の呼び出し元を追う(通常 boot loader main → load_boot → verify_boot → return)
3. 関数のシグネチャ推定(引数、戻り値)
4. 戻り値の使われ方: `if (verify_boot() != 0) reboot();` パターンを探す
5. **パッチ対象を絞る**:
   - 関数の入り口を即 return
   - 呼び出し元の分岐条件を無効化
   - 戻り値を強制 0

### 検証

- verify 関数の物理オフセット確定
- パッチ設計の候補が 2-3 個絞れる

### 中止判断 F-2

- verify 関数を特定できない(難読化されている) → 中止

---

## Phase F-3: パッチ設計とバイナリ書き換え

### 目的

**dump/lk.img** をコピーして、特定した関数を無効化したパッチ版を作成。

### 手順

1. `cp dump/lk.img scratch/lk-patched.img`
2. パッチ選択肢:
   - **A: 関数入口で return 0**: 4 バイト書き換え(ARM: `MOV R0, #0` = `0xE3A00000`, `BX LR` = `0xE12FFF1E`)
   - **B: 呼び出し元の分岐を NOP に**: 分岐命令(BL / BLX)を NOP に(`0xE1A00000`)
   - **C: 分岐条件フラグ強制 clear**: 条件付き分岐 (`BEQ` 等)の条件反転
3. `xxd -s <offset> -l 16 dump/lk.img` で該当バイトを確認
4. `printf` + `dd` で書き換え(先ほどの logo 焼きテストと同じ手法)
5. SHA256 記録

### 検証

- パッチ後の lk-patched.img が 1MB 維持(サイズ不変)
- 該当バイトが期待通り書き換わっている
- Ghidra で再 disassemble してパッチが期待通り機能する形になっている

### 中止判断 F-3

- パッチが適用できない(バイナリオフセットずれ) → 中止

---

## Phase F-4: SP Flash Tool 経由での LK 焼き

### 目的

パッチ済み LK を SP Flash Tool で BROM 経由で書き込む。

### 手順

1. **`tools/firmware/Firmware/lk.bin` を退避**(2021 版、これは焼きたくない)
2. **`scratch/lk-patched.img` を `tools/firmware/Firmware/lk.bin` にコピー**(scatter file は lk.bin を参照する)
3. **専用 config.xml を作成** `tools/firmware/spft-config-f4-lk-only.xml`:
   - rom-list で **index 24 (lk) だけ enable=true**、他全て enable=false
   - Format タグ無し(GPT は現状維持)
   - da-download-all タグ
4. 端末を電源長押し → 完全シャットダウン
5. USB 抜く
6. こちらで flash_tool を bg 起動、120 秒 USB scan 待機
7. USB を挿す(電源キー不要、BROM 進入待ち)
8. LK 焼き完了確認

### 検証

- SP Flash Tool の出力に `[24] WRITE TO PARTITION [ lk ]` と `Download Succeeded`
- LK 以外のパーティションが焼かれていない
- Disconnect 表示

### 中止判断 F-4

- LK 焼きが auth エラー → 中止、復旧
- Preloader mode に落ちて BROM 進入不可 → 復旧手順

---

## Phase F-5: 起動確認(Sunmi Secure Boot の完全度判定)

### 目的

**このプラン全体の成否を決める瞬間**。パッチした LK が preloader を通り抜けて起動できるか。

### 手順

1. 端末の電源キーを短押しで起動
2. 60 秒観察
   - **通常起動 = LK パッチが preloader verify を通った = 大成功、F-6 に進む**
   - **bootloop = LK パッチが蹴られた or LK 自身の integrity check で失敗 = 中止、F-復旧**
   - **画面何も出ない = LK が完全に動作不能 = 中止、F-復旧**

### 中止判断 F-5

- **preloader が LK を verify する実装**なら、パッチ LK は絶対に起動しない → 純正 LK に書き戻して撤退
- LK 自身が自己 integrity check(hash 計算 + 内蔵ハッシュ比較)を持つ場合、パッチ検出でも起動しない → 復旧

### F-復旧手順

1. 電源長押し 20 秒で強制シャットダウン
2. USB を PC に抜き挿し(BROM 進入)
3. `tools/firmware/Firmware/lk.bin` を退避したものから復元、または dump/lk.img を使う
4. SP Flash Tool CLI で lk パーティションに純正 LK を焼き戻し
5. 起動確認

---

## Phase F-6: Magisk 焼き再挑戦(F-5 成功時)

### 目的

パッチ LK で AVB verify が無効化された状態を活かして、Magisk-patched boot を焼く。

### 手順

1. mtk-su 再 push、root 取得
2. Magisk apk を端末に adb push(既にある: `tools/Magisk.apk`)
3. `tools/magisk-cli/boot_patch.sh` で dump/boot.img を patch(Phase 3-1 で作った手順を再利用)
4. new-boot.img を dd で boot パーティションに焼き
5. reboot
6. 起動確認
7. Magisk apk 起動、root 確認: `su -c id` で uid=0

### 検証

- `Magisk installed successfully`
- reboot 後も Magisk 状態が維持される
- Sunmi 自動復旧が発動しない(電源長押しでも Magisk boot が残る)

### 成功時

- Magisk 永続 root 実現
- Flutter kiosk 化を Magisk 経由でパワーアップして進める
- 別プラン `docs/downgrade-magisk-plan.md` の Phase D-4/D-5 に相当

---

## Phase F-7: dm-verity 無効化(F-6 成功時、オプション)

### 目的

Magisk 永続 root が動いた後、dm-verity を切って system を rw にする。

### 手順

1. Magisk の「MagiskHide」or 「Zygisk」経由で dm-verity を module から動的無効化
2. または `resetprop ro.boot.veritymode disabled` で kernel 認識を騙す
3. system remount で書き込みできるか確認

### 成功時

- **GSI 焼きの現実性が生まれる**(system 書き換え可能)
- 追加プランに繋げる

---

## 記録するもの

各 Phase で `logs/experiment-F<n>-<subject>.log` に保存:

- `logs/experiment-F0-tools.log`: Ghidra インストール、環境確認
- `logs/experiment-F1-lk-structure.md`: LK の magic, base addr, strings, entry point
- `logs/experiment-F2-verify-function.md`: verify 関数の名前候補、オフセット、コールグラフ
- `logs/experiment-F3-patch-design.md`: 選んだパッチ方法、書き換えバイト、before/after
- `logs/experiment-F4-flash.log`: SP Flash Tool の出力
- `logs/experiment-F5-boot.log`: 起動可否、電源キー押下後の挙動
- `logs/experiment-F6-magisk.log`: Magisk 動作確認
- `logs/experiment-F7-verity.log`: dm-verity 状態

WORKLOG.md にも各 Phase 完了ごとに要約を追記。

---

## 実行前チェックリスト

- [ ] Sunmi Recovery の入り方を再確認(音量+ + 電源キー)
- [ ] `dump/*.img` の SHA256SUMS 検証
- [ ] `tools/firmware/Firmware/lk.bin` を退避 (`tools/firmware/Firmware/lk.bin.2021orig`)
- [ ] SP Flash Tool + auth 経路が動くことを再確認(D-2 実測済み)
- [ ] adb 疎通、mtk-su root 取得できる
- [ ] このプランをレビュー

---

## 全体の予想時間

- F-0(環境準備): 15-30 分
- F-1(LK 構造把握): 30-60 分
- F-2(verify 関数特定): 60-120 分 ← **最も時間かかる**
- F-3(パッチ設計 + バイナリ書き換え): 30-60 分
- F-4(焼き): 5-10 分
- F-5(起動確認): 5 分 → 成功なら F-6、失敗なら復旧 5-10 分
- F-6(Magisk 焼き + 動作確認): 30 分
- F-7(dm-verity 無効化): 30-60 分

**成功パス合計: 3.5-5 時間**  
**失敗時の復旧: 10-20 分**  
**最悪合計(F-5 失敗まで): 3-4 時間**

---

## 判定基準

### 成功条件

1. パッチ LK で通常起動できる(F-5)
2. Magisk 焼き後に reboot しても Magisk root が維持される(F-6)
3. `su -c id` で uid=0
4. 電源長押しで起動しても Magisk が残る(Sunmi 自動復旧が無効化されている)

### 中止判定

- F-1/F-2 で LK バイナリ解析が難読化・暗号化で進まない
- F-3 でパッチ設計が組めない
- F-4 で焼きが失敗
- F-5 でパッチ LK が起動しない = preloader が LK を verify している = プラン失敗、撤退

---

## Q&A

**Q: preloader が LK verify する場合、preloader も同時にパッチできない?**  
A: 理論的可能、ただし preloader は BROM に verify されるので同じ問題(BROM verify を突破する必要 = eFuse の署名鍵が必要 = 私たちには無理)。**preloader が LK verify する場合、C1 経路は完全に詰み**。

**Q: SP Flash Tool + auth で preloader が焼けるが、それは verify 通ってる証拠では?**  
A: SP Flash Tool の auth は "書き込む権限" の認証、BROM の verify とは別。preloader を書き込めるが、書き込んだ preloader が起動時 BROM に verify されて拒否される可能性は依然としてある。preloader は触らない鉄則を守る。

**Q: Sunmi V2 で「これで動いた」実績のあるカスタム LK はある?**  
A: 現時点で確認できていない。dafish7 は LK を触っていない(Magisk-patched boot だけ)。**私たちが最初の挑戦者になる可能性**。
