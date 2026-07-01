# Experiment G-2: Sunmi V2 SPFT logo 焼き USB プロトコルキャプチャ

**日付**: 2026-07-01 23:38-23:48  
**pcap**: `logs/g2-capture/cyrus-logo-flash.pcapng` (10MB, 11314 packets)  
**方式**: Linux `usbmon3` + `dumpcap` → SPFT v5.1916 CLI で logo 焼き実行

## 手順

1. `sudo modprobe usbmon` + `sudo setcap` で dumpcap 権限整備
2. `sudo chmod 666 /dev/usbmon*` で bacon から書き込み可能に
3. `sudo dumpcap -i usbmon3 -w /tmp/cyrus-logo-flash.pcapng` バックグラウンド開始
4. 端末電源長押し 15 秒で shutdown、USB 抜く
5. 端末に USB 挿し直し → 最初は 0e8d:20ff (META) 表示
6. SPFT v5.1916 CLI 起動(60s USB scan)
7. Scan 中に USB 抜き差し → 端末が **0e8d:2000 (Preloader/BROM mode)** で enumerate
8. SPFT が BROM connect → DA transfer → logo write @ 16.29 MB/s → Download Succeeded

## デバイス enumeration

キャプチャで見えた PID 遷移:
- 最初(scan 前): `0e8d:20ff` MediaTek Android (META mode)
- USB replug 後: `0e8d:2000` MediaTek Preloader — **SPFT がこれを BROM として扱う**

前回 WORKLOG では以下 PID をトラブル対象にしていた:
- `0e8d:2008` "Cyrus Technology CS 24" — **今回一切現れず、単なる誤解だった可能性**
- `0e8d:20ff` META mode — SPFT からは通信不可

**結論**: 正しい PID は **0x2000**。前回の mtkclient 試行(0x20ff / 0x2008)は間違いだった。

## ★★★ 決定的発見: auth_sv5.auth は送信されない

pcap 全域を `auth_sv5.auth` の最初 16 バイト (`4d4d4d013800000046494c455f494e46`) で検索 → **0 hit**。

- SPFT は auth ファイルを config で参照するが、実際の USB 通信では送っていない
- BROM 側からの challenge 要求も無い
- → **Sunmi V2 の BROM は SLA (Serial Link Authentication) を実装していない**

**攻撃面が根本的に変わる**:
- 誰でも `power off + USB` → 標準 MTK BROM handshake で通信可能
- 任意の DA (Download Agent) を送って実行可能
- 唯一の壁: DA レベルの image signature verify(それは DA を patch すれば bypass できる)

これは mtkclient がそのまま動く**ハズだった**。前回タイムアウトしたのは PID を間違っていたため。

## プロトコル完全解読

トラフィック量:
- OUT (host→dev): 8.6 MB total(大半は logo.bin 8MB + DA 120KB)
- IN (dev→host): 51 KB total

### Phase 1: BROM Handshake (proto#6-14, frames 5276-5294)

```
host → dev: a0    dev → host: 5f  (~a0)
host → dev: 0a    dev → host: f5  (~0a)
host → dev: 50    dev → host: af  (~50)
host → dev: 05    dev → host: fa  (~05)
```

**100% 標準 MTK BROM handshake**。修正されていない。

### Phase 2: Chip Info Query (proto#16-26)

- `fe` → status byte
- `fd` → **hwcode `0x0699` = MT6739**
- `fc` → sw version `8a 00 cb 00 00 02 00 00`

### Phase 3: eFuse Status Read (proto#28-70, `d1` command 3回)

- `d1` = READ32 command
- addr = `0x11c00250` (**eFuse controller register**)
- 返り値: `0x00000001`
- 3 回繰り返し(polling)

前回 G-1 で見つけた `0x11c00060` bit 1 とは**別の eFuse register**。secure boot status ないし fuse readable 系。

### Phase 4: SEND_DA (proto#75-98, `d7` command, 8192B x 14 chunks)

- `d7` = SEND_DA
- target addr: `0x00200000` (SRAM base、前回 G-1 で判明した preloader load 位置と同じ)
- DA size: `0x0001dc58` = 119 KB
- sig len: `0x00000100` = 256 bytes
- ペイロード: MediaTek 公式 signed DA (MT6739-DA.bin 由来)

DA 内には以下の文字列を確認:
- `"GNED(count)"`
- `"[PMIC]"`
- `"[EMI] PCDDR3 RXTDN Calibration"`
- `"DRAM type is n"`
- `"lock is enabled"`

### Phase 5: JUMP_DA (proto#101-103, `d5` command)

- `d5` = JUMP_DA
- jump addr: `0x00200000`
- 実行開始

### Phase 6: DA protocol (proto#107+, magic `0xEFEEEEFE`)

DA 内部プロトコル:
```
[magic 4B: efeeeefe] [version 4B: 01000000] [payload_len 4B] [payload]
```

例:
- 初回: `efeeeefe 01000000 04000000` + `53594e43` = magic + ver=1 + len=4 + **"SYNC"**
- SYNC 応答パターン多数

### Phase 7: logo write (proto#437+, ~839 chunks × 10240 bytes = 8.4 MB)

DA が受信した logo データを eMMC の logo パーティション(0x22 番)に書き込み。

## 攻撃面の書き換え

前回 G-1 で立てた「preloader RE で SBC state 変数書き換え」経路は、実は**大幅に短絡できる**:

### 新経路(超短縮)

1. **端末を電源 OFF + USB 挿し** → 0e8d:2000 に落ちる
2. **mtkclient を `--pid 0x2000` で起動** → 標準 BROM 通信成立
3. **DA を patch した版に置き換えて送信** → image signature verify を無効化した DA が起動
4. **patched DA で任意 image 焼き** → LK/boot/system 自由書き換え
5. 起動 → 実 OS Android 10 到達

### 前提の書き換え

| 項目 | 前回想定 | 実測後 |
|---|---|---|
| BROM auth 必要か | 必要(auth_sv5.auth 送信) | **不要**(送信されていない) |
| Sunmi 独自 Cyrus protocol | 存在(0e8d:2008 = mtkclient 非対応) | **不存在**(0e8d:2000 標準 preloader) |
| G-1 の preloader SBC state 書き換え | 唯一の道 | **他の道もある**(DA patch でも同等) |
| 攻撃工数 | 数ヶ月 | **数日〜数週間**(mtkclient + DA patch) |

## Files

- `logs/g2-capture/cyrus-logo-flash.pcapng` — 10MB pcap 生キャプチャ
- `logs/g2-capture/protocol-trace-full.txt` — Python 解析出力
- `logs/g2-capture/proto-phases.txt` — フェーズ毎のコマンド列
- `logs/experiment-G2-cyrus-capture.md`(このファイル)

## 次のステップ

G-4 は方針変更:
1. **mtkclient を `--pid 0x2000` で試す** — 手元の bkerler/mtkclient で BROM 制御可能か
2. 動けば mtkclient の DA patcher (`--patchda`) や rpmb / seccfg 系操作を試す
3. K-touch i9 の Android 10 の image を焼く準備
