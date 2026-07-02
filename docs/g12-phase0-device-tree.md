# G-12 Phase 0-D: K-touch i9 device tree + mtk_patches 単独 clone

作成日: 2026-07-02

## 目的

Sunmi V2 用 LineageOS 17.1 device tree の base として、同じ MT6739 SoC 端末 K-touch i9 の device tree を fork するための素材確保。LineageOS 全体 tree への import は Phase 3。

## clone 結果(bacondata `/home/bacon/sunmiandroid/src/`)

### `ktouch-i9-device/` (PeterCxy/android_device_ktouch_i9, branch `lineage-17.0`)

構造:

```
Android.mk / AndroidProducts.mk
BoardConfig.mk / device.mk / lineage_i9.mk
bluetooth/ configs/ overlay/ overlay-lineage/ prebuilt/
proprietary-files.txt / extract-files.sh / setup-makefiles.sh
recovery.fstab / releasetools/ rootdir/
framework_manifest.xml
```

K-touch i9 の spec(参考):

| 項目 | K-touch i9 | Sunmi V2 |
|---|---|---|
| SoC | MT6739 | MT6739(同一)|
| CPU | Cortex-A53 quad 1.5 GHz | 同じ |
| RAM | 2GB | 2GB |
| Android | 8.1 (O-MR1) 出荷、LineageOS 17 対応済 | 7.1.1 出荷 |
| Display | 340×800 | 720×1280(要 override)|
| Storage | 16/32 GB eMMC | 8GB eMMC + printer/battery hardware 追加 |

### `mtk-patches/` (PeterCxy/mtk_patches, branch `lineage-17.0`)

構造:

```
external/skia/
frameworks/base/
frameworks/opt/net/
non-mtk/frameworks/base/
system/core/
```

Android 10 で MT6739 系を動かすための AOSP 側 patch 集(HIDL、Skia、netd、init 等)。Phase 3 で LineageOS の repo に手動 apply する対象。

## arm32 retarget 方針(Sunmi V2 用)

K-touch i9 は arm64 primary + arm32 secondary の dual-arch。Sunmi V2 は 32-bit userspace 前提なので arm32 単独に retarget:

### K-touch i9 の現状 `BoardConfig.mk`

```makefile
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := generic
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := generic
```

### Sunmi V2 用 (Phase 3 で適用)

```makefile
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a53
# TARGET_2ND_ARCH 系削除
```

## 本 Phase の Stop point

Phase 0-D は「素材 clone のみ」で完了。以下は Phase 3 の作業:

- `~/aosp/lineage-17.1/device/sunmi/v2/` に fork rename
- Sunmi V2 用の spec 反映(display 720×1280、manufacturer/device name、lunch target 追加)
- vendor blobs 準備(`dump/system.img` mount → `/vendor/lib/hw/*.mt6739.so` を `vendor/sunmi/v2/proprietary/` に配置)
- `extract-files.sh` と `proprietary-files.txt` を Sunmi V2 用に書き直し

## 位置(bacondata)

- `/home/bacon/sunmiandroid/src/ktouch-i9-device/`
- `/home/bacon/sunmiandroid/src/mtk-patches/`
