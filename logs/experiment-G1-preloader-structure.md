# Experiment G-1: Sunmi V2 Preloader RE(構造 + 攻撃対象特定)

**日付**: 2026-07-01  
**対象**: `dump/preloader-boot0.img` (4MB, SHA256 `10d33a52ce7ea88269dccea15cfb3180721b72070bd2a4eb96c1e3f5e86d6424`)  
**成果**: **世界初レベル**の攻撃対象特定 — Sunmi V2 preloader は MediaTek Kamakiri と同型の SBC state 変数を使用

## ファイル構造(全体)

- **ファイルサイズ**: 4MB(mmcblk0boot0 パーティション全体)
- **実コンテンツ**: 先頭 114KB のみ(0x0 - 0x1C89C)、残りは 0xFF padding
- **preloader.bin 実体**: dump 内 0x800-0x1C89C(114,844 bytes)= `scratch/g1-preloader/preloader.bin`

### GFH ヘッダマップ

| offset (dump) | 内容 |
|---|---|
| 0x0 | EMMC_BOOT wrapper |
| 0x200 | BRLYT (Boot Region Layout Table) |
| 0x808 | FILE_INFO GFH |
| 0x622C | GFH_BROM_CFG (type=0x1000 len=0x1122) |
| 0xAF14 | GFH sub-block type=0xBBEB |
| 0x10FE4 | GFH sub-block type=0xAF50 |
| 0x11158 | GFH sub-block type=0x0002 len=0x0200(signature block) |
| 0x11740 | GFH sub-block type=0x6F5E |
| 0x1BC98 | MTK_BLOADER 終端マーカー |

### FILE_INFO(decoded)

| フィールド | 値 |
|---|---|
| load_addr | **0x00200F10**(SRAM) |
| file_len | 0x1C09C = 114KB |
| **jump_offset (entry)** | 0xF0 → **VA 0x201000** |
| sig_len | 0x66C(RSA 署名末尾) |
| attr | 0xC2600001 |

## Sunmi 独自カスタマイズの度合い

**preloader バイナリ内に "sunmi" / "SUNMI" / "cyrus" / "CYRUS" 系文字列は 0 件**。

→ **ほぼ MediaTek 標準 MT6739 preloader** に USB PID 変更等の最小限のカスタマイズ。Sunmi 独自 protocol ではなく **標準 MTK preloader の派生**なので、既知の MTK exploit 技術がそのまま適用できる可能性が極めて高い。

## 特定した関数群(全体像)

r2 `aaaa` で 1343 関数、うち auth/verify 系の要:

| 関数 | VA | 役割 |
|---|---|---|
| `fcn.002027d4` | 0x2027D4 | USB DL DA verify **dispatcher**(引数 r0 で分岐) |
| `fcn.002027cc` | 0x2027CC | 上の直前で `bl fcn.0021277c` 呼び、返り値を r0 に |
| **`fcn.0021277c`** | **0x21277C** | **★ SBC state gatekeeper**(secure boot state 読み判定) |
| `fcn.00214174` | 0x214174 | **★ eFuse bit read**(0x11C00060 bit 1)= HW-level secure boot enable |
| `fcn.002066b4` | 0x2066B4 | image auth main(`img_auth_required`, `cert vfy fail`) |
| `fcn.0020b638` | 0x20B638 | image auth alt |
| `fcn.00210ddc` | 0x210DDC | 公開鍵 auth(`pubk auth fail`) |
| `fcn.002124ac` | 0x2124AC | cert verify |
| `fcn.00211f74` | 0x211F74 | seclib_img_auth_load_sig wrapper |

## ★★★ 決定的発見: SBC state gate mechanism

### fcn.0021277c(SBC state 判定関数)の完全ロジック

```asm
0x21277C: push {r3, lr}
0x21277E: ldr r3, [0x2127C4]  ; r3 = 0x9BE8 (offset)
0x212780: add r3, pc          ; r3 = ptr_to_state_table @ 0x21C36C
0x212782: ldr r3, [r3]        ; r3 = *(0x21C36C) = 0x0010B8B8 (SRAM 拡張域)
0x212784: ldr r3, [r3]        ; r3 = *(0x10B8B8) = ??? (runtime SRAM)
0x212786: ldr r2, [r3]        ; r2 = *(???) = SBC state value

0x212788: cmp r2, 0x11
0x21278A: beq → return 1      ; SBC ENFORCING(current Sunmi state)
0x21278C: cmp r2, 0x22
0x21278E: beq → fcn.00214174  ; SBC eFuse-controlled path
0x212790: cbnz r2, log path   ; other values → log + return
0x212792: mov r0, r2 = 0
0x212794: return 0            ; ★ r2 == 0 → SBC DISABLED → verify SKIPPED
```

### ポインタチェーン先の意味

- `0x21C36C` (in code): 静的テーブル、値 = `0x0010B8B8`
- `0x0010B8B8` (in SRAM 拡張域, 0x00100000-0x0011FFFF): runtime 初期化される service dispatch table
- ここに書かれるアドレスの先が **SBC state**(1 バイト or 4 バイトの整数値)

### fcn.00214174(eFuse-controlled path)

```asm
0x214174: ldr r3, [0x214180]  ; r3 = 0x11C00060
0x214176: ldr r0, [r3]        ; r0 = eFuse ctrl reg
0x214178: ubfx r0, r0, 1, 1  ; r0 = bit 1
0x21417C: bx lr               ; return r0
```

**0x11C00060 = MediaTek eFuse controller register**。bit 1 = "secure boot enable" fuse。焼かれていれば 1(通常返す)、焼かれてなければ 0。

### 攻撃対象の明確化

| SBC state 値 | 判定結果 | 我々への意味 |
|---|---|---|
| 0 | verify DISABLED | **★ ここに書き換えれば全防御突破** |
| 0x11 | verify ENFORCING(常時) | Sunmi 現状、突破不可 |
| 0x22 | eFuse bit 1 依存 | eFuse に書けないので実質同じ |

**Sunmi V2 の seccfg が LOCKED(state=1)確認済み** → 起動時に SBC state は 0x11 に初期化される。

**必要な攻撃 primitive**:
- USB DL コマンドで **runtime SRAM 内の SBC state 値 を 0 に上書き**する
- SBC state のアドレスは runtime 決定なので、SRAM を dump しないと具体的な数値は不明
- ただし範囲は狭い(0x00100000-0x0011FFFF)、特に fcn.0021277c 実行時に SRAM を dump できれば一発

**これは MediaTek Kamakiri (CVE-2020-0069) の攻撃 pattern そのもの** — mtkclient 系 tool が使う技術。

## USB command dispatcher

- `fcn.002026f0` 周辺に **`cmp r3, 0xNN; beq handler`** のカスケード
- 特定した command ID:
  - `0xD4`: (handler at 0x2028EE)
  - `0xD5`: 別 handler
  - `0xD7`: → fcn.00202788 → fcn.002027cc → **fcn.002027d4 (DA verify dispatcher)**
  - `0xDB`: 別 handler
- 標準 MediaTek USB DL protocol の command ID と照合すると:
  - 0xC0-0xC1: **hardware/version query**(pre-auth 可能な典型)
  - 0xA0-0xA6: **read/write mem**(WriteReg など、要注意)
  - 0xD0, 0xD3: **SEND_DA, JUMP_DA**
  - **0xD5 = WriteReg** の可能性(要検証)= **書き込み primitive の候補**!

## 攻撃シナリオ(次 Phase での実行案)

### 理論攻撃フロー

1. G-2 で USB pcap 捕獲 → 正規手順の command 順序把握
2. G-3 で全 command ID 仕様書作成、**pre-auth で使える WriteReg 系 command** 特定
3. G-4 で:
   - a. 実機を Cyrus (0e8d:2008) mode に落とす(電源 OFF + USB 挿し)
   - b. 正規 handshake の途中で SRAM を dump するコマンド発行
   - c. SBC state 変数の runtime アドレス特定
   - d. WriteReg で SBC state を 0 に上書き
   - e. その後の DA verify は SKIP される → **任意 DA 実行** → LK/boot 焼き自由
4. G-5〜G-8 で焼き実行 → Magisk root → Android 10 port

### 期待値

- MediaTek Kamakiri は本来 BROM 対象だが、**preloader も同じ SBC state 変数を継承**するのでほぼ同じ攻撃が動く可能性
- Sunmi の追加防御は「USB PID 変更で mtkclient を騙す」程度のように見える
- 実際のプロトコルは stock MTK ならば mtkclient のコードをフォークして PID を 0x2008 対応にするだけで通る可能性

## 次にやること(G-1 完了、G-2 へ)

- G-1: **完了**(構造・攻撃対象・攻撃 primitive 候補まで全部特定)
- G-2: **要 Windows VM** で SP Flash Tool + auth 通信を USBPcap 取得
- G-4 の並行準備: mtkclient を fork して 0x2008 対応 preloader mode を強制、生 protocol を実験できる状態を作る
- Ghidra は補助として後で導入(r2 で十分ここまで進んだ)

## Files

- `scratch/g1-preloader/preloader.bin` — 114KB, GFH headers 込みの実体
- `logs/experiment-G1-verify-functions.log` — r2 disasm 生ログ(auth 系関数)
- `logs/experiment-G1-preloader-structure.md`(このファイル)
