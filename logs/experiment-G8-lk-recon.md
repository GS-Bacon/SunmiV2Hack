# Experiment G-8: Sunmi V2 LK Reverse Engineering Recon

**Date**: 2026-07-02 (session continued from G-7 patched LK attempt)
**Goal**: G-7 復旧待ちの間に、LK 内の "unlock-state check"(seccfg unlocked → META boot への遷移)を特定し、次の patch 実験の土台を作る。
**Target LK**: `dump/lk.img` (Sunmi V2 T5930, 純正 MT6739 LK, 1 MB partition, 505 KB payload)

## 実施成果

### 1. LK payload の base address 特定 = **0x56000000**

- 従来「MT6739 LK は 0x41E00000 base」と仮定していたが実測で否定
- 判定手段: SEC_POLICY Table (offset 0x6c340) の `+0x04` field が pointer で、`0x5604XXXX` 値が並ぶ
- pointer 先 (VA - 0x56000000 = file offset) を decode すると **"default", "preloader", "lk", "logo", "boot", "system", "tee1", "tee2", "oemkeystore", "keystore", "userdata", "md1img", "md1dsp", ...** と partition 名文字列が並び、完全一致
- 従って LK base = **0x56000000** で確定

### 2. SEC_POLICY Table 構造の完全解読

- **開始 offset**: file 0x6c340 (VA 0x5606C340)
- **エントリ 1 個 = 28 バイト**、少なくとも 21 エントリ
- 各エントリ構造:

```c
struct sec_policy_entry {
    uint32_t reserved_0;    // +0x00: 常に 0 (patched-original 両方)
    uint32_t name_ptr;      // +0x04: partition 名文字列への VA (0x5604XXXX)
    uint32_t reserved_2;    // +0x08: 0
    uint32_t reserved_3;    // +0x0c: 0
    uint32_t reserved_4;    // +0x10: 0
    uint16_t lock_state;    // +0x14: 01 (locked) / 00 (unlocked)
    uint16_t mode;          // +0x16: 03 (verify enforced) / 00 (skip)
    uint32_t next_ptr;      // +0x18: 次エントリ or 0
};
```

- **lkpatcher が patched した部分**: entry 19 以降の `lock_state (01→00)` + `mode (03→00)`
  - Entry 0-18 は元々 `lock_state=0` (partition が verify 対象外扱い?)
  - Entry 19+ (0x6c554 以降) が boot / recovery / lk 系の enforced partition
- **判定**: この Table は「boot chain の各段で verify するかどうか」を制御する。lkpatcher が単に verify を off にしただけ。preloader 側の verify (DAA) は別 gate。

### 3. LK payload の重要文字列一覧(判明済み VA 付き)

| VA | file offset | 文字列 | 用途 |
|---|---|---|---|
| 0x5604e888 | 0x4e888 | `[SEC_POLICY] lock_state(default) = 0x%x` | SEC_POLICY 検索 fallback ログ |
| 0x5604e8b4 | 0x4e8b4 | `[SEC_POLICY] lock_state = 0x%x` | SEC_POLICY entry hit ログ |
| 0x5604ef54 | 0x4ef54 | `[%s]: Lock seccfg ` | write protect 系ログ |
| 0x5604ef70 | 0x4ef70 | `[%s]: Lock seccfg failed:%d` | 同、失敗系 |
| 0x56043154 | 0x43154 | `reboot_meta_flag` | META 遷移 flag 変数名(文字列参照) |
| 0x560434a9 | 0x434a9 | `unlocked` | AVB status 文字列 |
| 0x56044710 | 0x44710 | `boot-recovery` | Recovery entry 文字列 |
| 0x56044750 | 0x44750 | `[META]` | META boot ログ |
| 0x5605e2c1 | 0x5e2c1 | `Bypass Kernel Power off charging mode and enter Meta Boot` | META entry log |
| 0x5605d7a0 | 0x5d7a0 | `Set_RTC_Fastboot_Mode` | fastboot RTC flag |
| 0x56061e64 | 0x61e64 | `oem reboot-recovery` | fastboot cmd |
| 0x560613d4 | 0x613d4 | `atag,meta` | kernel atag |
| 0x56068a90 | 0x68a90 | `[%s] read seccfg` | seccfg read fn |
| 0x56068abc | 0x68abc | `[%s] write seccfg` | seccfg write fn |
| 0x56068ae0 | 0x68ae0 | `[%s] Initializing seccfg` | seccfg init fn |
| 0x56068ba8 | 0x68ba8 | `[%s] Show seccfg` | seccfg dump fn |
| 0x56068cd4 | 0x68cd4 | `[%s] seccfg not` | seccfg 見つからず |

### 4. String xref 追跡の中間状況

- **課題**: 上記文字列 VAs は 4-byte-aligned literal pool にも、MOVW/MOVT 対にも一致しない
- **原因推測**: MediaTek LK は string を「近傍の anchor VA + ADD/SUB immediate」で参照している可能性が高い。GCC の -Os 最適化で literal pool の重複を排除、複数 string を一つの LDR で共有し、ADD で adjust するパターン
- **確認済 anchor 例**: 0x5604e908 ("default") は SEC_POLICY table 内で literal として存在(1 個)。周辺の "lk", "system", "tee1", ..., "mcupmfw" (0x5604e954 〜 0x5604e9cc) も同様に literal 存在
- **推論**: `[SEC_POLICY] lock_state = 0x%x` (0x5604e8b4) は、コードが 0x5604e908 を load し、`SUB #0x54` で `[SEC_POLICY] lock_state` の先頭を得るような accessing pattern と推定される

### 5. lkpatcher patch との照合(G-7 実験の再解釈)

- G-7 で焼いた `scratch/lk-patch/lk-patched.img` は、SEC_POLICY Table の entry 19+ の 21 field を patch(01/03 → 00/00)している
- しかし、この patch は **preloader の LK verify を通らない** (F-5 / G-7 で確認済)
- → **SEC_POLICY Table のみの patch では、LK が preloader の signature check を通過しない**
- **結論**: preloader 内の LK 検証 (DAA) が別 gate、SEC_POLICY Table は LK 自身が自 partition の verify を管理する内部構造。この 2 段は独立
- **G-7 が bootloop になった真の理由**: preloader が patched LK の RSA signature を verify して reject。SEC_POLICY 改変とは無関係の防御

## 次にできる有望な RE

1. **Ghidra で decompile**(要 JDK 21 install または JAVA_HOME_OVERRIDE)
   - LK base 0x56000000 で load
   - Auto-analyze で関数境界確定
   - "reboot_meta_flag" / "atag,meta" 参照を辿って META boot 遷移関数を特定
   - その関数の caller が「seccfg 読み込み → unlocked 判定 → META へ」の分岐
2. **seccfg partition 読み込み関数の逆解析**
   - "[%s] read seccfg" 参照(0x56068a90)
   - 読んだ seccfg buffer の lock_state field を検査する場所を追う
3. **META boot 遷移路の逆解析**
   - "Bypass Kernel Power off charging mode and enter Meta Boot" (0x5605e2c1) は明確な META 突入ログ
   - この文字列を出力するコードの前提条件を追う

## 副次的な確定情報

- LK image 全体は 1 MB partition の header(0x200 bytes)+ payload 505 KB + padding + signature
- payload は raw ARM/Thumb-2 混在(startup vector table = ARM32、本体 = Thumb-2)
- payload 内の MOVW/MOVT pair = 1161 対、うち 415 対が MMIO region (0x14000000: GPU registers 等) を指す

## 参考ファイル

- `scratch/g8-lk-recon/lk-payload.bin` — extracted payload
- `scratch/g8-lk-recon/lk-strings.txt` — 全 strings dump
- `scratch/g8-lk-recon/find_xrefs_v2.py` — xref finder (base 0x56000000)
- `scratch/g8-lk-recon/string-xrefs-report.txt` — 現状 0 hit だが今後の base 補正で更新可
- `scratch/g8-lk-recon/find_string_xrefs.py` — 初版
- `dump/lk.img` — 純正 LK 1 MB
- `scratch/lk-patch/lk-patched.img` — G-7 で焼いた patched LK
- `logs/experiment-G7-patched-lk-attempt.md` — G-7 実験の完全レポート
- `logs/experiment-G4-mtkclient-victory.md` — G-4 世界初 secure boot 突破の完全レポート
