# G-13 Phase 2 プラン: Sunmi V2 の Android 10 化(kernel 温存・pre-Treble 継続・32-bit 貫徹)

作成日: 2026-07-02(2026-07-02 全面書き換え、方向転換の記録は本文 Context を参照)
前提: G-12 Phase 0/1a の driver 書き起こし + G-13 Phase 1a-6 の LineageOS 17.1 sync 完了

## Context / 方向転換の背景

本 plan は当初「Android 10 化には kernel を 4.4 → 4.9/4.14 に上げる」前提だった。しかし調査の結果、この前提には**材料が世の中に存在しない致命傷**があった:

- `Power535/android_kernel_common_MT6763` は commit 6 個の廃墟、実質使えない
- AOSP `kernel/mediatek` googlesource には MT6739 の 4.9-q / 4.14-q が存在しない(mooneye-4.4-oreo-wear-dr で止まってる)
- 他 OSS の MT6739 kernel-4.9 tree は事実上どこにも公開されていない(MediaTek 内部 NDA)
- `cateajansmedya/android_kernel_mediatek_mt6739` は Android 7.1、`deadman96385/android_kernel_alcatel_mt6739` は 2 commits の stub

一方で、**MT6739 に Android 10 (LineageOS 17.0/17.1) を移植した実績が K-touch i9 (Anica i9)** にある(damolmo/LineageOS_KTOUCH-i9、PeterCxy/mtk_patches)。その手法は「**kernel は stock を温存、ramdisk / framework 側だけ Android 10 化**」だった。

さらに詰め調査で判明した Sunmi V2 の実態:

| 項目 | 値 |
|---|---|
| 元 OS | **Android 7.1.1**(SDK 25、`ro.build.version.release=7.1.1`)|
| Treble | **pre-Treble**(Android 8.0 前世代、vendor partition 分離なし)|
| Arch | **32-bit ARM 一貫**(`armeabi-v7a` only、`abilist64` 空、`zygote32`)|
| Kernel | 32-bit ARM zImage(先頭 `00 00 a0 e1` = ARM 32-bit NOP)|
| boot.img | ANDROID! **header v0**、kernel_size 7.4MB、page_size 2048、kernel_base 0x40008000 |
| cmdline | `bootopt=64S3,32S1,32S1 buildvariant=user` |
| 駆動 driver | printer/oz8806 は **stock kernel に完全 built-in**(kallsyms で 40+ symbol 確認、dmesg で実動作確認)|
| Partition image | 先頭 1024 bytes は MTK EMMC 予約(ゼロ)、offset 1024 から生 ext4、strings 直接読める |

これを踏まえ、Phase 2 の方針を「**32-bit pre-Treble のまま、stock kernel 温存で LineageOS 17.1 userspace を被せる**」に確定する。K-touch i9 は 64-bit Treble なので device tree は arch 部分だけ書き換えて pattern として借用。

## 設計原則

1. **kernel は Sunmi stock zImage を温存**  
   - printer/oz8806 driver は kernel built-in、`kallsyms.txt` に `spi_printer_probe`、`oz8806_probe` 他 40+ symbol 実在  
   - 実 dmesg で `charger_thread` が oz8806 経由 voltage 読取を継続動作中  
   - **Phase 1a で書き起こした 6 driver は Android 10 化には不要**(Phase 1a 資産として待機、将来 kernel 自前 build 時に再利用可能)

2. **pre-Treble 継続、vendor 分離なし**  
   - `PRODUCT_FULL_TREBLE_OVERRIDE := false`  
   - `TARGET_COPY_OUT_VENDOR := system/vendor`  
   - partition layout は Sunmi stock 温存、bootloader は触らない

3. **32-bit arm 一貫、64-bit 化は目指さない**  
   - Sunmi userspace は元から 32-bit only  
   - 64-bit 化を狙うと stock kernel が使えなくなり Phase 2 が詰む

4. **SELinux は G-12 で確立した permissive routing 継承**  
   - cmdline に `androidboot.selinux=permissive`  
   - stock sepolicy(`scratch/sepolicy-permissive` 由来)の printer_dev_* label を Android 10 sepolicy に merge

5. **noverity 継承、dm-verity は G-12 通り無効**

## 目標成果物

1. **`boot.img`**(header v0、Sunmi stock zImage + LineageOS 17.1 用 ramdisk、24MB partition size 内)
2. **`system.img`**(LineageOS 17.1 base、pre-Treble 統合、arm 32-bit、`/system/vendor/` 込み)
3. Phase 1b で焼くために必要な mtkclient 用の image と scatter file 一式

## 作業ブレークダウン

### Phase 2-A: LineageOS 17.1 build ワークスペース準備(bacondata)

- `bacondata:/home/bacon/sunmiandroid/lineage-17.1/`(72GB、G-13 Phase 1a-6 で sync 済)を使う
- `source build/envsetup.sh` 実行、`lunch` list を確認
- 32-bit arm target で通ることの確認(host tools は arm cross build 前提)
- 参考: `daviiid99/LineageOS_KTOUCH-i9/README.md` の "Build Rules (Q) (Stable)" 手順、arch/Treble 部分は本 plan で書き換え

### Phase 2-B: device tree 作成 `device/sunmi/v2/`

新規で `device/sunmi/v2/` を作成する。骨組みは K-touch i9 (`daviiid99/LineageOS_KTOUCH-i9` の `device_tree_stable` branch)を pattern に、arch/Treble 部分を Sunmi V2 に合わせて書き換える。

**BoardConfig.mk 骨子**:
```make
# Arch (32-bit ARM 一貫、K-touch i9 の arm64 とは違う)
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := generic
TARGET_BOARD_PLATFORM := mt6739

# Kernel(stock 温存)
TARGET_KERNEL_ARCH := arm
BOARD_KERNEL_IMAGE_NAME := zImage
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/zImage
BOARD_KERNEL_CMDLINE := bootopt=64S3,32S1,32S1 buildvariant=user androidboot.selinux=permissive
BOARD_KERNEL_BASE := 0x40008000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_RAMDISK_OFFSET := 0x04ff8000
BOARD_BOOTIMG_HEADER_VERSION := 0

# System-as-Root なし(K-touch i9 も false)
BOARD_BUILD_SYSTEM_ROOT_IMAGE := false
BOARD_USES_RECOVERY_AS_BOOT := false

# pre-Treble、/system/vendor 統合
PRODUCT_FULL_TREBLE_OVERRIDE := false
TARGET_COPY_OUT_VENDOR := system/vendor

# Partition size(stock と同じ、24MB boot、2.5GB system)
BOARD_BOOTIMAGE_PARTITION_SIZE := 25165824
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 2684354560
BOARD_FLASH_BLOCK_SIZE := 131072
```

**rootdir**:
- `rootdir/init.mt6739.rc` を Sunmi stock ramdisk(G-12 で dump 済、`scratch/boot-extract/` 相当)から抽出、LineageOS 17.1 の init.rc 構造に merge
- Sunmi 独自 service(`sunmi_service` 系、`ro.sunmi.*` 依存)は pre-Treble path で残す

**sepolicy**:
- Sunmi stock sepolicy(`scratch/sepolicy-permissive`)の printer_dev_* label を Android 10 sepolicy に追加
- 具体 label: `printer_dev_power`、`printer_dev_reset`、`printer_dev_sleep`、`printer_dev_resume`
- `androidboot.selinux=permissive` を cmdline に入れ、全 domain permissive fallback を確保

### Phase 2-C: prebuilt kernel & DTB の配置

- `dump/boot.img` から `scratch/bootimg-tool.py --unpack` で kernel 部分 (zImage) と ramdisk と DTB を extract
- `device/sunmi/v2/prebuilt/zImage` に kernel を配置
- DTB は stock のを流用(printer/oz8806 の DT binding が既に入ってる、Phase 0/1a で実測確認済)
- 参考: `patches/g12-drivers/arch/arm/boot/dts/mt6739-sunmi_v2-printer.dtsi`(Phase 1a-1 の書き起こし、stock DT と照合用の reference として保管)

### Phase 2-D: vendor blob extract & pack `vendor/sunmi/v2/`

**system.img から extract**:
- `dd if=dump/system.img bs=1024 skip=1 of=scratch/system-raw.img`(先頭 1KB の MTK EMMC 予約領域を落とす)
- `debugfs -R "..." scratch/system-raw.img` で `/system/vendor/`、`/system/lib/hw/`、`/system/lib/mtk*`、`/system/etc/init/*` を extract

**sunmi.img から extract**:
- 同様に offset 1024 で raw 化、Sunmi 独自 lib と `ro.sunmi.*` prop、Sunmi HAL を extract

**配置**:
- `vendor/sunmi/v2/proprietary/` に配置
- `vendor/sunmi/v2/extract-files.sh` と `setup-makefiles.sh` で LineageOS build system に組込

**MediaTek 系 lib の補完**:
- Sunmi 純正に無い MTK lib は `daviiid99/LineageOS_KTOUCH-i9` の `vendor_mediatek_Q` から借用試行
- **K-touch i9 は 64-bit なので 32-bit 版が無い可能性が高い**、その場合 Sunmi stock 側で完結させる

### Phase 2-E: AOSP tree patches

**PeterCxy/mtk_patches の 5 個をそのまま apply**:
```
patch -d external/skia         -p1 < patches/external/skia/0001-GrGLCaps-allow-ignoring-vendor-supplied-texture-swiz.patch
patch -d frameworks/base       -p1 < patches/frameworks/base/0001-hwui-add-dependency-on-libbase.patch
patch -d frameworks/base       -p1 < patches/frameworks/base/0002-Fix-crash-on-some-devices-by-checking-for-null-clien.patch
patch -d frameworks/opt/net/wifi -p1 < patches/frameworks/opt/net/wifi/0001-Restore-O-O-MR1-behaviour-of-initing-ifaces-before-s.patch
patch -d system/core           -p1 < patches/system/core/0001-rootdir-add-support-for-custom-ld-template.patch
```

**Sunmi 固有追加 patch(判明次第)**:
- sepolicy に `printer_dev_*` の 4 label 追加
- `init.mt6739.rc` の Sunmi 独自 service を pre-Treble path で存続
- G-12 で確立した noverity + SELinux permissive path の反映

### Phase 2-F: build & artifact 検証(端末不要範囲)

```
source build/envsetup.sh
lunch lineage_v2-userdebug
brunch v2
```

**生成物**: `out/target/product/v2/{boot.img, system.img}`

**検証項目**:
- [ ] boot.img header dump で `page_size=2048`、`kernel_addr=0x40008000`、cmdline に `androidboot.selinux=permissive` が入ってる
- [ ] `scratch/bootimg-tool.py --unpack out/.../boot.img` で kernel 部分が `dump/boot.img` の stock zImage と **bit-identical**(sha256 一致)
- [ ] system.img から prop 抜き出しで `ro.build.version.release=10`、`ro.build.version.sdk=29`
- [ ] system.img 中に `/system/vendor/lib/hw/gralloc.mt6739.so` 等の vendor blob が入ってる
- [ ] sepolicy dump で printer_dev_* label が Android 10 sepolicy と衝突してない(`sepolicy-analyze` で type conflict チェック)
- [ ] boot.img size ≤ 25165824 bytes(24MB partition 内収まる)
- [ ] system.img size ≤ 2684354560 bytes(2.5GB partition 内収まる)

## Stop point(本 plan 範囲)

- Phase 2-A/B/C/D/E/F 完走で boot.img + system.img が手元にできる状態まで
- **実機焼き試験(Phase 1b 相当)は端末必要 = 別セッション、範囲外**

## 既知リスクと保険

| リスク | 保険 |
|---|---|
| pre-Treble LineageOS 17.1 の MT6739 32-bit port は前例が薄い | まず system-only 差替(boot は Sunmi stock で通す)で system.img だけ試験、boot に手を出すのは stage 2 |
| MTK vendor lib の 32-bit 版が K-touch i9 から借りられない | Sunmi stock 純正 lib(dump 済)を primary、K-touch i9 のは fallback |
| header v0 の stock kernel を LineageOS 17.1 の `brunch` build が受け付けない | 事前に mkbootimg を手動で叩いて v0 / v1 両方生成試行、cmdline は Sunmi stock のを継承 |
| pre-Treble Sunmi V2 で VNDK 分離が無い | `PRODUCT_FULL_TREBLE_OVERRIDE := false` + `TARGET_COPY_OUT_VENDOR := system/vendor` で /system 統合、VNDK 分離自体を回避 |
| printer/oz8806 sysfs の SELinux label が Android 10 sepolicy と齟齬 | 既存 label 名継承、`type_transition` で追加、cmdline `androidboot.selinux=permissive` で全 domain permissive fallback |
| Phase 1b で焼いて bootloop | 実機必要、本 plan 範囲外だが recovery 経路(BROM + mtkclient)は既に確立済 |

## 実行時のユーザ確認ポイント

- Phase 2-A 開始前: bacondata 上の LineageOS 17.1 tree の sync 状態確認(72GB あるか)
- Phase 2-B 完了時: device tree の骨組みが完成、`lunch lineage_v2-userdebug` が通るところまで
- Phase 2-D 完了時: vendor blob の網羅性(printer HAL、oz8806 HAL、Sunmi 独自 daemon 系が全部揃ってるか)
- Phase 2-F 完了時: 焼く前 sanity check(size / magic / cmdline / zImage bit-identical)

## Phase 1a driver 6 個の位置付け

Phase 1a-1〜3 で書き起こした driver 6 個(`spi_printer.c`、`odm_printer_gpio.c`、`sunmi_oz8806_compat.c`、`parameter.c`、`table.c`、`oz8806_battery.c`)と DTS(`mt6739-sunmi_v2-printer.dtsi`)は、**Android 10 化への直接経路からは切り離される**。

理由: Sunmi V2 stock zImage に既に driver が built-in されており(kallsyms 実証済)、stock zImage 温存路線では自前 driver 再ビルドが不要。

**用途**:
1. 将来 kernel を自前 build する必要が出た時(Android 12+ 以降の GKI 化等)の再利用資産
2. printer / バッテリ機能の RE 資産としての保管(driver source が Sunmi V2 修理・拡張時のドキュメント)
3. `patches/g12-drivers/` 配下でそのまま保存、`docs/g12-phase1a-*.md` の記録も温存

## 参考資産

### ローカル

- `dump/boot.img`(Sunmi V2 stock、kernel + ramdisk + cmdline 温存)
- `dump/system.img`(2.5GB、vendor blob source、先頭 1024 bytes は MTK 予約)
- `dump/sunmi.img`(10MB、Sunmi 独自 partition、`ro.sunmi.*` prop 込み)
- `scratch/bootimg-tool.py`(既存の boot.img unpack/repack ツール)
- `scratch/sepolicy-permissive`(G-12 の SELinux permissive 化 sepolicy)
- `scratch/boot-permissive2.img`(G-12 で作った SELinux permissive + noverity boot、cmdline reference として)
- `logs/g12-permissive/kallsyms.txt`(stock kernel の全 symbol、printer/oz8806 driver 存在確認済)
- `logs/g12-permissive/getprop-full.txt`(Sunmi V2 の実 build props、arch/Treble/version 判定根拠)
- `logs/g12-permissive/dmesg-printer.txt`(oz8806 実動作ログ、printer sysfs SELinux audit)
- `bacondata:/home/bacon/sunmiandroid/lineage-17.1/`(sync 済 LineageOS 17.1 tree、72GB)
- `patches/g12-drivers/`(Phase 1a driver 6 個、DTS 書き起こし、待機資産)
- `patches/g12-permissive-tools/`(sepolicy 加工ツール、G-12 で確立)

### 参考 GitHub

- **`daviiid99/LineageOS_KTOUCH-i9`** — MT6739 で LineageOS 17.1 (Android 10) を動かした実績、device_tree_stable / vendor_mediatek_Q / vendor_tree_VNDK27 branch
- **`PeterCxy/mtk_patches`** — AOSP tree に当てる 5 個の patch、そのまま流用可
- **`Iscle/OrangePi_4G-IOT_Android_8.1_BSP`** — MT6739 kernel-4.4 BSP(Phase 0/1a 時の kernel base、Phase 2 では直接使わないが DTS 照合用)

### Phase 1a 資産

- `docs/g12-phase1a-driver-writeback.md`(driver 書き起こしと DT binding 詳細)
- `docs/g12-phase1a-build.md`(kernel-4.4 build 環境 patch、Phase 1a の zImage build 課題整理)
- `docs/g12-phase0-*.md`(kernel baseline / device tree / workspace の Phase 0 記録)
