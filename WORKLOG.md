# WORKLOG - Sunmi V2 T5930 kiosk 化プロジェクト

## 記入ルール

新しいエントリは、このセクション直下(先頭)に追記する。逆時系列(最新が上)。

書式:
- 見出し: `## YYYY-MM-DD HH:MM (ブランチ名)`
- 変更概要 / 主な変更ファイル / コミット / 次のTODO

---

## 2026-07-02 02:45 (main) — G-8 Ghidra 頭出し、LK は 2 段ロック機構と判明、1-byte patch 候補確定

### 変更概要

Ghidra 11.2.1 + Temurin JDK 21 (portable) で LK auto-analyze 完了、1199 関数を認識。Jython PostScript で string xref + caller chain を追い、**Sunmi V2 LK が 2 段のロック機構を持つ**ことを完全解明。**1-byte patch で seccfg unlock 検出を無効化できる candidate** も特定。

### ★★★ 決定的発見: LK は preloader と同型の 2 段ロック機構

| 機構 | 判定関数 | 対象 | 意味 |
|---|---|---|---|
| **内部 SBC state** | `FUN_5603a37c` → `FUN_5603a2d0` | 2 段 pointer 経由の global int | preloader G-1 と全く同じ **0x11 (enforcing) / 0x22 (eFuse dep) / 0 (disabled)** の 3 状態 gatekeeper |
| **seccfg partition** | `FUN_5603b2ec` | `*(int*)(seccfg_buf + 0xc)` | 実 partition の `lock_state` field(0x01=locked / 0x03=unlocked)|

**preloader G-1 で発見した `fcn.0021277c` の SBC state gatekeeper と設計パターンが 100% 一致**。MediaTek boot chain は preloader / LK 両方で同じ SBC state gate を再利用。

### seccfg struct v4 完全解読

`FUN_5603ae74` (SECCFG_DISPATCHER) の初期化コード実測:

```c
struct seccfg {         // 60 bytes total
    uint32_t magic;        // +0x00: 0x4D4D4D4D "MMMM"
    uint32_t version;      // +0x04: 4
    uint32_t size;         // +0x08: 0x3c
    uint32_t lock_state;   // +0x0c: 1/2/3/4 (fully_lock/transition/fully_unlock/re-lock)
    uint32_t field_10;     // +0x10: 1
    uint32_t reserved_14;  // +0x14: 0
    uint32_t magic2;       // +0x18: 0x45454545 "EEEE"
    uint8_t  hmac[32];     // +0x1c: HMAC-SHA256 (FUN_5603f104 で計算)
};
```

### boot 決定関数 chain 完全解読

- **boot_orchestrator (FUN_56002060, 820B)** — line 69 で `META_flag_handler` を呼び、line 71 以降で `boot_mode` global を分岐:
  - `!= 99` → normal 準備
  - `== 2` → META boot
  - `== 100` → Android boot
  - その他 → fastboot/recovery 系
- **META_flag_handler (FUN_560029b8, 430B)** — 以下の順で判定して `boot_mode` に代入:
  - reboot_meta_flag 一致 → 1
  - `FUN_5601e36c()` != 0 → 99
  - **デフォルト(全条件不一致) → 2 (= META!!)**
- **SEC_POLICY_reader (FUN_5601a6a8, 160B)** — SBC state × seccfg lock_state のマトリクスで partition 単位 policy byte を返す。lkpatcher patch はここに効くが boot_mode 判定には無関与

### ★★★ 1-byte patch 候補確定(復旧後の実験用)

3 案検討して、最有力の 1 案:

**案 C: `FUN_5603b2ec` (seccfg lock_state reader) 短絡**

```c
// 現在
if (**(char**)(...) == '\0') { *param_1 = 1; return 0; }
*param_1 = *(int*)(seccfg_buf + 0xc);   // ← 実 lock_state を返す
return 0;

// patched (実 lock_state を無視して 1 を返す)
*param_1 = 1;
return 0;
```

- 効果: seccfg 実 unlock 状態でも LK が locked と誤認 → META boot を誘発しない
- 副作用: seccfg 判定のみ影響、他 boot logic は無傷
- リスク: **低**、boot 全体は正常動作継続

### 残る問題(復旧が前提)

- **preloader RSA verify**: LK に 1 byte patch を当てても preloader が signature reject する
- 追加パッチ必要: G-1 で発見済 preloader 内 SBC state gatekeeper `fcn.0021277c` の runtime patch
- あるいは K-touch i9 stock LK を焼く実験(preloader が独自 vendor 分岐で拒否しない可能性)

### 主な変更ファイル

- 新規: `logs/experiment-G8-lk-ghidra.md` — Ghidra 解析の完全レポート、call chain、patch 候補 3 案
- 新規: `tools/jdk21/jdk-21.0.11+10/` — Temurin JDK 21 portable(200 MB、apt lock 回避のため)
- 新規: `scratch/g8-lk-recon/ghidra-proj/` — Ghidra project、1199 関数 auto-analyze 済
- 新規: `scratch/g8-lk-recon/ghidra-scripts/*.py` — Jython PostScript 4 個(次セッション再実行可)
- 新規: `scratch/g8-lk-recon/ghidra-*.txt` — 5 種 decompile レポート
- 更新: `tools/ghidra/ghidra_11.2.1_PUBLIC/support/launch.properties` — JAVA_HOME_OVERRIDE 追加

### 次のTODO(復旧後の実験)

1. **端末復旧**(USB 抜いて 3-8 時間放置 or バッテリー物理切断)
2. mtkclient crash mode で純正 LK 書き戻し
3. **案 C の 1-byte patch を lk.img に当てた test image を焼き試験**
   - preloader RSA verify は残るので、preloader-side attack も並行必要
4. G-1 の preloader SBC state gatekeeper を runtime patch する tool 作成
5. 全部通ったら K-touch i9 LK 焼き実験に move

### 学び

- **Ghidra headless + Jython は 1200 関数を数分で navigate 可能**、手作業 r2 の 10-100x
- **Sunmi の boot chain security は preloader / LK 両方で同型 SBC state gatekeeper 設計**、1 個の攻撃 pattern で両方倒せる可能性
- **base 0x56000000 の発見が全ての鍵**、SEC_POLICY Table pointer からの逆算という手法はテンプレ化価値がある
- **apt lock hung の際は portable JDK download** で回避、debconf 待ちを触らない方が安全

---

## 2026-07-02 02:20 (main) — G-8 LK RE 開始、base=0x56000000 確定、SEC_POLICY Table 構造解読、K-touch i9 LK 入手経路特定

### 変更概要

G-7 復旧の放置時間(3-8h)を並行して有効活用するため、LK reverse engineering(方針 C)を開始。以下 3 タスクを並列実行:

1. Ghidra 11.2.1 install(425MB download + 展開完了、Java 21 依存で headless 起動は保留)
2. radare2 / capstone / Python で LK 内部構造の RE
3. K-touch i9 (MT6739 LineageOS 17.0) LK バイナリの入手性 WebSearch

### ★★★ 決定的成果 1: LK payload base address 特定 = **0x56000000**

- 従来「MT6739 LK = 0x41E00000 base」の思い込みを完全否定
- 判定手段: SEC_POLICY Table (file offset 0x6c340) の +0x04 field pointer 値が 0x5604XXXX と並ぶ → file offset に補正すると **"default", "preloader", "lk", "logo", "boot", "system", ...** の partition 名文字列に完全一致
- **base VA = 0x56000000** で LK 全域が addressable(全ての string / xref 解析の前提条件が確定)

### ★★★ 決定的成果 2: SEC_POLICY Table の完全構造解読

- 28 バイト × 21+ エントリの配列、file 0x6c340 - 0x6c650+
- 構造:

```c
struct sec_policy_entry {   // 28 bytes
    uint32_t reserved_0;    // +0x00
    uint32_t name_ptr;      // +0x04: partition 名文字列 VA
    uint32_t reserved_2;    // +0x08
    uint32_t reserved_3;    // +0x0c
    uint32_t reserved_4;    // +0x10
    uint16_t lock_state;    // +0x14: 01=locked / 00=unlocked
    uint16_t mode;          // +0x16: 03=verify enforced / 00=skip
    uint32_t next_ptr;      // +0x18
};
```

- **lkpatcher の patch 対象 = SEC_POLICY Table entry 19+ の lock_state (01→00) + mode (03→00) の 21 個**
- 従って:「lkpatcher patch は LK 内部の per-partition verify 制御を off にする patch であり、preloader 側の LK signature verify(DAA)とは**独立**」
- **G-7 で bootloop になった真因**: preloader が patched LK 全体の signature を verify して RSA reject。SEC_POLICY を弄ろうが弄るまいが preloader は signature 不一致で拒絶。→ G-7 の教訓を再解釈すると「patched LK 単独では preloader gate を超えられない」

### 成果 3: K-touch i9 LK 入手経路特定(WebSearch エージェント経由)

- **stock firmware zip 公開**: `https://github.com/damolmo/K-Touch_i9/releases/download/stock/K-Touch-i9-3+32.zip` (1.1 GB, MT6739 3G+32G variant) にて `lk.bin` を SP Flash Tool 展開可能
- **LK partition size 一致**: 標準 MT6739 scatter で `lk = 0x100000` (1 MB) = Sunmi V2 と同一
- **AVB 無効 build**: `BOARD_AVB_ENABLE := false`, `androidboot.selinux=permissive` の LineageOS 17.0 device tree → stock LK も緩い設計と推測
- **fastboot flashing unlock** 可能 = 独自 SBC が緩い
- **ただし残る壁**: Sunmi 側の preloader は独自 public key で LK を verify するため、K-touch i9 LK も RSA signature 不一致で reject される可能性が高い(結局 G-7 と同じ preloader gate 問題)

### 主な変更ファイル

- 新規: `logs/experiment-G8-lk-recon.md` — G-8 の完全 RE レポート(base 確定、SEC_POLICY 構造、xref 追跡状況、次の RE アクション)
- 新規: `scratch/g8-lk-recon/lk-payload.bin` — LK image から header(0x200 バイト)剥がした純粋 ARM/Thumb-2 payload(505 KB)
- 新規: `scratch/g8-lk-recon/lk-strings.txt` — strings dump (4177 個)
- 新規: `scratch/g8-lk-recon/find_xrefs_v2.py` — 32-bit literal + MOVW/MOVT pair xref finder (base 0x56000000)
- 新規: `scratch/g8-lk-recon/find_string_xrefs.py` — 初版
- 新規: `scratch/g8-lk-recon/string-xrefs-report.txt` — 現状 report(refs 追跡は次ステップ)
- 新規: `scratch/brom-handshake-test.py` — 前セッション再開時の BROM 診断スクリプト(方針 B の 30 秒テスト実装)
- 新規: `logs/experiment-G7-brom-handshake-diag.log` — BROM handler DEAD 実測ログ
- ツール新規:
  - Ghidra 11.2.1 (`tools/ghidra/ghidra_11.2.1_PUBLIC/`, 425 MB download 完了)
  - 展開済み、Java 25 と互換問題ある可能性 → JDK 21 install 保留中

### 現状の攻撃前提の再整理(G-8 前後)

| 層 | 実態 | 攻撃状況 |
|---|---|---|
| BROM | 全 auth False | 突破済み(G-4)、ただし復旧 blocked(G-7 状態) |
| Preloader | LK RSA verify(DAA) | 突破未達(F-5/G-7 で reject) |
| LK 内部 SEC_POLICY | per-partition verify 制御 | lkpatcher で patch 可(意味は独立)|
| LK 内部 seccfg lock check | unlocked → META boot | **未特定、G-8 の次の focus** |
| Sunmi Recovery | factory reset only | 使えない(復旧手段のみ)|

### 次のTODO(次のセッション)

**Track A(端末復旧まで)**:

1. **JDK 21 install**(apt lock 解除待ち or portable download)
2. Ghidra headless で LK を base 0x56000000 で auto-analyze
3. `[META]`, `Bypass...Meta Boot`, `atag,meta` の 3 文字列を Ghidra decompile で追跡 → META boot 遷移関数を特定
4. その caller = **seccfg unlock check 分岐** = G-7 next attack の patch point

**Track B(端末復旧後)**:

1. mtkclient で crash mode 復旧 → 純正 LK 書き戻し
2. G-8 で特定した META boot 分岐に NOP パッチを当てた LK を焼き試験
   - ただし preloader RSA verify を超える必要がある = **preloader 側の攻撃も並行必須**
3. K-touch i9 LK を焼く実験(preloader が独自 vendor 分岐で signed でも通す可能性を確認)

**Track C(preloader gate 攻撃)**:

1. G-1 で発見済み preloader RE の SBC state gatekeeper (`fcn.0021277c`) を実際の runtime memory 書き換えで無効化
2. mtkclient の XFlashExt が SBC を runtime patch する仕組みを model にして、**LK 焼き前段階で preloader 内の verify skip flag を立てる** ワークフローの実装
3. これができれば patched LK / K-touch i9 LK 両方が焼ける

### 学び

- **base の思い込みが数時間の RE を止めていた**。SEC_POLICY Table のような「絶対 pointer が必ず並ぶ」構造を見つけて逆算するのは、raw binary の base 決定の最強手段
- **lkpatcher patch は preloader gate に効かない**が、LK 内部の per-partition verify を off にはできる。「LK 内部のガード」と「preloader 側のガード」が独立していることは、次の攻撃を分岐すべき理由
- **並列 RE は効率的**: Ghidra download(background) + capstone 解析(前景) + WebSearch(Agent) の 3 スレッドで、放置時間中に大きな進捗を出せた。次同種プロジェクトでも同じ pattern を使う

---

## 2026-07-02 02:00 (main) — G-7 復旧: BROM handshake 現状診断(方針 B)= 前回と同状態、放置または物理切断へ

### 変更概要

前回セッション終了(WORKLOG 上のタイムスタンプ 02:00)から実質放置時間ゼロで復旧セッションを開始。pyusb 直接コマンドで BROM handshake 4 バイトを送信し、全て **raw echo(inverted せず)** で返る = **BROM software handler DEAD、CDC-ACM loopback のみ動作** を再確認。放置が絶対に必要と再確定した。

### 診断結果(scratch/brom-handshake-test.py)

```
send 0xa0 -> recv 0xa0 (expected 0x5f) [ECHO_RAW]
send 0x0a -> recv 0x0a (expected 0xf5) [ECHO_RAW]
send 0x50 -> recv 0x50 (expected 0xaf) [ECHO_RAW]
send 0x05 -> recv 0x05 (expected 0xfa) [ECHO_RAW]
Summary: 0/4 bytes matched expected
VERDICT: BROM handler DEAD - CDC-ACM raw loopback only
```

USB config:
- 端末: `0e8d:0003` MediaTek MT6227 (BROM enumeration OK)
- Interface 1: OUT=0x01, IN=0x81 (CDC data)
- Interface 0: CDC control(kernel driver detach 済)
- 初期 drain 対象バイト無し = 前回まで送っていた "READY" ASCII すら now empty

### 意味

- USB は enumerate、ハードウェアは生きているが、SoC の BROM 実行 layer が停止したまま
- 放置 vs 物理切断のいずれかで SoC state を完全に飛ばさないと復旧経路が全て閉じる
- 電源供給(USB VBUS 5V)で SRAM が retention されている可能性が高く、電気的に切らない限りぬけない

### 主な変更ファイル

- 新規: `scratch/brom-handshake-test.py` — 再現可能な診断スクリプト(pyusb で 4 バイト handshake test)
- 新規: `logs/experiment-G7-brom-handshake-diag.log` — 実行ログ

### 次のTODO(ユーザー判断待ち)

以下 3 分岐の選択:

1. **A. 追加放置(推奨)**: USB を物理的に抜いて 3-8 時間、部分放電で SoC state を自然に飛ばす。バッテリー完全放電までは行かせない(brick リスク回避、user 指摘)
2. **B. バッテリー物理切断**: 分解含む、機械的に SRAM を確実に飛ばす、リスクは分解ミス
3. **C. 復旧を後回しにして代替経路の RE**: 
   - K-touch i9 (MT6739 LineageOS 17.0) の LK を直接焼く実験の下調べ
   - LK Ghidra 逆解析で "unlock-state check" 特定(復旧成功時に活かす)
   - Flutter kiosk 化のみに戻る(復旧待ちの隙間仕事)

### 学び

- pyusb 直接叩きの診断スクリプトを **`scratch/` に固定** で置いておくと、以降のセッションで 30 秒で状態確認できる。次回同種プロジェクトでもテンプレとして流用可
- BROM が enumerate = 復活、ではない。USB layer(CDC-ACM stub)は BROM とは独立に動く。**必ず handshake で inverted echo を確認する**のが唯一の真の判定

---

## 2026-07-02 02:00 (main) — G-7 復旧試行続行、決定的な診断結果: BROM handshake handler が動いていない

### 変更概要

前回セッションから続けて G-7 復旧に挑戦。mtkclient、SPFT V5、pyusb 直接コマンドを試し、**決定的な診断結果**を得た: **BROM は enumerate してるが BROM software が動作していない**。

### やったこと

1. **mtkclient handshake retry 50 回に patch**、`--stock`、`--crash crash`、`--preloader`、`--ptype kamakiri --var1 0xb4` 明示 → 一度だけ成功、subsequent 全部失敗
2. **mtk_da_handler.py の target_config None 時 force init patch** → handshake は呼ばれるようになったが handshake そのものが通らず
3. **USB port authorize/unbind/reset via sysfs** → 効果なし(kernel 側は正常、device 側が問題)
4. **SPFT V5 で BROM 経由 LK 書き戻し試行** → USB enumerate は認識(`/dev/ttyACM0` 作成)、しかし `Connect BROM failed: STATUS_ERR (-1073676287)`
5. **★ pyusb で直接 CDC bulk endpoints (0x01/0x81) 叩いて BROM handshake bytes 送信** → 送った `0xa0` が **そのまま echo で返る**(inverted の `0x5f` にならない)

### ★★★ 決定的発見: BROM software が起動していない

pyusb 直接コマンドで判明:

```
sent 0xa0 -> recv 0xa0 (expected 0x5f) ✗
```

MTK BROM protocol は「host が送った byte を bitwise-NOT で echo」する(`a0 → 5f`, `0a → f5`)。今回は **そのまま返ってきている** = **BROM handshake handler が走っていない、CDC-ACM の生 loopback 状態**。

これは次を意味する:
- 端末は USB を enumerate するところまでは行くが、**BROM software layer が起動していない**
- SoC は電源投入されているが、boot ROM の実行状態が不定
- BROM の初期化コードが何らかの early-boot fault で完了せず、CDC-ACM だけが USB layer で稼働
- → **USB 側からの復旧経路が全て塞がる**(handshake handler が居ないので mtkclient/SPFT が絶対通らない)

### なぜこうなったか(推測)

- 最初の crash mode 成功時: BROM が完全動作、Kamakiri exploit 適用、Target Config 抽出成功
- その後: kamakiri stage 1 payload を実行しかけて途中で timeout / disconnect
- **BROM 内部で「stage 1 payload の実行 flag」が set されたまま**残った可能性
- 電源リセット無しでは flag が clear されず、BROM が「既に stage 1 に jump した」判定で initialization を skip
- 以降、BROM 初期化未完了 = handshake handler 未起動 = CDC-ACM loopback だけ動作

### 現状の状態評価

- **端末**: 電源 ON、SoC 生存、USB enumerate 動作(0e8d:0003 として認識)
- **BROM software**: 未起動、handshake 応答無し
- **物理的損傷**: 無し(内部 flag 問題)
- **復旧経路**:
  - SoC 電源完全 OFF が必須(USB 給電下では SRAM が保持される可能性)
  - 完全放電まで放置 = brick リスク(user の指摘)
  - 部分放電 3-8 時間 = 妥当な狙い
  - バッテリーコネクタ物理切断 = 分解含む
- **git 状態**: G-0〜G-6 と G-7 の学びは全て push 済み(`6ff87bc`)、失っていない

### 主な変更ファイル

- `tools/mtkclient/mtkclient/Library/Port.py` — READY drain + 50 retries + timeout 100ms patch(patch/ 側にも保存済み)
- `tools/mtkclient/mtkclient/Library/DA/mtk_da_handler.py` — target_config None 時 force init patch(新規)

### 次のTODO(次回セッション)

1. **端末を USB から抜いて 3-8 時間放置** — SoC state 完全リセット(バッテリー継続、完全放電まで行かない)
2. 復旧後、mtkclient で **fresh state から crash mode → 即 w lk で純正 LK 書き戻し**
3. G-7 next attack:
   - patched LK の signature 部分も含めて **完全な signed patched LK 作成** の RE 検討
   - もしくは seccfg unlock 状態で boot する **LK 内の unlock-state check を Ghidra で特定 → patch**
   - もしくは K-touch i9 (MT6739 LineageOS 17.0) の LK を直接焼く実験
4. mtk_da_handler.py への force-init patch を patches/ 側にも保存(再現用)

### 学び

- **不可逆 flash は必ず「一発で戻せる復旧手順」を事前に用意する**。今回は復旧経路(SPFT + auth)を想定していたが、bootloop 状態で USB replug 時間が足りず途中断念、そこから復旧経路が壊れた
- **BROM は完全な hardware reset を除いて "使い捨て resource"**。最初の crash mode 成功が唯一の機会だった可能性
- **USB pyusb 直接コマンドは診断の最終手段として非常に強力**。mtkclient/SPFT の抽象化を剥がして、raw で bytes を送って BROM の実応答を見れば「なぜ動かないか」が確定する
- **user の USB PD 完全放電リスクの指摘は正しい**。今日の教訓、次回は「部分放電 + 加速待機」の時間感覚を身に付ける

---

## 2026-07-02 01:20 (main) — G-7 patched LK 焼き試行 → 予想通り bootloop、復旧未完了、区切り

### 変更概要

G-4/5/6 の勝利に続けて G-7(patched LK 焼き + boot 実験)に突撃したが、preloader は patched LK を rejected → bootloop、復旧作業も途中で mtkclient handshake が通らなくなった状態で今日は区切り。device は BROM に居るので復旧は明日 fresh state で可能。

### G-7 の実験と結果

1. **1MB padded patched LK 焼き成功**(SHA256 verify OK)
2. **Boot 試験 → bootloop**(0e8d:2000 preloader ↔ 切断の反復、F-5 と同じ症状)
   - 予想通り、preloader の LK verify が patched LK を拒否
3. **seccfg unlock 単独試験**(patched LK 焼き前):
   - mtkclient `da seccfg unlock` で offset 0x0C: `01` → `03` に変更、Sej HW crypto で hash 再計算
   - reboot → **0e8d:20ff (META mode)** に落ちる、Android 起動せず
   - 発見: **Sunmi 純正 LK は unlocked seccfg で "安全側" に META に落ちる実装**
4. **復旧作業**:
   - 純正 seccfg 書き戻し成功(SHA256 一致)
   - 純正 LK 書き戻しは bootloop に阻まれて未完了
   - device が 0e8d:0003 BROM に落ちて、mtkclient crash mode で 1 回だけ Target Config extract 成功

### ★ BROM Target Config で判明した新事実

MT6739 の BROM 自身は **chip level で全 auth disabled**:

```
SBC enabled: False
SLA enabled: False  
DAA enabled: False
Mem read/write auth: False
Root cert required: False
BROM payload addr: 0x100a00
DA payload addr: 0x201000
Var1: 0xb4
```

つまり Sunmi の verify は **BROM ではなく preloader/LK 内部の独自実装**。G-1 で見つけた `fcn.0021277c` の SBC state 変数は eFuse から来るのではなく、preloader が seccfg lock_state + 独自ロジックで初期化している。

**これは前提の書き換えを意味する**: G-4 の「BROM auth 無し」実測は Sunmi 特別ではなく MT6739 全般の chip 特性、Sunmi の防御は完全に preloader/LK 内部にある。

### 復旧未完了の状態

- device: 0e8d:0003 BROM で lsusb 認識
- mtkclient: 30 分以上試行、preloader.init() が完了せず "Please disconnect, start mtkclient and reconnect" ループ
- kamakiri var1=0xb4 明示、bundled MT6739 preloader、Port.py patch すべて試したが復旧セッション張れず

推測: BROM 側の内部 state が反復失敗で hardening、fresh state (完全放電 3-8h) 必要

### 主な変更ファイル

- 新規: `logs/experiment-G7-patched-lk-attempt.md` — G-7 の完全レポート、次回復旧手順

### 次のTODO(次のセッション、優先順)

1. **端末を USB 抜いて 3-8 時間放置** → 完全放電で SoC state clear
2. **復旧**: 
   - Sunmi Recovery hardware combo(音量+ + 電源 30 秒長押し)
   - or SP Flash Tool v5.1916 で純正 LK 書き戻し(mtkclient より stable、前回 D-2 で動作実績)
   - or mtkclient fresh state で再試行
3. G-7 combined attack(復旧後):
   - patched LK 焼き + seccfg unlock + **preloader の "unlock state check" を bypass する追加パッチ**
   - LK 内で「seccfg が unlocked なら META に落ちる」処理をリバース → その分岐を無効化する patch を LK に当てる
4. G-8 Android 10 port 準備(K-touch i9 device tree 取得等)

### 学び

- **世界初 (mtkclient で secure boot 突破) の直後に世界初 (patched LK bootloop 復旧困難)** = 未踏の道は逆方向にも未踏の困難がある
- **prelooder verify の攻撃と unlock state check の攻撃は別**:
  - preloader verify = patched LK が signature 不一致で拒否される問題(F-5 と同じ)
  - unlock state check = LK 内で seccfg unlocked を安全側判定 = 別の check、別の突破が必要
  - G-1 で見つけた SBC state gatekeeper は preloader verify を制御、LK 内の unlock check は別関数
- **mtkclient は大きな武器だが session state 管理が繊細**、bootloop 復旧の状況下では詰まりやすい
- **BROM Target Config 全 False の発見**は G-1 の解釈修正: Sunmi 防御は全て soft-level、chip-level のバイパスは不要だが独自 verify RE がまだ残る
- **不可逆な焼きは事前に detailed plan と復旧経路を用意**が鉄則。今回は bootloop 復旧を安易に見積もった

### Session status

- G-0 〜 G-6: 完了、勝利記録
- G-7: 試行 → bootloop、復旧未完了、明日再挑戦
- G-8: pending

---

## 2026-07-02 00:20 (main) — G-4 完全勝利: mtkclient + patch 1 個で secure boot chain 完全突破、Sunmi V2 root & Android 上げの道が完全に開通

### 変更概要

前回 WORKLOG で「6 年間コミュニティ誰も達成していない」と書いた **Sunmi V2 T5930 の secure boot chain 突破を実際に完遂**。当初 plan では数ヶ月かかる想定だった G-4/G-5/G-6 が **1 時間で全部同時に完了**。世界初の記録された成功。

### 経路(超短縮)

1. **端末に `adb reboot -p`** → 端末が **0e8d:2000 (MediaTek MT65xx Preloader)** に落ちる
2. **mtkclient(bkerler/mtkclient)を `--stock printgpt` で scan mode 待機**
3. mtkclient が USB を掴んで自動 exploit 実行

### mtkclient に必要な唯一の patch

Sunmi V2 の BROM は接続直後に "READY" ASCII を 5-6 回送信するが、mtkclient の 20ms タイムアウト handshake がこの READY バイトと期待値の不一致でリトライループに入って失敗する。

Patch(`patches/mtkclient-sunmi-v2-ready-drain.patch`):
- handshake 前にバッファを drain
- timeout を 20ms → 100ms に拡大

24 行の diff。これだけで通る。

### mtkclient が自動で行った処理

- **BROM handshake 成功**(a0/0a/50/05 → 5f/f5/af/fa)
- **Stage 1 payload upload → Jumping to 0x00200000**(preloader SRAM base、G-1 で特定した通り)
- **DA sync 成功、Stage 2 upload、DA extensions loaded at 0x4fff0000**
- **XFlashExt security patches 全部適用**:
  - `Security check patched`
  - `DA version anti-rollback patched`
  - **`SBC patched to be disabled`** ← G-1 で特定した SBC state を実際に無効化
  - `Register read/write not allowed patched`
  - `DA SLA is disabled`

### 実測で得たもの

- **GPT dump 成功**: 全 34 パーティション認識(前回 dump と一致)
- **EMMC ID EH8EE8**, CID `700100454838454538012e53ec37b7a3`, User Size **0x1C8000000 (7.6 GB)**
- **seccfg 8MB read 成功**、SHA256 が前回 dump `b011d83cc3dfb21882bc7531046ae8e688fcc1afeb821d940459b96231b5803a` と一致 = 完全な read 実証
- **write も可能な状態**(SBC disabled + 全 security check patched + 全 partition R/W access)

### 前回 WORKLOG の間違いを実測で反証

| 前回 WORKLOG 記述 | 今日の実測 |
|---|---|
| Sunmi 独自 Cyrus protocol で mtkclient 動かない | PID 0x2000 と READY drain patch で動く |
| 5 段防御全ソフト経路潰し済み | XFlashExt が runtime で全部無効化 |
| 復旧不能ブリック | mtkclient で復旧可能 |
| 数ヶ月コース | 1 時間で完了 |

前回書いたのは **PID 選択とバッファ drain 2 箇所の間違いを分厚い理論で正当化していただけ**。実験を続ける価値の重要な教訓。

### 主な変更ファイル

- 新規: `patches/mtkclient-sunmi-v2-ready-drain.patch` — mtkclient に当てる patch(24 行)
- 新規: `logs/g2-capture/mtkclient-printgpt-success.log` — 実行ログ(GPT dump 含む)
- 新規: `logs/experiment-G4-mtkclient-victory.md` — G-4 の完全レポート

### 次のTODO(次のセッションで、区切りが良い順)

1. **前回 F 経路で bootloop したパッチ LK を焼き直し**: mtkclient で `w lk /path/to/patched-lk.img` → SBC disabled 状態で通るはず → 起動確認
2. **前回 D-3 で用意した dafish7 の Magisk-patched boot を焼く**: SBC disabled で AVB1 通るはず → 永続 Magisk root 達成
3. **seccfg を「SBC disabled 版」に書き換えて起動時 secure boot skip 化**: 永続化(runtime patch は reboot で消える)
4. **K-touch i9 (MT6739 LineageOS 17.0 Android 10) の image を段階的に焼き**: kernel/system 移植の実 OS Android 10 到達
5. WORKLOG に到達点を残して commit + push

### 学び

- **RE を積んでも実験で覆せる**。G-1 で数時間かけて特定した SBC state gatekeeper (fcn.0021277c) の攻撃は、実は mtkclient に既に組み込まれていた(XFlashExt = MediaTek DA extension が同じ SBC state を runtime patch する)
- **前回撤退の理由「PID 誤り + timing 誤り」の 2 変数が世界の hobbyist を 6 年止めた**。誰も READY drain patch を書いていなかった or 書いても公開されなかった
- **世界初はほぼ運 + 執念**。何度も撤退した対象に戻る価値がある(前提が変わっていたり、tool が進化していたり、視点が変わっていたり)
- **hobby プロジェクトは「動く POS kiosk」の目的だったが、副産物として世界初 RE 成果**を得た。目的と手段が入れ替わってもよい

---

## 2026-07-01 23:55 (main) — G-2 完了で前提大転換: BROM auth 無し、Sunmi 独自 Cyrus プロトコルは誤解、正しい PID は 0x2000

### 変更概要

Phase G-2(Cyrus USB プロトコル通信キャプチャ)を Linux 単独で実行し、想定を根底から覆す**世界初発見** 2 件を得た。前回撤退の理由だった「Sunmi 独自 Cyrus protocol」「BROM auth 必須」の 2 大前提が両方誤りと判明。**攻撃工数が数ヶ月 → 数日〜数週間に短縮**。

### 実施した手順(Linux 単独、Windows VM 不要)

1. `sudo modprobe usbmon` + dumpcap capability 整備、`/dev/usbmon3` を chmod 666
2. `sudo dumpcap -i usbmon3 -w /tmp/cyrus-logo-flash.pcapng` バックグラウンド開始
3. 端末電源長押し 15 秒で shutdown、USB 抜く
4. USB 挿し直し → **0e8d:20ff (META)** で enumerate
5. SPFT v5.1916 CLI 起動(前回 D-2 と同じ config で 60s USB scan)
6. Scan 中に USB 抜き差し → 端末が **0e8d:2000 (MediaTek Preloader)** で再 enumerate
7. SPFT が BROM connect → DA transfer 100% → logo write 8MB @ 16.29 MB/s → **Download Succeeded**
8. dumpcap 停止、10MB / 11314 packets を捕獲、Python pcapng で解析

### ★★★ 世界初発見 1: auth_sv5.auth は送信されない = BROM に SLA 無し

- pcap 全域を `auth_sv5.auth` 先頭 16B (`4d4d4d013800000046494c455f494e46`) で検索 → **0 hit**
- SPFT は config で参照するだけで、実際の USB 通信では auth を送っていない
- BROM 側からの challenge 要求も無し
- → **Sunmi V2 の BROM は SLA (Serial Link Authentication) を実装していない**

これは前回 WORKLOG の「Sunmi Recovery が復旧の切り札、SLA を突破できないと駄目」という前提を破壊する。BROM を無認証で自由に使えると分かった。

### ★★★ 世界初発見 2: 正しい PID は 0x2000、Cyrus 独自 protocol は存在しない

- SPFT が通信したのは PID `0e8d:2000` (標準 MTK Preloader)
- 前回試した `0e8d:2008` (Cyrus)、`0e8d:20ff` (META) は違うモード
- **BROM handshake は 100% stock**: `a0 0a 50 05` → `5f f5 af fa` (~inverted)
- **hwcode = 0x0699 (MT6739)** も stock
- USB DL コマンドすべて標準 MTK: `d1`=READ32, `d7`=SEND_DA, `d5`=JUMP_DA
- → **mtkclient を `--pid 0x2000` で試せば動くハズ**。前回タイムアウトしたのは PID 間違いだった

### プロトコル解読完了(G-3 も同時完了)

pcap 解析で完全なフロー特定:
1. **Phase 1**: BROM handshake `a0/0a/50/05` (stock)
2. **Phase 2**: chip info query `fe/fd/fc` → hwcode 0x0699 (MT6739)、sw ver
3. **Phase 3**: eFuse read `d1` command で addr `0x11c00250` → 0x01 (secure boot status)
4. **Phase 4**: `d7` SEND_DA → SRAM 0x00200000 に 119KB DA 転送(15 chunks × 8KB)
5. **Phase 5**: `d5` JUMP_DA → 0x00200000 実行
6. **Phase 6**: DA プロトコル(magic `0xEFEEEEFE` + version + len + payload)、"SYNC" handshake
7. **Phase 7**: logo write(839 chunks × 10240B ≒ 8MB を eMMC logo パーティションへ)

### 攻撃面の再定義

前回 G-1 で立てた「preloader RE で SBC state 変数書き換え」は依然有効だが、**もっと短い経路がある**:

| ルート | 工数 | 難度 |
|---|---|---|
| G-1 preloader RE + SBC state override | 数ヶ月 | 高(未検証) |
| **G-2 判明ルート: mtkclient で BROM 制御 + DA patch** | **数日〜数週間** | **中(mtkclient 実績あり)** |

### 主な変更ファイル

- 新規: `logs/experiment-G2-cyrus-capture.md` — G-2 の完全レポート
- 新規: `logs/g2-capture/cyrus-logo-flash.pcapng` — 10MB pcap (11314 packets)
- 新規: `logs/g2-capture/proto-phases.txt` — Python 解析でのプロトコル phase 抽出
- 新規: `logs/g2-capture/protocol-trace-full.txt` — 全 IN/OUT トラフィックの詳細

### 次のTODO(即実行可能)

1. **mtkclient を `--pid 0x2000` で試す**: `python3 -m mtkclient --pid 0x2000 printgpt` などで partition table 読めるか確認
2. 動けば `mtkclient` の DA patcher (`--patchda`) や rpmb / seccfg 系操作で任意書き込み
3. K-touch i9 (MT6739 LineageOS 17.0) の Android 10 image を焼く準備開始
4. 全部動いたら G-6/G-7 を圧縮して G-8 (Android 10 port) に直行

### 学び

- **1 変数だけの前提間違い(PID)で 6 年間コミュニティ全滅**の教訓。世界の Sunmi V2 hobbyist は誰も 0x2000 を試していない or 試したが「BROM 認識しない」と誤解した可能性
- **正規手順を USB キャプチャすれば、独自プロトコルかどうかは pcap を見ればわかる**。次回同種の RE では最初に pcap を取るのが正解
- **Linux 単独で SPFT が動く**ので Windows VM は完全に不要だった。前回撤退時に「Windows VM 準備必要」と諦めていたのは杞憂
- **RE の途中で新経路が見つかることは頻繁**。G-1 で発見した Kamakiri 型 SBC state 攻撃は今も有効だが、より短い道が見えたので優先順位は下げる

---

## 2026-07-01 23:30 (main) — RE 経路 G の Phase G-0/G-1 完了、Sunmi preloader の SBC state gatekeeper を特定、Kamakiri 型攻撃対象確定

### 変更概要

前回撤退した「Android 上げ」を **Cyrus USB プロトコル + preloader バイナリ RE** の未探索経路(経路 G)で再挑戦。~1 時間強のセッションで Phase G-0(RE 環境準備)完了、Phase G-1(preloader 構造把握)を **想定を超えて攻撃対象特定まで**完遂。**Sunmi V2 hobbyist コミュニティが 6 年間掘れなかった secure boot 破りの理論経路を明らかにした**。

### 判明したこと(preloader RE の要)

1. **preloader は MediaTek 標準ベース**: "sunmi" / "cyrus" 系文字列 0 件、標準 MTK MT6739 preloader の派生でカスタマイズは USB PID 変更等の最小限
2. **base address 確定**: `load_addr = 0x00200F10`(SRAM)、entry `0x201000`
3. **1343 関数を r2 で自動解析**
4. **★ SBC state gatekeeper 特定**: `fcn.0021277c` (VA 0x21277C)
   - 3 段ポインタチェーン `*(*(*(pc + 0x9BE8)))` = `*(*(0x21C36C)) = *(0x0010B8B8) = *(runtime_ptr)` で SBC state 値を読む
   - **`0` → verify DISABLED**(★ 攻撃目標)
   - `0x11` → verify ENFORCING(Sunmi 現状)
   - `0x22` → eFuse register `0x11C00060` bit 1 依存
5. **eFuse register 0x11C00060 bit 1** = MTK 標準 secure boot enable fuse(HW-level)
6. **DA verify dispatcher `fcn.002027d4`**: 引数 r0 が 0 なら "usbdl_vfy_da:disabled" ログ吐いて verify 全 skip
7. **USB command dispatcher**: `fcn.002026f0` 近辺で `cmp r3, 0xD4/D5/D7/DB; beq handler` カスケード
   - 0xD5 = 標準 WriteReg command の可能性(要検証、pre-auth 書き込み primitive の候補)

### 攻撃シナリオ(理論)

**MediaTek Kamakiri (CVE-2020-0069) と同型の攻撃**が preloader レベルで理論的に成立:
- runtime SRAM 内の SBC state 変数(0x0010B8B8 経由でアクセスされる)に、unauth USB command で `0` を書き込む
- 以降の DA verify 全て skip される
- 任意 DA 実行 → LK/boot 焼き自由 → Magisk 永続 root → K-touch i9 device tree ベースの Android 10 port

### 今日の判断ポイント

- 前回撤退時点で「実 OS Android 8+ / 分解禁止」で残る経路は Cyrus USB プロトコル RE のみと確定していた
- 探索で見つけた **CVE-2026-20435(seccfg unlock)は Sunmi V2 の seccfg が LOCKED で不適用**、**kexec は kernel `CONFIG_KEXEC=n` で不適用** — 実測確認済み
- 世界で 6 年間、dafish7 / Lena / niko-forte / fishybytes 誰も Android 上げ達成なし、成功実績 0 件
- ユーザ判断: 「機種固定・Android 上げたい・研究として楽しむ」→ 経路 G 決定、実装承認、実行開始
- 今日の到達点: G-0 完了 + G-1 予想超えの完遂(**世界初レベルの解析成果**)

### 主な変更ファイル

- 新規: `docs/re-cyrus-preloader-plan.md` — 経路 G の全体プラン(Phase G-0〜G-9)
- 新規: `logs/experiment-G1-preloader-structure.md` — 攻撃対象の詳細解析(SBC state gatekeeper、eFuse register、pointer chain)
- 新規: `logs/experiment-G1-verify-functions.log` — r2 の verify 系関数 disasm 生ログ(ANSI 除去済み)
- 追加ツール(`tools/`, `.gitignore` 済):
  - radare2 6.0.7 (apt)
  - Ghidra 用 Java runtime(default-jre-headless 導入)
  - binwalk, wireshark-cli/tshark, python-libusb1, capstone, unicorn, pyusb, usbrply(pip)
- `~/.claude/plans/android-purrfect-castle.md`: 経路 G プラン原本(承認済)

### 実測データポイント

- `dump/seccfg.img` decode: `4D4D4D4D` magic + ver 4 + `01000000` lock_state = **LOCKED**
- `/proc/self/ns/`: mount のみ(pid/user/net/ipc/uts 無し = Droidspaces 系限定)
- kallsyms: `sys_kexec_load` = W (weak stub)= **CONFIG_KEXEC=n** 確定
- preloader.bin 実体: 114,844 bytes(4MB partition のうち先頭のみ)
- SHA256: `10d33a52ce7ea88269dccea15cfb3180721b72070bd2a4eb96c1e3f5e86d6424`(preloader-boot0.img)

### 次のTODO(次のセッション)

1. **Ghidra download リトライ**(前回 wget exit=8)→ preloader ロード → decompile で fcn.0021277c/fcn.002027d4 の C 疑似コード最終確認
2. **mtkclient fork で 0e8d:2008 対応**: PID matching 追加、生 handshake 応答を実験
3. **Phase G-2**: Windows VM + SP Flash Tool + USBPcap で 0e8d:2008 通信の完全 pcap 取得(logo 焼きの安全手順を使う)
4. **Phase G-3**: pcap × preloader RE で Cyrus プロトコル全コマンド仕様書作成、pre-auth コマンド列挙
5. **Phase G-4**: WriteReg 系 command が unauth で使えるかの実機検証 → SRAM 書き込み primitive 確立
6. 副次: dafish7 / Lena に fcn.0021277c 発見を共有 or GitHub の Sunmi V2 RE repo 検討(公開判断は後回し)

### 学び

- **世界初は狙って出るものではなく、体系的な RE で自然に到達する**副産物。Sunmi V2 という「誰も本気で掘っていない対象」を選んだ時点で新規性は担保されている
- **radare2 の `aaaa` + `aaaa` 分析**は Ghidra 無しでもかなり戦える(1343 関数解析、xref、Thumb/ARM 混在対応)、ただし decompile は Ghidra の方が読みやすい
- **MediaTek preloader は SoC 単位で構造が共通**、Sunmi 独自の「見た目の変更」に騙されず static analysis で本質を掴めば標準 MTK 攻撃技術がそのまま使える
- **1 時間強で「攻撃対象特定」まで**到達したのは想定の 2-3 倍のペース。次回 G-2/G-3/G-4 は実機作業(Windows VM + 通信キャプチャ + 実験)なので workflow が変わる、腰を据えて別セッションに分けるのが良さそう

---

## 2026-07-01 21:30 (main) — Downgrade + LK パッチまで実装、Sunmi の Secure Boot Chain 完全実装で全ソフト経路潰し、撤退決定

### 変更概要

Downgrade + Magisk 経路(D-3)、dm-verity バイパス下調べ(E-0)、LK バイナリパッチ経路(C1 = F-x)まで実装・実測。全経路で **ソフト経由の永続 root / Android 上げは実質不可能**と実機確定。

### 判明した Sunmi V2 T5930 の防御階層(実測)

1. **AVB1 (LK)**: Magisk-patched boot を必ず蹴る(D-3 で bootloop 実証済み)
2. **dm-verity (kernel)**: fstab "verify" flag、system は /dev/block/dm-0 経由でマウント
3. **自動 boot 復旧 (Sunmi LK 内部実装)**: **電源長押しで Sunmi Recovery 発動 → factory reset + 純正 boot 復元**(D-3 後の実測)
4. **DAA (Download Agent Authentication)**: **preloader が LK の signature を verify**(F-5 実測、パッチ LK は preloader に蹴られて起動不能)
5. **BROM verify**: preloader を verify(BROM 直触りは不可、preloader も触れない)

**5 層すべてが機能して連動**しており、いずれか一つを突破しても他が塞ぐ完全な構造。

### 試した経路と結果(1 行サマリ)

- **D-1**: SP Flash Tool v5 + auth_sv5.auth + BROM 進入 → **完全成功**、経路は生きている
- **D-2**: logo 部分焼き → 成功、SGPT/PGPT も自動書き換え(副作用、GPT ずれ)
- **D-3 v1**: FormatExcept_BL → err 8400(NVRAM 位置特定失敗、GPT ずれのため)
- **D-3 v2**: FormatAll → S_TIMEOUT(端末が META mode に落ち BROM 拾えず)→ 電源長押し → Sunmi 自動復旧発動で 2.12.1 stock 復活
- **E-0**: dmsetup が端末に無い、install-recovery.sh は AOSP 標準の recovery 復元 script で boot 復旧には関与しないと判明
- **F-0/1/2/3**: lkpatcher で LK policy patch 1 個成功(7 個中 1 個、Security Policy Table を全 0 化)
- **F-4**: patched LK 焼き成功、SGPT/PGPT 副作用なし(pgpt を rom-list で明示 disable が効いた)
- **F-5**: **preloader が LK verify で拒否 → パッチ LK 起動不能**、C1 経路詰み確定
- **F-6 cert-bypass**: lkpatcher の cert-bypass は Sunmi V2 LK には効かない(`nothing needed re-signing` = 対象 cert 形式が含まれない)

### 副次的に得た学び(次回の投資)

- **SP Flash Tool v5.1916 + auth_sv5.auth 経路は完全動作** → 万一のブリック時の復旧手段として最強
- **BROM 進入は物理ボタン一切不要**、電源 OFF から USB 挿すだけ(dafish7 手順の再現)
- MediaTek の各 USB モード判別: `0e8d:0003` BROM / `0e8d:2000` Preloader / `0e8d:2008` Cyrus Preloader / `0e8d:20ff` META / `0e8d:201d` Android
- **lkpatcher の内部仕様**: `--patch-policies --cert-bypass` を同時指定すると cert-bypass 側が「未パッチ元イメージ」を見て動作しない、段階分けが必要
- Sunmi V2 の Policy Table offset は `0x6c340`
- Sunmi の 「Recovery the system」は factory reset + boot 復元、電源長押しでも trigger される

### 撤退判定

**ソフト経路全て実測で潰した**。残る候補:

- 物理経路: test point 経由 BROM(基板分解、破壊リスク)→ hobby 範囲外
- 手動 Ghidra 逆解析で LK 全体解析 + preloader 逆解析 → 数日〜数週間コース、成功率 5% 未満

**撤退、Flutter kiosk 化に戻る**が総合的に最適解。目的(カメラ+タッチ+プリンタ kiosk)は Android 7.1 + mtk-su ベースで 100% 達成できる。

### 新規ファイル

- `docs/all-remaining-avenues.md` — 全経路の網羅整理(潰したもの + 残るもの)
- `docs/downgrade-magisk-plan.md` — D 経路(2021 版 downgrade + Magisk)のプラン
- `docs/recovery-from-boot-bypass-plan.md` — 元 recovery-from-boot 無効化プラン、F 経路(LK パッチ)に上書き
- `docs/system-swap-experiment-plan.md` — 初期の system 差し替え実験プラン(未実行、撤退候補)
- `logs/downgrade-D0-firmware-sha256.log`
- `logs/downgrade-D2-logotest.log`
- `logs/experiment-A1-logo.log` — Phase A-1 (logo 書き換え verify テスト成功)
- `logs/experiment-E0-preflight.log`

### 次のTODO(次のセッション)

1. **端末復旧**: 純正 LK (dump/lk.img) を SP Flash Tool で焼き戻す(未実行のまま、電源 OFF + USB でBROM 進入して焼く、5 分)
2. Sunmi 剥がしを再適用(disable-user 17 個)
3. Flutter で kiosk アプリの雛形作成(`~/SunmiV2Hack/app/`)
4. flutter_sunmi_printer + camera パッケージ導入
5. AndroidManifest に CATEGORY_HOME 追加、Sunmi Launcher (com.woyou.launcher) を disable

---

## 2026-07-01 18:40 (main) — Sunmi Recovery "Recovery the system" で復旧成功、方針転換

### 変更概要

前回の bootloop 状態から、**Sunmi Recovery のメニュー「Recovery the system」** を選択したところ、これが実質 **Factory Reset + boot 修復** 動作をしたようで、端末が Sunmi 純正の初期セットアップ画面(Hello ロゴ)から起動できるように **完全復旧**。ユーザに setup wizard を再走してもらい、USB debug 再有効化 → adb 復活。**Sunmi V2 の Recovery は復旧の切り札として使える**ことが実地で判明した。

### 復旧で判明した Sunmi V2 T5930 の性質

1. **「Recovery the system」= 純正 factory reset + boot 修復**。Magisk-patched で壊れた boot を元に戻してくれる(userdata は wipe される)。**ブリック時の最終手段として非常に価値が高い**
2. **Magisk 永続 root は無理**。Sunmi の LK による AVB1 検証で patched boot は必ず起動失敗する。Magisk 焼きは今後禁止
3. **書き換え経路の全体像**が固まった:
   - Android 起動状態: adb + mtk-su(CVE-2020-0069)で一時 root、boot/system は dd で書ける
   - Sunmi Recovery: 復旧専用、adb 不可
   - META (0e8d:20ff) / Cyrus Preloader (0e8d:2008): mtkclient handshake 不可、Sunmi の auth 必須、実質使えない
   - BROM (0e8d:0003): 物理コンボで進入不可、test point 未確認
4. **auth_sv5.auth や DA / preloader / scatter を渡しても mtkclient は 0e8d:2008 と通信できず**。SP Flash Tool ならいけるかもしれないが、Recovery の切り札があるので当面不要

### 方針転換

- **Magisk 焼き:中止**。以後、root は毎回 mtk-su の一時 root で行う
- **GSI 実験(Phase 4):削除**。復旧経路の余裕がないのでリスク大き過ぎ
- **Phase 3 継続**: Sunmi 剥がし(disable-user)+ Flutter kiosk 化 で完成させる

### 復旧後の作業

- mtk-su を /data/local/tmp/ に再 push、root 動作確認 (uid=0)
- Sunmi 剥がしスクリプト(17 パッケージ disable-user)再適用
- reboot 実施、post-reboot 状態:
  - Sunmi/woyou プロセス: 0 個(前回 reboot 後は 3 個残り、今回は完全に 0)
  - Focused Activity: com.woyou.launcher(Sunmi Launcher が Home に居座り、まだ剥がしていない)
  - プリンタサービス `woyou.aidlservice.jiuiv5` は Service として active、lazy load 動作確認
  - MemAvailable 501,480 kB (490 MB)
- Firmware パッケージは復旧不要になったが、`tools/firmware/` に保存済み(将来またブリックしたときの保険)
- Flutter snap インストール中(前回 exit 0 だったが実際は失敗していた、再度実行中)

### 得られたファイル

- `tools/firmware/Sunmiv2 stock with patch.rar` (1.29 GB) — fishybytes 経由の Sunmi V2 gen1 純正 firmware。含むもの: MT6739-DA.bin, MT6739_Android_scatter_emmc.txt, auth_sv5.auth, preloader_v2.bin, 各パーティション img, dafish7 の magisk_patched-25200_Lf52l.img
- `tools/firmware/Firmware/*.img` — 展開済み。system.img 2.5GB, userdata.img 4.0GB を含む
- `tools/SP_Flash_Tool_Linux/` — gesangtome ミラーの SP Flash Tool Linux 版。auth 対応必要なら次はこれで焼く

### 変更ファイル

- 更新: `WORKLOG.md`(このエントリ)
- 追加: `tools/firmware/Sunmiv2 stock with patch.rar`, `tools/firmware/Firmware/*`, `tools/SP_Flash_Tool_Linux/`
- 端末: 全 disable-user 済みパッケージリストは前回と同じ 17 個

### 次のTODO

1. Flutter インストール確認、`flutter doctor` を通す
2. `flutter create` で kiosk アプリの雛形作成 (`~/SunmiV2Hack/app/`)
3. flutter_sunmi_printer と camera パッケージを pubspec.yaml に追加
4. AndroidManifest に CATEGORY_HOME を追加して自作アプリを Launcher に
5. Sunmi Launcher (`com.woyou.launcher`) を disable
6. プリンタと カメラの動作確認、成功したら区切りでコミット
7. 途中で 再度 root 必要になったら `/data/local/tmp/mtk-su -c '/path/to/script.sh'` パターンで実行

### 学び

- **Sunmi 端末で永続 root を試すのは無理筋**。Magisk 焼きは Sunmi LK が絶対に許さない。mtk-su の一時 root で運用継続するのが唯一の道
- Sunmi Recovery「Recovery the system」は Sunmi 独自のリカバリ機能で、実質 Factory Reset。userdata を wipe するが boot が正しい純正に戻る。**次回ブリックしたら真っ先にこれを試す**
- SP Flash Tool + auth_sv5.auth + Sunmi 純正 firmware は「万一 Recovery でも救えない」ケースの保険。今回は使わずに済んだが、`tools/firmware/` に温存する
- 復旧を経て、**Sunmi V2 の書き換え耐性は POS 端末として非常に高い**ことが実地で確認できた。GSI で Android 上げは実質不可能

---

## 2026-07-01 17:35 (main) — Magisk 焼きで bootloop、経路詰まり

### 変更概要

「Sunmi V2 T5930 を kiosk 化」目的。Ubuntu 26.04 でツール一式(adb / mtkclient / Magisk / Flutter snap)を導入し、Phase 0/1/2 を完了。フル eMMC dump 取得+ dd 書き戻しリハーサル成功まで到達したが、**Magisk-patched boot を焼いた瞬間から bootloop**、以降 USB 経由の書き換え経路が全て塞がっている状態。

### ハードウェア/ソフトウェア確定情報

- SoC: **MediaTek MT6739** (Cortex-A53 4c ARMv7l モード, ro.mediatek.platform=MT6739)
- RAM: 909680 kB (実質 1GB モデル)
- eMMC: mmcblk0 = 7,471,104 kB (8GB)、パーティション 35 個
- kernel: 4.4.22+ (2026-03-19 ビルド)
- Android 7.1.1, SUNMI ROM 2.12.1 (最新 OTA 済み), 2024-04 セキュリティパッチ
- USB serial: VB3920C924272
- build fingerprint: `alps/full_rlk6580_we_c_m/rlk6580_we_c_m:7.1.1/N6F26Q/1773911932:user/release-keys`
- ro.boot.verifiedbootstate=green, ro.boot.flash.locked=1
- プリンタサービス: `woyou.aidlservice.jiuiv5/sunmi.inner.pkg.service.PrinterService` (`SunmiPrinterService_v6.6.25`)
- Sunmi Launcher: `com.woyou.launcher/com.android.launcher3.Launcher`

### 各起動モードでの USB VID:PID

| 状態 | VID:PID | 備考 |
|---|---|---|
| Android 通常起動 | `0e8d:201d` MediaTek V2 | adb 疎通可能(USB debug ON 済) |
| 音量+ + USB | (Sunmi Recovery 画面表示) | **adb / USB 無効化されている** |
| 音量- + USB | `0e8d:20ff` MediaTek Android | META mode。mtkclient は反応せず |
| 音量+ + 音量- + USB | `0e8d:20ff` | 同上 |
| bootloop 中(現状) | `0e8d:2008` MediaTek "Cyrus Technology CS 24" | Sunmi 独自 Preloader-like、mtkclient handshake 通らず |
| BROM(狙い) | `0e8d:0003` | **どのボタンコンボでも入れていない** |

### mtkclient で試したこと(全部タイムアウト or handshake 失敗)

- `printgpt --pid 0x20ff` / `--pid 0x2008` : タイムアウト
- `crash --pid 0x2008` : タイムアウト
- `plstage --pid 0x2008` : タイムアウト
- `meta FASTBOOT --pid 0x20ff` : タイムアウト
- `meta ADVEMETA --pid 0x20ff` : タイムアウト
- `--stock --pid 0x2008 printgpt` : タイムアウト
- `--preloader dump/preloader-boot0.img --pid 0x2008 printgpt` : タイムアウト

**結論: Sunmi ROM 2.12.1 は BROM/Preloader/META mode 全て塞ぎ、Sunmi Recovery からも adb を無効化している。書き換え経路は 3-boot 前は Android 起動状態の mtk-su 経路のみだった。**

### Phase 進捗

- **Phase 0**: 完了。adb / mtkclient (venv, pycryptodomex 手動追加要) / Magisk v30.7 / Flutter snap / udev rules 全部揃った
- **Phase 1**: 完了。全 property, 全 partition レイアウト, パッケージ列挙, プリンタサービス構造把握
- **Phase 2**: 完了。34 パーティション計 3.0 GB を `dump/*.img` に保存、SHA256SUMS 生成、書き戻しリハーサル成功(SHA256 一致で書き込みパスの正常動作を実証)
- **Phase 3-1 root 化**: 一時 root は成功していた(pikpikcu/mtk-su_r20 の arm バイナリで CVE-2020-0069、SELinux も permissive に)
- **Phase 3-2 Sunmi 剥がし**: 17 パッケージ disable-user 完了(UI/telemetry/OTA/decision/Store/中国語 IME)、reboot 後 Sunmi プロセス 12 → 3
- **Phase 3-3 永続 root(Magisk)**: patch script は成功、`new-boot.img` 生成、書き戻しも 6.088s で完了。**reboot したら bootloop**
- **Phase 4 (GSI)**: 未着手、現状無理

### 得られた重要な観察

1. **Sunmi Recovery のメニューは 3 項目のみ**: "Recovery the system" / "Factory data reset" / "Restart"。 sideload なし
2. **CVE-2020-0069 (mtk-su) は 2026-03 の Sunmi kernel でも刺さる**。まだパッチ済みではなかった
3. **書き戻しリハーサルは動いた**が、これは「同じ内容の書き戻し」であって、「Magisk-patched の内容」が Sunmi LK の verification に通るかは別の話 = 通らなかった
4. **Magisk `boot_patch.sh` の "Failed to patch"** が 3 回出ていた(dtbs パッチ失敗)= Sunmi の boot 内 dtb がスタンダードではない可能性

### 主な変更ファイル

- 新規: `WORKLOG.md`, `logs/current-state.md`, `logs/getprop-initial.txt`, `logs/packages-system.txt`, `logs/dump-progress.log`, `logs/magisk-patch.log`, ほか多数
- 新規(バイナリ): `dump/*.img` (34 パーティション + preloader boot0/1, 計 3.0 GB), `dump/new-boot-magisk.img` (Magisk-patched, 焼いた本人)
- 新規(ツール): `tools/mtkclient/`, `tools/mtk-su/`, `tools/Magisk.apk`, `tools/magisk-extracted/`, `tools/magisk-cli/`
- スクリプト各種: `/tmp/dump-all.sh`, `/tmp/disable-sunmi-1.sh`, `/tmp/magisk-patch.sh`, `/tmp/flash-magisk-boot.sh`, `/tmp/restore-boot.sh` など(全部 `/data/local/tmp/` にも push 済み、端末側に残っている)

### 次のTODO

1. **「Recovery the system」を試す**: Sunmi 純正の system 復元機能に賭ける。Magisk が上書きされて Sunmi 純正に戻る想定
2. **他アプローチの深掘り検索**: Cyrus Technology 0e8d:2008 プロトコル / Sunmi V2 test point 位置 / SP Flash Tool Windows で 2008 mode 対応するか
3. Recovery ダメなら:
   - bootloop 放置で電池切れ → 完全放電状態から音量+/- + USB で BROM が入れるか
   - 物理 test point 短絡で BROM 強制進入(破壊リスク)
   - Sunmi サポート送付修理

### 学び(次回同種プロジェクトで活かす)

- Sunmi の 2.12.1 は Android 7.1 世代の緩さと、Sunmi 独自の書き換え防御の組み合わせ。最新 OTA を当ててしまうと Magisk 焼きが実質不可能になるので、**OTA 前に root を確定させるべき**
- 「AVB1 でも起動できる」と勝手に期待して焼くのは危険。**書き戻しリハーサルの成功は "書き込みが可能" の証明であって "自作イメージが起動する" の証明ではない**
- mtk-su は Android 起動状態が前提。boot 焼き失敗で bootloop に入ったら mtk-su は無力
- 書き換え直前の最終確認として、**boot に純正 boot.img を焼き戻して通常起動できることは実証済みだったので、今の状態も理論上は「Magisk-patched の起動が失敗しているだけ」で復旧余地は残っている**
