# Experiment G-8 (継続): Ghidra 頭出し + 詳細 LK 逆解析

**Date**: 2026-07-02 (session 継続)
**Prev**: `logs/experiment-G8-lk-recon.md` (base 0x56000000 確定、SEC_POLICY Table 構造解読まで)
**Env**: Ghidra 11.2.1 + OpenJDK 21 (Temurin, portable install)
**Target**: `scratch/g8-lk-recon/lk-payload.bin` (base=0x56000000, ARM:LE:32:v7)
**Auto-analyze**: 完了、1199 関数抽出

## 決定的な発見: **Sunmi LK は 2 段のロック機構を持つ**

WORKLOG G-7 で「seccfg unlock 単独で META boot に落ちる」と観察されていた挙動の背景が完全解明:

| 機構 | 参照関数 | 参照先 | 意味 |
|---|---|---|---|
| **内部 SBC state** | `FUN_5603a37c` → `FUN_5603a2d0` | `**(int**)(DAT_5603a350 + 0x5603a2d6)` (2 段 pointer) | preloader G-1 と同じ **0x11 (enforcing) / 0x22 (eFuse 依存) / 0 (disabled)** の 3 状態変数、chip level ではなく LK 内部のセキュリティ状態 |
| **seccfg partition** | `FUN_5603b2ec` | `*(int*)(seccfg_buf + 0xc)` | 実際の `seccfg.lock_state` field(0x01=locked、0x03=unlocked) |

これは **G-1 preloader RE で発見した SBC state gatekeeper (fcn.0021277c) と完全に同じ設計パターン** が LK 内にも実装されている。preloader と LK が同じ設計を共有しているのは MediaTek boot chain の特徴。

## seccfg buffer 構造(実測)

`FUN_5603ae74` (SECCFG_DISPATCHER) が新規 seccfg を初期化する時のコードから確定:

```c
// 60-byte seccfg struct (v4)
struct seccfg {
    uint32_t magic;         // +0x00: 0x4D4D4D4D "MMMM"
    uint32_t version;       // +0x04: 4
    uint32_t size;          // +0x08: 0x3c (60)
    uint32_t lock_state;    // +0x0c: 1 (=locked default)
    uint32_t field_10;      // +0x10: 1
    uint32_t reserved_14;   // +0x14: 0
    uint32_t magic2;        // +0x18: 0x45454545 "EEEE"
    uint8_t  hmac[32];      // +0x1c: HMAC-SHA256
};
```

- lock_state 値: 0x01=locked, 0x03=unlocked (G-6 の実測とも一致)
- HMAC (offset 0x1c-0x3b) は `FUN_5603f104` で計算、`FUN_5603acbc` で XOR(?)、`FUN_5602e538` で書き込み

## 完全なコールグラフ

### Boot orchestrator (FUN_56002060, 820 bytes)

`piVar9` = 大きな boot_arg 構造体 pointer。以下の順で処理:

1. `FUN_56021f54(&local_30)` — local_30 = 0x800, uStack_2c = 0
2. **line 45**: `if ((*(int *)(*piVar9 + 0x58c0) == 1) && (FUN_5601ce50() == 0)) → FUN_56000188()` — WD boot 判定
3. **line 69**: `FUN_560029b8()` = **META_flag_handler** を呼ぶ(ここで boot_mode 決定)
4. line 70: `puVar8 = *(uint**)(iVar5 + DAT_560023d0)` — boot_mode global pointer 取得
5. **line 71**: `if (*puVar8 != 99) FUN_5601f60c()` — normal boot 準備
6. **line 75**: `if (*puVar8 == 2 && FUN_560251d8(...) < 0)` — boot_mode == 2 (META) 特殊処理
7. **line 81**: `FUN_5603a390(local_30, uStack_2c)` — seccfg wrapper 呼び出し
8. line 82: `FUN_5603a400(*(uint*)(iVar5 + DAT_560023d8), 0x100)`
9. **line 90**: `if (*(int*)(*piVar9 + 4) == 100) { normal Android boot; }`
10. line 116: `bVar11 = 0x62 < uVar4;` 系分岐(uVar4 = boot_mode)、詳細は fastboot/recovery/META 分岐

### META_flag_handler (FUN_560029b8, 430 bytes)

`iVar5 + DAT_56002b6c` を deref すると boot_mode global。書き込み値:

- `= 1` (line 234): normal boot(reboot_meta_flag 一致時)
- `= 99` (line 242): fastboot 系(charging mode or something)
- `= 2` (line 289): **META boot(デフォルト case)**

fallthrough 順:
1. `if (*(int*)(iVar5 + DAT_56002b5c) != 0 && FUN_56028b60() != 0)` — WDT flag 等、無限 loop
2. `iVar1 = FUN_560045b8(); if (iVar1 != 0) return;` — META_tag_printer 呼び出し、非 0 なら early return
3. reboot_meta_flag 文字列を BootPartition から検索(FUN_56004b68)、値と一致 → boot_mode = 1
4. `FUN_5601e36c()` != 0 → boot_mode = 99
5. line 246-249 の **`0x4c4c4c4c` magic check**: `if (*(int *)(**(int **)(iVar5 + DAT_56002b68) + 0x58e4) == 0x4c4c4c4c && *(int *)(...) + 0x58e8) == 1)` — `LLLL` magic + version 1 = 特殊状態 flag(未特定、`atag,lock` あたりか?)
6. デフォルト boot_mode = 2 (META)

### SEC_POLICY_reader (FUN_5601a6a8, 160 bytes)

このシステム上の「seccfg 状態と SEC_POLICY Table を組み合わせて partition 単位 policy byte を返す」関数。呼出元 = `FUN_5601a7e0` および `FUN_5601a7ec`(それぞれ 12 バイト stub、tail-call wrapper)。

- `local_18` = 内部 SBC state (0 or 1 or 破損)
- `local_14` = seccfg partition lock_state (1, 2, 3, ...)
- 組み合わせテーブル:

| SBC state | seccfg lock_state | 返り値 offset |
|---|---|---|
| 0 (unlocked) | == 3 | entry[+0x15] |
| 0 (unlocked) | != 1,2,3 (or 0) | entry[+0x14] |
| 1 (locked) | == 3 | entry[+0x17] |
| 1 (locked) | else | entry[+0x16] |
| その他 | 任意 | 0 |

lkpatcher patch は entry[+0x14, +0x15, +0x16, +0x17] を全て 00 にした。→ どの組み合わせでも policy = 0 (verify off) となる。しかし、これは partition verify のみで、boot_mode 決定には関わらない。

### seccfg 変更経路

`FUN_5603af2c` (state machine): fastboot commands から呼ばれる。boot flow には登場しない。

- Callers:
  - `FUN_5602ac98` (fastboot_flashing_lock 274 bytes): 音量ボタン確認、`FUN_5603af2c(0)` → lock_state = 3 (unlocked state marker!)
  - `FUN_5602adf0` (fastboot_flashing_unlock 292 bytes): `FUN_5603af2c(1)` → lock_state = 2 or 4

state machine 詳細:
```c
FUN_5603af2c(int param_1) {
    if (seccfg_dispatcher failed) return 0;
    if (param_1 != 0) {   // param_1 = 1 の場合(unlock)
        if (seccfg.lock_state != 1) {  // 既に unlocked
            ... seccfg[0xc] = 4;   // 「一度 unlock された」状態
            print/emit event
        } else {  // locked → transitioning to unlock
            ... seccfg[0xc] = 2;
        }
    } else {  // param_1 = 0 の場合(lock command)
        ... seccfg[0xc] = 3;   // unlocked state
    }
    return 0;
}
```

**注**: lock_state 値の解釈が独特:
- 1 = fully locked (production ship 値)
- 2 = locked-transitioning-to-unlock
- 3 = fully unlocked
- 4 = re-locked (once was unlocked)

## 攻撃の再定義(G-8 で判明した理由)

WORKLOG G-7 では「patched LK で bootloop = preloader RSA verify」「seccfg unlock 単独で META = LK が判定」と観察。G-8 で明らかになった機構:

### patched LK 経路(F-5, G-7)

- LK signature が preloader の RSA verify を通らない
- SEC_POLICY Table を全 0 化しても、これは LK 内の partition verify 制御であり、preloader gate とは独立
- **必要な追加攻撃**: preloader 側の verify skip(G-1 の SBC state gatekeeper `fcn.0021277c` を runtime patch)or LK signature 部の再計算

### seccfg unlock 経路(G-7)

- 純正 LK が seccfg.lock_state = 3 を見て META boot(0x02)を選ぶ
- 判定ロジックはまだ複雑で特定できていない。少なくとも `META_flag_handler` の default case(全条件不一致)に落ちる可能性が高い
- **可能性のある攻撃**: seccfg.lock_state = 1 のまま SBC state だけ書き換えて「実 unlock 相当」を作れれば META に落ちない可能性
- あるいは META_flag_handler の line 289 の `= 2` を patch して `= 100` にすれば Android boot に流れる可能性

### K-touch i9 LK 焼き経路(未実験)

- preloader RSA verify で reject される可能性(preloader が Sunmi 独自 public key のみ受理する場合)
- 実験のみが真実を教えられる、G-7 復旧後に試験可能

## 攻撃 patch 候補 3 案(復旧後の実験用)

### 案 A: META_flag_handler line 289 patch (最も安易)

```
file offset ~0x2ae0 付近(要 assembly 確認)
`str immediate 0x02` → `str immediate 0x64` (100)
```

- 効果: 全ての「default = META」case で boot_mode = 100 になる
- 副作用: 通常起動が壊れる可能性、非常時 fallback が消える
- リスク: 高い(boot 全経路に影響)

### 案 B: SBC state gatekeeper (FUN_5603a2d0) を patch(preloader 版 G-1 と同型)

```
file offset ~0x3a2d0 の関数開始付近
「if (iVar1 == 0x11) return 1;」 → 「return 0;」
```

- 効果: 内部 SBC state を強制的に「unlocked」扱い
- 副作用: SEC_POLICY_reader の分岐が変わる、全 partition の verify policy が偏る
- リスク: 中(状態変数無効化だけ、他ロジックは維持)

### 案 C: `FUN_5603b2ec` (seccfg lock_state reader) 返り値 = 1 に固定

```
file offset ~0x3b2ec
`*param_1 = *(int)(seccfg_ptr + 0xc);` → `*param_1 = 1;`
```

- 効果: seccfg.lock_state を常に 1 (locked) と報告
- 副作用: seccfg 実 unlock 状態でも LK が locked と誤認 = META boot 誘発しない
- リスク: 低(seccfg unlock 判定のみ影響)
- **これが最有力の 1-byte patch 候補**

## 次のTODO(復旧後の実験)

1. **端末復旧**(USB detach 3-8 時間放置 or バッテリー物理切断)
2. mtkclient crash mode で純正 LK 書き戻し(復旧)
3. **案 C の 1-byte patch(FUN_5603b2ec 短絡)を試験**:
   - patched LK でも preloader RSA reject される問題は残るので、`preloader-boot0.img` の verify skip も並行必要
4. G-1 で発見した preloader 内 SBC state gatekeeper を実装レベルで再確認、runtime patch tool を作成
5. K-touch i9 LK dump 準備(GitHub releases から stock zip 取得)

## 生成ファイル

- `scratch/g8-lk-recon/ghidra-proj/` — Ghidra project(auto-analyze 済 1199 関数)
- `scratch/g8-lk-recon/ghidra-string-xrefs.txt` — 全 string の xref/含む関数/呼出元
- `scratch/g8-lk-recon/ghidra-decompile.txt` — 17 関数の decompile
- `scratch/g8-lk-recon/ghidra-seccfg-callers.txt` — seccfg reader 群の callers
- `scratch/g8-lk-recon/ghidra-deep-chain.txt` — deeper call chain
- `scratch/g8-lk-recon/ghidra-final.txt` — SBC state + seccfg buffer parser の decompile
- `scratch/g8-lk-recon/ghidra-scripts/*.py` — 全 Jython script(次のセッションで再実行可)
- `tools/jdk21/jdk-21.0.11+10/` — Temurin JDK 21 portable(200 MB)
- `tools/ghidra/ghidra_11.2.1_PUBLIC/` — Ghidra 11.2.1 install

## 学び

- **Ghidra headless + Jython PostScript は極めて強力**。1200 関数 + xref を数分でナビできる。手作業の r2 より 10-100x 速い
- **Sunmi の LK security 設計は preloader と同型 SBC state gatekeeper**。G-1 で見つけた設計パターンの LK 版がここに再登場 → **1 個の patch scheme が両方に効く可能性**
- **preloader signature reject が最大の壁**。LK patch では超えられない、preloader 側の攻撃が並行必須
- **base 0x56000000 の発見が全てを支えた**。SEC_POLICY Table pointer 群の逆算という手法は次同種 project でもテンプレとして使える
