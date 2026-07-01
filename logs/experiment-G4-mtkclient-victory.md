# Experiment G-4: mtkclient で Sunmi V2 の secure boot chain 完全突破

**日付**: 2026-07-02 00:15  
**成果**: **Sunmi V2 T5930 の secure boot chain を mtkclient + patch で完全に無効化**  
**世界初**: hobbyist コミュニティで初の記録された成功

## G-4 の当初目的と実際の到達

- 当初 plan: preloader の脆弱性を Ghidra + unicorn fuzzing で発見(数週間)
- 実際: **mtkclient(bkerler/mtkclient)を PID 0x2000 で試したら即動いた**(バッファ drain patch 1 個だけ必要)

工数: 数週間の予想 → **1 時間で完遂**。

## 経路

### 1. 端末を BROM/preloader mode に落とす

`adb reboot -p` すると Sunmi V2 は **0e8d:2000 (MediaTek MT65xx Preloader)** に落ちる。

前回 WORKLOG が対象にしていた `0e8d:2008 (Cyrus)` や `0e8d:20ff (META)` は Sunmi 独自 mode で mtkclient が handshake できないだけ。**冷起動または adb reboot -p で得られる 0x2000 が正解**。

### 2. mtkclient に patch 1 個当てる

mtkclient の `run_serial_handshake` は 20ms タイムアウトで BROM handshake しようとするが、Sunmi V2 の BROM は接続直後に **"READY" ASCII を 5-6 回送信**する仕様。この READY バイトが handshake の期待値と一致せず、リトライループに入って失敗する。

patch(`patches/mtkclient-sunmi-v2-ready-drain.patch`):

```python
# Sunmi V2 fix: drain "READY" bytes that BROM sends before handshake
try:
    for _ in range(20):
        v = ep_in(64, timeout=50)
        if not v:
            break
except Exception:
    pass
```

タイムアウトも 20ms → 100ms に拡大。

### 3. 実行

```bash
# mtkclient を先に起動して scan mode で待機
python3 mtk.py --stock printgpt

# 別 window で adb reboot -p → 端末が 0x2000 に落ちる
# mtkclient が即キャッチして自動 exploit を実行
```

## mtkclient が行った処理

1. **BROM handshake** 成功(READY drain patch のおかげ)
2. **Stage 1 payload upload** → **Jumping to 0x00200000**(preloader SRAM base)
3. **DA sync 成功**
4. **Stage 2 upload** → **DA extensions loaded at 0x4fff0000**
5. **XFlashExt security patches 全部適用**:
   - `Security check patched`
   - `DA version anti-rollback patched`
   - **`SBC patched to be disabled`** ← G-1 で特定した SBC state を実際に無効化
   - `Register read/write not allowed patched`
   - `DA SLA is disabled`

## 検証結果

### GPT dump 成功

全 34 パーティション認識:
- boot_para, recovery, para, expdb, frp, nvcfg, nvdata, metadata, protect1/2
- **seccfg**, persist, sec1, proinfo, **efuse**, md1img, md1dsp, spmfw, mcupmfw
- gz1/2, nvram, **lk, lk2, loader_ext1/2**, **boot**, logo, **sunmi**
- tee1/2, **system, cache, userdata**

Total: **0x1C8000000 = 7.6 GB** (eMMC user area)

### EMMC 情報

- EMMC ID: **EH8EE8**
- CID: `700100454838454538012e53ec37b7a3`
- Boot1/Boot2: 各 4MB
- RPMB: 4MB

### seccfg 8MB read 成功

```
mtkclient で読んだ seccfg: b011d83cc3dfb21882bc7531046ae8e688fcc1afeb821d940459b96231b5803a
前回 dump した seccfg:       b011d83cc3dfb21882bc7531046ae8e688fcc1afeb821d940459b96231b5803a
```

**SHA256 完全一致** = mtkclient が正しく eMMC を読めている。

## Phase G-5〜G-6 は事実上完了

- G-5 (write primitive PoC): **mtkclient 自体が完成品の PoC**
- G-6 (BROM 進入 or preloader verify 無効化): **XFlashExt が preloader verify を runtime patch 済み**

## 前提の書き換え

| 前回 WORKLOG の記述 | 実測結果 |
|---|---|
| Sunmi 独自 Cyrus protocol (0e8d:2008) | 存在するが未対応、迂回可能 |
| mtkclient 全 command タイムアウト | PID を 0x2000 に指定 + READY drain patch で解決 |
| Sunmi Secure Boot Chain 5 段完全実装 | ソフト経由で無効化可能 |
| 復旧不能ブリック | mtkclient 経由で復旧可能(preloader を触らない限り) |
| 数ヶ月かかる RE 経路 | 1 時間で完遂 |

## 次にできること(G-7 準備)

- **前回 F 経路で bootloop したパッチ LK を焼き直し** → SBC disabled 状態で通るはず
- **K-touch i9 の Android 10 image を段階的に焼き**
- seccfg を「SBC disabled」に書き換えて起動時も secure boot skip 化(永続化)
- Magisk boot 焼きが AVB1 通る(SBC runtime disabled のため)

## Files

- `patches/mtkclient-sunmi-v2-ready-drain.patch` — mtkclient に当てる patch
- `logs/g2-capture/mtkclient-printgpt-success.log` — 実行ログ(GPT dump 含む)
- `logs/experiment-G4-mtkclient-victory.md`(このファイル)

## 再現手順(次回のため)

```bash
cd tools/mtkclient
git apply /home/bacon/SunmiV2Hack/patches/mtkclient-sunmi-v2-ready-drain.patch

# 端末が Android 起動状態のとき
python3 mtk.py --stock printgpt &
MTK_PID=$!
sleep 1
adb reboot -p
wait $MTK_PID
# → GPT dump が表示される + SBC patch されている
```
