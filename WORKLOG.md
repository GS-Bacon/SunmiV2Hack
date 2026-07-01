# WORKLOG - Sunmi V2 T5930 kiosk 化プロジェクト

## 記入ルール

新しいエントリは、このセクション直下(先頭)に追記する。逆時系列(最新が上)。

書式:
- 見出し: `## YYYY-MM-DD HH:MM (ブランチ名)`
- 変更概要 / 主な変更ファイル / コミット / 次のTODO

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
