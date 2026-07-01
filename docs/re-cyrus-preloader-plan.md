# Sunmi V2 T5930 実 OS Android 8+ 化: 経路 G(Cyrus USB プロトコル + Preloader RE)

## Context(なぜこの計画か)

**目的**: Sunmi V2 T5930(MT6739 / Android 7.1.1 / Sunmi ROM 2.12.1)を実 OS レベルで Android 8+ に上げる。**端末固定・分解禁止**を維持。

**なぜ「今もう一度」やるか**:

- 前回撤退(2026-07-01)時点で 5 段防御(BROM / preloader / LK / kernel / dm-verity)を実測で全て潰した
- 今日の追加調査で残り 3 経路も潰れた: kexec (CONFIG_KEXEC=n)、CVE-2026-20435 (seccfg LOCKED)、Droidspaces (namespace=mount only)
- **世界で 1 例も成功実績なし**(dafish7 / Lena / niko-forte / fishybytes の主要研究者 4 名全員 7.1.1 で足踏み、6 年間ゼロ)
- したがって「機種を変えない」条件下では **未踏の RE 経路を掘る以外に道は無い**

**残された唯一の理論経路**: **Cyrus USB プロトコル(0e8d:2008 = Sunmi 独自 Preloader-like モード)の RE で認証前メモリ書き込み primitive を発掘**

**成功時の連鎖**:
Cyrus RE で穴発見 → BROM 進入 primitive 獲得 → 前回すでに動作した SP Flash Tool + auth 経路と組み合わせて **preloader を含む全パーティション自由焼き** → K-touch i9 (MT6739 LineageOS 17.0 / Android 10) ベースの device tree 移植 → 実 OS Android 10 到達

**成功可能性**: 5-15%(mtkclient が取り込めていない = 難しい証拠、しかしゼロではない)  
**時間投資**: 数週間〜数ヶ月  
**復旧経路**: Sunmi Recovery "Recovery the system"(実証済み、ブリック時 20-30 分で 2.12.1 stock 復活)

**hobby プロジェクトの位置づけ**: 「動く POS kiosk」の実務目的なら投資対効果は極端に悪い。**RE 修行 + 世界初への挑戦**という研究動機で受け入れる。実務用 kiosk は別ラインで Flutter (Android 7.1) を進行させて構わない。

---

## 前提と手元の武器

### 手元にある RE 素材

- `dump/preloader-boot0.img` (4MB) — **本命 RE 対象**。Sunmi 純正 preloader
- `dump/lk.img` (1MB) — 前回 F 経路で解析済み、Policy Table Offset `0x6c340` 判明
- `dump/*.img` (34 partitions, 3.0GB) — 現行 stock full dump、書き戻しで復旧素材にできる
- `tools/firmware/Firmware/MT6739-DA.bin` (15MB) — Sunmi 対応 Download Agent
- `tools/firmware/auth_sv5.auth` (2.2KB) — Sunmi Preloader 認証ファイル
- `tools/firmware/Firmware/preloader_v2.bin` (112KB) — 2021 版 Preloader(比較用)

### 動作実績のある焼き経路

- SP Flash Tool v5.1916 + auth_sv5.auth + BROM 相当進入 = **動作実証済み**(D-2 で logo 焼き成功)
- ただし **BROM 進入は Sunmi 2.12.1 で物理ボタン全滅**、電源 OFF + USB 挿しで Cyrus (0e8d:2008) 落ちに変化
- G-6 で Cyrus RE 経由の BROM 進入 primitive を再構築するのが本命の突破点

### 復旧経路(常に生きている)

Sunmi Recovery "Recovery the system"(前回実証):

1. bootloop / 起動失敗時 → 電源長押し 20 秒で強制シャットダウン
2. 音量+ + 電源キーで Sunmi Recovery 起動
3. "Recovery the system" 選択 → factory reset + boot 純正復元
4. 初期セットアップ → USB debug ON → adb + mtk-su 再 push
5. 手戻り時間 20-30 分

---

## Phase G-0: 環境準備(非破壊、5-8 時間)

### 目的

RE ツールチェーンの整備と USB キャプチャ環境の構築。

### 手順

1. **Ghidra 導入**(前回 F-0 で未完了なら)
   - `sudo apt install ghidra` or 公式 zip
   - Java 21+ 確認
   - ARM v7 Little-Endian language pack 確認
2. **Wireshark + USBPcap**
   - Linux: `sudo apt install wireshark tshark`、usbmon module load
   - **Windows VM が必要**(SP Flash Tool for Windows でキャプチャ用) — VirtualBox / VMware Player / QEMU いずれか
   - USBPcap for Windows インストール
3. **SP Flash Tool Windows 版取得**(Linux 版は既に `tools/SP_Flash_Tool_Linux/` にあり、Windows 版は別途)
4. **静的解析補助ツール**
   - `binwalk` — preloader 内の埋め込み構造抽出
   - `radare2` / `Cutter` — Ghidra 補助
   - `capstone`(Python) — スクリプトによる disasm
   - `unicorn`(Python) — CPU エミュレーション(preloader 部分実行に有用)
5. **USB プロトコル解析補助**
   - `usbrply`(GitHub) — キャプチャした pcap を再生
   - `python-libusb1` — 自作 client の実装用
6. **mtkclient 最新化 + Sunmi 特化フォーク調査**
   - `pip install --upgrade mtkclient` or clone latest
   - Cyrus 0e8d:2008 対応 fork が 2025-2026 に出ていないか GitHub 検索

### 検証

- Ghidra で `dump/preloader-boot0.img` をロード可能
- Windows VM で SP Flash Tool 起動、USB pass-through 動作確認
- USBPcap でダミー USB 通信キャプチャ動作確認

### 中止判断 G-0

- Ghidra + Wireshark + Windows VM の 3 点セットが揃わない場合 → 中止、ハード面の投資を再考

---

## Phase G-1: Preloader バイナリの初期構造把握(1-2 日)

### 目的

`dump/preloader-boot0.img` (4MB) の全体構造、ロードアドレス、entry point、主要関数群の把握。

### 手順

1. **`file` + `binwalk` で構造確認**
   - MediaTek preloader は typically GFH + BROM header + Little Kernel-like structure
   - 4MB は通常より大きい(Sunmi 独自 payload の可能性)
2. **`strings` で識別子探索**
   - "Cyrus", "Sunmi", "MTK", バージョン文字列, USB descriptor, command handler 名
   - "PRELOADER", "DA", "auth", "SLA", "DAA"
3. **Ghidra ロード**
   - Language: ARM v7 Little-Endian
   - Base address: 手掛かり無ければ 0x0 or 0x201000(MTK preloader 典型値)から試す
   - GFH header offset を binwalk で特定した位置に合わせる
4. **重要文字列参照からエントリポイント特定**
   - "Preloader Start" 系メッセージから main() を辿る
5. **USB endpoint と handler table の位置特定**
   - `USB_Setup`, `usb_read`, `usb_write` 系のシンボル or ハンドラテーブル
   - MTK 標準 USB stack と Sunmi カスタマイズ部分の差分を早期に見つける

### 検証

- entry point 確定
- main loop、USB IRQ handler、コマンド dispatch 関数の 3 点特定
- コマンド ID テーブル(通常配列 or switch)の位置候補

### 記録

`logs/experiment-G1-preloader-structure.md` に:
- SHA256, サイズ, base addr 候補
- 抽出した strings 一覧
- 特定した関数の物理オフセット
- コマンド dispatch table の位置

### 中止判断 G-1

- preloader が暗号化 / 圧縮されていて disasm できない → 中止
- 独自 CPU モードや難読化が強すぎる → 中止

---

## Phase G-2: SP Flash Tool 通信の USB キャプチャ(1 日)

### 目的

正規の SP Flash Tool + auth_sv5.auth 経由の焼き通信を pcap で完全記録し、Cyrus プロトコルの生のパケット列を得る。

### 手順(改訂: Linux 単独で完結)

**Windows VM は不要**。`tools/SP_Flash_Tool_Linux/` は前回 D-2 で logo 焼き動作実績あり、Linux kernel の `usbmon` + tshark で kernel 層 USB キャプチャできる。Windows VM の準備コスト(数時間)を skip。

1. `sudo modprobe usbmon`
2. インターフェース確認: `ls /sys/kernel/debug/usb/usbmon/`(通常 `0u`, `1u`, ...)
3. 端末を電源 OFF → USB 挿しで Cyrus (0e8d:2008) mode に落とす、`lsusb` で確認
4. tshark で全 bus キャプチャ開始(root 必要):
   ```
   sudo tshark -i usbmon0 -w logs/experiment-G2-cyrus-logo.pcapng
   ```
   → 0e8d:2008 を含む bus が別番号なら usbmon1/2/... に切替
5. 別 window で SP Flash Tool GUI 起動 → 前回 D-2 と同じ **logo 部分焼き**を実行
6. tshark を Ctrl-C 停止、pcapng 保存
7. 追加パターンで別 pcap も取得:
   - `logs/experiment-G2-cyrus-download-only.pcapng`(auth 無しで download 試行 → 拒否ログ含む)
   - `logs/experiment-G2-cyrus-handshake.pcapng`(接続だけして即切断、handshake だけ抽出)
8. tshark で post 解析:
   ```
   tshark -r logs/*.pcapng -Y 'usb.data_len>0' -T fields -e frame.number -e usb.data_fragment
   ```

### 検証

- pcap ファイルに 0e8d:2008 のパケットが完全記録されている
- 認証 handshake、DA 転送、コマンド送信、レスポンスの区別が付く
- G-1 で特定したコマンド ID `0xD4/D5/D7/DB` のバイトが pcap 中に見える

### 記録

- `logs/experiment-G2-cyrus-logo.pcapng` — 正規手順の完全記録
- `logs/experiment-G2-cyrus-download-only.pcapng` — auth 無し試行
- `logs/experiment-G2-cyrus-handshake.pcapng` — 接続 handshake のみ
- `logs/experiment-G2-notes.md` — 手順・タイムスタンプ・観察

### 中止判断 G-2

- Linux 版 SP Flash Tool が新 preloader (0e8d:2008) で不具合 → Fallback で Windows VM + Windows 版 SP Flash Tool + USBPcap に切替(数時間の準備追加)
- usbmon が端末を認識しない → udev rule 見直し(前回 mtkclient 用に整備済)

### G-2 kickoff チェックリスト(次セッション先頭で確認)

- [ ] `lsmod | grep usbmon` で usbmon が使えるか、無ければ `sudo modprobe usbmon`
- [ ] 端末バッテリー 90% 以上、AC 接続
- [ ] `tools/SP_Flash_Tool_Linux/flash_tool` が起動する(前回動作確認済み)
- [ ] `tools/firmware/auth_sv5.auth` 存在確認
- [ ] `tools/firmware/Firmware/MT6739_Android_scatter_emmc.txt` 存在確認
- [ ] Sunmi Recovery 復旧手順を再確認(音量+ + 電源)
- [ ] 前回の D-2 logo 焼き手順を頭に入れておく(WORKLOG 2026-07-01 21:30 参照)

---

## Phase G-3: Cyrus プロトコル解読(1-3 週間 ★最も時間かかる)

### 目的

G-2 の pcap と G-1 の preloader RE を突き合わせて、**Cyrus プロトコルの全コマンド仕様書**を作る。

### 手順

1. **pcap をコマンド境界で分割**
   - 通常 MTK は 2 バイト or 4 バイトコマンド ID + パラメタ
   - Sunmi 独自変換(XOR / スクランブル)がかかっているかを確認
2. **preloader の USB handler の switch/table を辿って各コマンド ID の処理を Ghidra で確認**
3. **コマンド分類**
   - 認証系(SLA / DAA)
   - メモリ操作(read/write/exec)
   - パーティション操作(read/write partition, format)
   - 制御(reset, reboot, get info)
4. **認証ステートマシン特定**
   - どの state で auth が要求されるか
   - **auth 前に呼べるコマンド一覧**(ここが本命)
5. コマンド仕様書 `logs/experiment-G3-cyrus-protocol.md` を作成

### 検証

- 主要コマンド 20-30 個の仕様が判明
- 認証ステート遷移図が書ける
- **auth 前に呼べる無認証コマンド群のリスト**が完成

### 中止判断 G-3

- プロトコルが強い暗号化で保護されていて解読不能 → 中止
- 認証前に呼べるコマンドが極めて少なく、read only 系のみ → **G-4 の窓が無くなる可能性、要判断**

---

## Phase G-4: 無認証コマンドの脆弱性探索(1-2 週間)

### 目的

G-3 で得た「auth 前に呼べるコマンド」の中から、**バッファ長チェック漏れ / 境界チェック不備 / 未初期化ポインタ経由の書き込み** など、**認証前に任意メモリ書き込みできる bug** を発見する。

### 手順

1. Ghidra で各無認証コマンドハンドラを 1 個ずつ精査
2. パラメタ長がホスト側指定で、preloader 側で clamp していない箇所
3. buffer overflow: stack based (return address 上書き)/ heap based
4. **fuzzing**: unicorn engine で preloader を部分エミュレート、無認証コマンドに perturbed 入力を投げて crash 探し
5. mtkclient の過去 exploits を教材にする(Kamakiri: USB DMA 経由の Read/Write プリミティブ)
   - 参考: [bkerler/mtkclient exploits ディレクトリ](https://github.com/bkerler/mtkclient)
   - 参考: [Kamakiri original writeup](https://forum.xda-developers.com/t/wip-kamakiri-a-brom-based-attack-for-mediatek-devices.4160199/)

### 検証

- 少なくとも 1 個の任意メモリ書き込み primitive 発見
- 書き込み先アドレス、書き込めるバイト数、call constraints の判明

### 中止判断 G-4

- 全ての無認証コマンドが正しく実装されている(bug 無し) → **プロジェクト詰み、G-9 の撤退フェーズへ**
- fuzzing で 2 週間かけて crash 見つからず → 中止(専門 fuzzing チーム規模の投資が必要)

### この Phase で成否が決まる

**G-4 でバグ発見できなければ G-5 以降は成立しない**。全プロジェクトのボトルネック。ここに 2 週間投資して見つからなければ潔く撤退する。

---

## Phase G-5: PoC exploit の実装(3-7 日)

### 目的

G-4 のバグを使う PoC exploit を Python + libusb1 で書き、実機で任意メモリ書き込みができることを実証。

### 手順

1. Python client で 0e8d:2008 に接続
2. 発見した vulnerable コマンドを送信、控えめな書き込み(NULL 上書き等の無害な場所)から開始
3. crash が予測通り起きるか、write が期待通りに反映されているか、**メモリ read プリミティブ**とペアで検証
4. 書き込み位置と大きさを検証、reliable な primitive に育てる
5. `scratch/g5-poc-writeprim.py` に固める

### 検証

- 任意アドレスに任意バイト数書き込める(制約含めて)
- 100% 再現、5 連続実行で全部成功
- **BROM への jump に使える** or **preloader 内 verify 関数を無効化できる** ことを示す

### 中止判断 G-5

- 実機で primitive が動かない(Ghidra 上の理解が誤り) → G-4 に戻る or 中止

---

## Phase G-6: BROM 進入経路の再構築(1-2 週間)

### 目的

G-5 の write primitive を使って、**BROM (0e8d:0003) に降ろす** or **preloader 内の signature verify 関数を無効化する** ペイロードを実装。

### 選択肢

- **A**: preloader を crash させて BROM fallback を発動(Kamakiri 系の手法)
- **B**: preloader の LK signature verify 関数を write primitive で NOP 化 → 前回 F 経路のパッチ LK が起動できるように
- **C**: preloader を bootstrap して新しい DA を注入、そこから焼き

### 手順

1. G-5 の primitive で選択肢 A/B/C のどれが現実的か判定
2. 最有力(推定: B が最短経路、preloader verify 無効化のみで前回 F 経路が復活する)を実装
3. 実機で発動、`lsusb` で 0e8d:0003 (BROM) が見える or preloader が verify 拒否をやめるまで検証
4. 発動後の状態が **stable な書き込みモード**として使えることを実証(次のフェーズで焼きに使う)

### 検証

- 選択肢 B なら: G-5 の primitive で verify 関数を無効化した状態を `getvar` 系で確認
- 選択肢 A なら: lsusb で 0e8d:0003 が現れる、mtkclient が handshake できる

### 中止判断 G-6

- どの選択肢も primitive の制約(書き込みサイズ、アドレス範囲)で成立しない → 中止、G-4 で別 bug 探し or 撤退

---

## Phase G-7: preloader signature verify の永続無効化(F 経路の復活 + 拡張、1-3 日)

### 目的

G-6 で得た state を活かして、**preloader そのものを触らずに** LK signature verify を無効化した状態を焼く。

### 手順

1. G-6 が選択肢 B(runtime verify 無効化)の場合、この状態でパッチ LK を焼く
2. G-6 が選択肢 A(BROM 進入)の場合、mtkclient + BROM 経由で **前回 F-4 のパッチ LK** を再焼き
3. 選択肢 C(DA 注入)の場合、任意 partition を DAA なしで焼ける状態を利用
4. LK を焼いた後、reboot して 前回 F-5 と同じ確認

### 検証

- パッチ LK で通常起動できる(前回 F-5 が bootloop だったのが、今回起動する)
- Magisk-patched boot を焼いても bootloop しない = AVB1 verify が LK パッチで無効化されている確認

### 中止判断 G-7

- パッチ LK 焼き後もまだ起動しない → preloader が起動時 verify を別経路でやっている可能性、G-6 のペイロードを見直し

---

## Phase G-8: K-touch i9 device tree ベースの Android 10 port(1-3 ヶ月)

### 目的

G-7 で LK/boot の署名検証が無効化された状態を前提に、**Android 10 の実 OS を Sunmi V2 上で動かす**。

### 手順

1. K-touch i9 LineageOS 17.0 (MT6739 / Android 10) の device tree を clone
   - 参考: [android_device_tcl_mt6739-common](https://github.com/topics/mt6739)
   - 参考: [XDA スレッド](https://xdaforums.com/t/rom-unofficial-10-lineageos-17-0-for-k-touch-i9-mini-phone-anica-i9-mt6739.3992285/)
2. Sunmi V2 T5930 の partition layout に合わせて device tree を fork(mt6739-sunmi-v2)
3. Sunmi 固有ハードウェア(プリンタ、カメラ、タッチ)の HAL 移植
   - **プリンタ**: `woyou.aidlservice.jiuiv5` は Android 7.1 の AIDL、Android 10 の Binder 互換性検証必要
   - **カメラ**: MTK HAL の使い回し可能性
4. kernel は K-touch i9 の 4.9 系を Sunmi V2 の DTB に合わせて再ビルド
5. GSI or LineageOS system.img を焼き
6. Sunmi プリンタサービスを chroot 経由 or Android 10 用 port で動かす

### 検証

- 端末が Android 10 で起動、`getprop ro.build.version.release` が "10"
- adb 疎通、開発者オプション認識
- カメラ、タッチ、プリンタが動作(不動なら段階的に port を進める)

### 中止判断 G-8

- kernel port の壁が大きすぎる(数ヶ月投資しても boot まで行かない) → **G-7 まで到達している段階で「Android 7.1 + LK パッチで永続 Magisk root」を成果として確定させて撤退**
- Sunmi プリンタが Android 10 で動かない → 目的の POS kiosk 実装は不可能、研究成果として記録

---

## Phase G-9: 撤退・成果確定手順(常に用意)

### 目的

各 Phase で行き詰まった場合の秩序ある撤退と、そこまでの成果の保存。

### 撤退シナリオ

| どこで詰んだか | 撤退時の残る成果 |
|---|---|
| G-1 で preloader 難読化 | preloader 解析メモ、参考価値のみ |
| G-3 でプロトコル解読失敗 | Cyrus プロトコル部分解析、公開すればコミュニティ資産 |
| G-4 でバグ発見できず | fuzzing 結果、無認証コマンド仕様書、公開価値あり |
| G-5 で PoC 動かず | Ghidra 上の解析、次回リサーチの起点 |
| G-6 で BROM 進入 primitive 得られず | preloader 内 verify 関数のパッチ候補特定は残る |
| G-7 で LK パッチ焼き失敗 | Android 7.1 + F 経路成果は既存 |
| G-8 で Android 10 port 詰む | **Android 7.1 + 永続 Magisk + dm-verity 無効化**の状態は保持 = 実用的な成果 |

### 撤退プロトコル

1. 現状スナップショット取得(getprop, mounts, adb 疎通確認)
2. 復旧経路実施: Sunmi Recovery "Recovery the system" or SP Flash Tool + dump/lk.img の書き戻し
3. WORKLOG.md に到達点を記録(何が判って何が壁だったか)
4. 公開可能な RE 成果は Sunmi V2 の hobbyist コミュニティに還元(GitHub / XDA)

---

## 成功可能性の再確認(Phase 別)

| Phase | 成功率(単独) | 累積成功率 |
|---|---|---|
| G-0 環境準備 | 95% | 95% |
| G-1 preloader 構造 | 70% | 67% |
| G-2 USB キャプチャ | 90% | 60% |
| G-3 プロトコル解読 | 50% | 30% |
| G-4 脆弱性発見 | 20-30% | 6-9% |
| G-5 PoC 実装 | 80% | 5-7% |
| G-6 BROM 進入 | 70% | 3.5-5% |
| G-7 LK パッチ焼き | 90% | 3-4.5% |
| G-8 Android 10 port | 30-50% | 1-2% |

**最終ゴール(実 OS Android 10)到達率: 1-2%**

**中間ゴール(G-7 = 永続 Magisk root + dm-verity 無効化)到達率: 3-4.5%**

正直、極めて低い。ただし G-1〜G-4 の途中成果でも Sunmi V2 hobbyist コミュニティ初の成果になる。**研究プロジェクトとして受け入れる前提**の投資。

---

## Critical files to modify(実際に触るもの)

- `dump/preloader-boot0.img` → Ghidra 解析対象、ファイル自体は変更しない
- `scratch/g4-fuzzing/` (新設) — unicorn fuzzing 環境
- `scratch/g5-poc-writeprim.py` (新設) — PoC exploit
- `scratch/g6-brom-entry.py` (新設) — BROM 進入 payload
- `tools/firmware/Firmware/lk.bin` → G-7 で patched LK に置き換え(前回 F 経路の再利用)
- `logs/experiment-G0-tools.log` 以降各 Phase のログ
- `WORKLOG.md` — 各 Phase 完了時にエントリ追加

## 参考リンク(Sources)

- [bkerler/mtkclient — MTK BROM/preloader exploits の教科書](https://github.com/bkerler/mtkclient)
- [MTK-bypass/bypass_utility — 未対応 SoC 追加のフレームワーク](https://github.com/MTK-bypass/bypass_utility)
- [Kamakiri writeup (XDA)](https://forum.xda-developers.com/t/wip-kamakiri-a-brom-based-attack-for-mediatek-devices.4160199/)
- [XDA MediaTek MTK Auth Bypass SLA/DAA utility](https://xdaforums.com/t/mod-dev-mediatek-mtk-auth-bypass-sla-daa-utility.4232377/)
- [K-touch i9 (MT6739) LineageOS 17.0](https://xdaforums.com/t/rom-unofficial-10-lineageos-17-0-for-k-touch-i9-mini-phone-anica-i9-mt6739.3992285/)
- [dafish7/Sunmi-v2-Firmware](https://github.com/dafish7/Sunmi-v2-Firmware-)
- [Lena's SUNMI V2 reverse engineering blog](https://lena.nihil.gay/blog/sunmi-v2-reverse-engineering)
- [HackTricks — MediaTek Secure Boot BL2 EXT Bypass EL3](https://hacktricks.wiki/en/hardware-physical-access/firmware-analysis/android-mediatek-secure-boot-bl2_ext-bypass-el3.html)
- [Quarkslab — CVE-2020-0069 MediaTek rootkit autopsy](https://blog.quarkslab.com/cve-2020-0069-autopsy-of-the-most-stable-mediatek-rootkit.html)

## Verification(全体テストプラン)

**中間検証(G-7 到達時)**:
1. `adb shell 'su -c id'` → uid=0(永続 root)
2. reboot 後も root 維持
3. `mount | grep verity` → verity 無効化確認
4. Magisk app が「Installed」表示、reboot でも維持

**最終検証(G-8 到達時)**:
1. `getprop ro.build.version.release` → "10"
2. `getprop ro.build.version.sdk` → "29" or higher
3. `uname -a` → kernel version が K-touch i9 由来の 4.9 系
4. プリンタサービス `woyou.aidlservice.jiuiv5` or 代替が動作
5. カメラ preview 表示
6. Sunmi Recovery が壊れていないか(万一のバックドア)

**復旧テスト(各 Phase 開始前)**:
1. Sunmi Recovery が起動できる(音量+ + 電源)
2. "Recovery the system" が動作する
3. USB 挿しで Cyrus (0e8d:2008) が見える(SP Flash Tool ルートの生存確認)

---

## 前提チェックリスト(G-0 開始前)

- [ ] 端末バッテリー 90% 以上、AC 常時接続
- [ ] `dump/*.img` の SHA256SUMS 検証済み
- [ ] 現状 mtk-su で uid=0 取れる
- [ ] Sunmi Recovery 進入手順を身体で覚えている
- [ ] Ghidra + Java 21+ + Wireshark + Windows VM の 4 点セット準備完了
- [ ] 数週間〜数ヶ月の hobby 投資を受容する意思確認
- [ ] 撤退時の kiosk 実装は別ラインで進める(Flutter Android 7.1)、この計画は独立