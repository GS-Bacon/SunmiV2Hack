# G-11 LK ghidra grep: Track G v2 で bypass できていない verify を探した

**Date**: 2026-07-02 (継続 session)
**Motivation**: G-10 Phase 3 で Magisk-patched boot がなお bootloop。WORKLOG 記載の 4 候補仮説のうち "LK 内に Track G v2 で touch していない別 verify がある" (候補 1/2) を先に確認、brick リスクゼロで進む。
**Env**: Ghidra 11.2.1 headless + r2 4.x + Python 3

## 手法

1. `lk-payload.bin` (base 0x56000000) に含まれる **boot verify 関連 string** を全て列挙(`ANDROID!`, `boot cert vfy`, `verified boot signature`, `image hash verification`, `img cert-chain`, `md check header verify`, `SECLIB_IMG_VERIFY` 等 30+)
2. 各 string の VA を計算し、r2 `axt` で xref を列挙
3. 参照元 function を Ghidra headless で decompile
4. Track G v2 の gate (`FUN_5601a7e0` / `FUN_5601a7ec`) を通っているか、通っていないか判定
5. gate を通らない verify を "smoking gun" 候補として洗い出す

## 発見した boot verify 関連 function 一覧

| Function | 役割 | Gate | Track G v2 の effect |
|---|---|---|---|
| `FUN_56038388` (cert_chain_verify_orchestrator, 224B) | 汎用 partition cert-chain verify | `FUN_5601a7e0` 経由 | ✅ SKIP される。SKIP 内の追加 check は **md1rom のみ**(boot は該当しない) |
| `FUN_560384a4` (image_hash_verify_orchestrator, 284B) | 汎用 partition image-hash verify | 独立フラグ `*(DAT_560385c4+...) == 0` return + 内部 strncmp で **md1rom / md3rom のみ**対象 | ✅ boot は strncmp を全通過してそのまま `return`(halt しない) |
| `FUN_56039f40` (verified_boot_signature_parser, 836B) | AVB 相当の boot signature parse | 呼出元が `FUN_5603a54c` のみ、更にそれは `FUN_56038388` の NON-SKIP 経路のみ | ✅ Track G v2 で SKIP、この経路には到達しない |
| `FUN_56039618` (mcupmfw_cert_vfy, 388B) | MCU/MCUPMFW firmware auth | `FUN_5601a7e0` 経由 | ✅ SKIP される。SMC call `0x82000228` (MTK MCUPMFW load) だけ実行 |
| `FUN_56003158` / `FUN_56003438` (bootimg_loader_A/B, 610/542B) | boot.img header parse + kernel/ramdisk 読出 | **無し**、`ANDROID!` magic の strncmp のみ | ✅ 影響なし、magic 一致さえすれば pass |
| `FUN_56002d58` (bootstate_setter, 116B) | verified-boot state 設定(GREEN/YELLOW/ORANGE) | `FUN_5601a7e0` 経由 | ✅ SKIP、しかも state 3 (ORANGE) にしても **halt しない**、ただ warning 表示だけ |
| `FUN_56002dd8` (post_load_verify, 176B) | seccfg 読 → cert/hash verify → state 設定 | seccfg locked のみ、更に `FUN_5601a7e0` 経由 | ✅ SKIP + どのみち halt しない、state だけ |
| `FUN_56038618` (partition_load_master, 382B) | 汎用 partition load and verify master | `param_5 & 1` で cert+hash 両 SKIP 可、SKIP 無しでも上記 2 個の gate 参照 | 主に modem 系(FUN_56038b28 → FUN_56038da8 経由)、boot.img は経路が違う |
| `FUN_5603ff40` (md1rom_pubkey_hash_setter, 300B) | md1rom modem 公開鍵 hash セット | `FUN_56038388` SKIP 分岐から md1rom 判定時のみ | boot 無関係 |

## Fact Matrix (LK Ghidra RE ベース)

| 判定 | 結果 |
|---|---|
| LK には boot.img を halt させる verify path が(トレース範囲で)ある | ❌ 無い |
| Track G v2 は既知の cert/hash gate を **全て** SKIP している | ✅ |
| Track G v2 で SKIP されなかった `FUN_560384a4` の追加 verify は modem 専用 | ✅(md1rom, md3rom のみ) |
| `FUN_56002d58` / `FUN_56002dd8` は verify 失敗しても halt せず ORANGE state だけ設定 | ✅(warning UI のみ) |
| bootimg_loader (56003158/56003438) は ANDROID! magic 以外の check 無し | ✅ |

## 結論: LK 内には Track G v2 未 touch の boot 拒否 gate は **見つからない**

Ghidra + r2 で LK 全 verify string の xref を追跡した範囲で、**Magisk-patched boot.img を LK が halt させる gate は存在しない**。したがって G-10 Phase 3 の bootloop は **kernel 層以降**が原因である可能性が高い。

## 更新された仮説優先度

| # | 仮説 | G-11 前の priority | G-11 後の priority | 根拠 |
|---|---|---|---|---|
| 1 | LK 内に Track G v2 未 touch の AVB1 verify | 高 | **低** | 全 verify string の xref が SEC_POLICY gate 経由 or halt しない |
| 2 | Sunmi 独自の追加 verify path が LK 内 | 高 | **低** | 同上、Sunmi 独自 string は "Sunmi ロゴ" のみ、verify string は MTK 汎用 |
| 3 | Magisk boot_patch.sh が Sunmi boot.img と非互換で kernel/initramfs 壊れる | 中 | **高** | LK が拒否しない以上、kernel 起動失敗が最有力 |
| 4 | dm-verity が kernel mount 時に reject | 中 | **高** | kernel が起動しつつ /system mount で panic → reboot cycle と整合 |

## 次のTODO(次セッションで最初にやる作業、優先順)

1. **`1 バイト改変 boot.img` 切り分け実験(最優先、brick リスクほぼゼロ)**
   - `dd if=dump/boot.img of=scratch/boot-1byte-mod.img`
   - 末尾 padding を 1 バイト変更(例:最終セクタの 0x00 → 0x01)
   - 焼いて起動確認
   - **通る = 現行仮説確認**(LK は byte-level verify していない、Magisk 側が原因)
   - **通らない = LK に隠れた byte-level check あり**、RE 再実施(次項 2)
2. (1) が失敗した場合のみ: LK RE 深掘り
   - **`FUN_560030b4`(mkimg header check)** の詳細追跡:0x58881688 magic + FUN_5602e8e4 の 0x20 byte check、これが boot 適用されるか
   - **`FUN_56002bb4` / `FUN_56002cfc`** の中身: post_load_verify 系で呼ばれる、halt する可能性を追跡
   - LK payload の code section 中で `sha`, `hmac`, `rsa`, `verify` 系 API 名を追加 grep
3. (1) が成功した場合: Magisk 修正 or kernel cmdline patch
   - 別 Magisk バージョン(現在の canary → 25.x LTS など)で再パッチ
   - initramfs だけ手動抽出 → Sunmi kernel に合わせて再構築
   - kernel command line に `androidboot.veritymode=disabled` `androidboot.vbmeta.device_state=unlocked` 相当を注入
4. dm-verity 側の準備(kernel 起動が確定してから)
   - `/system` partition の block hash tree の位置確認
   - `fs_mgr` の dm-verity disable 手順(command line inject or vbmeta patch)

## 生成物

- `scratch/g8-lk-recon/r2-boot-verify-xrefs.txt` — string xref 一次結果
- `scratch/g8-lk-recon/r2-boot-verify-xrefs-2.txt` — 2 次(hash verify 系)
- `scratch/g8-lk-recon/ghidra-boot-verify-decompile.txt` — 主要候補 function decompile
- `scratch/g8-lk-recon/ghidra-hash-verify.txt` — image_hash_verify 系 decompile
- `scratch/g8-lk-recon/ghidra-loader-helpers.txt` — bootimg loader helper decompile
- `scratch/g8-lk-recon/ghidra-verify-chain.txt` — post-load verify + orange state 系
- `scratch/g8-lk-recon/ghidra-resolve-ptrs.txt` — FUN_56038388 DAT_ pointer 解決
- `scratch/g8-lk-recon/ghidra-scripts/DumpBootVerifyFns.py` 他 — 追加 Ghidra script(再実行可)

## 学び

- **string の xref を全数調査すると boot verify 網は俯瞰できる**。手動で decompile 1 個ずつ追うより 10x 速い
- **Sunmi V2 LK の verify 設計は MTK 汎用そのまま**。Sunmi 独自の boot verify path は(この LK 内には)存在しない
- **Track G v2 は当初想定より広範囲を SKIP している**。`FUN_5601a7e0` を通る全 gate を潰しているため、実質「LK の全 boot cert/hash verify」を無効化
- **verified-boot state (GREEN/YELLOW/ORANGE) は halt に使われていない**。state = ORANGE でも UI 表示だけで boot 継続、これは AOSP の仕様通り(unlocked device は boot 可能で状態表示のみ)
- **LK は kernel/initramfs の中身は見ない**。ANDROID! magic のみ確認、あとは kernel jump address に飛ぶだけ
