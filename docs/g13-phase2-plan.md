# G-13 Phase 2 プラン: Android 10 kernel port(kernel-4.9/4.14 系)

作成日: 2026-07-02  
前提: Phase 0 + Phase 1a 完了(driver 6 個 clean compile、LineageOS 17.1 tree bacondata に sync 済)

## Context / 動機

Phase 1a の driver work は完成、しかし kernel-4.4 の zImage full build が **modern toolchain との vintage 不整合**で詰まってる(`kernel/fork.o` の compile-time assertion 等)。

以下 2 択が候補:

| 選択肢 | コスト | 実益 |
|---|---|---|
| A. Linaro 4.9 arm-eabi toolchain 導入 → kernel-4.4 zImage 完成 | 中 | kernel-4.4 の zImage が出るだけ、Android 10 化には Phase 2 で結局 kernel 上げる |
| **B. Phase 2 前倒し = kernel-4.9/4.14 で zImage build** | 中 | Android 10 の本命 kernel、modern toolchain(gcc-9/13)対応済、build 完成 → 焼き試験まで一気通貫 |

**B 採用**。理由: 最終目標(Android 10 移植)への直線経路、環境 patch 累積の重複投資回避。

## kernel base tree の選定

### 本命: `Power535/android_kernel_common_MT6763`

- **Android 10 対応**、multi-SoC(MT6580 / MT6737 / MT6739 / MT6750 / MT6757c / MT6761 / MT6762 / MT6763)
- **kernel-4.9 系**
- MediaTek 系 vendor kernel の modern fork、gcc-9〜13 対応 patch 済(の可能性大、要確認)
- MT6739 defconfig の存在は要調査

### 代替: `LineageOS/android_kernel_motorola_sm6225`(参考のみ)

- LineageOS 17 系の Motorola sm6225 kernel、oz8806_battery.c もこの tree に存在
- ただし SoC が Qualcomm sm6225 で MT6739 とは互換なし、参考用

### 保険:LineageOS 17.1 の manifest から MT6739 用 kernel を探す

- `bacondata:/home/bacon/sunmiandroid/lineage-17.1/.repo/manifest.xml` に mt6739 系 kernel 定義があるか
- 通常 LineageOS は per-device kernel、共通 platform kernel は無いことが多い

## 目標成果物

1. `zImage`(Sunmi V2 用、Android 10 kernel + Sunmi driver 6 個込み)
2. `mt6739-sunmi_v2.dtb`(printer bindings 込み)
3. `boot.img v2`(SAR ramdisk 付き、noverity 化、SELinux permissive 用 cmdline)

## 作業ブレークダウン

### Phase 2-A: kernel base tree 選定・clone

- **調査**: `Power535/android_kernel_common_MT6763` の branch 一覧、`arch/arm/configs/` に mt6739 系 defconfig あるか、gcc-9 or 13 で baseline build 通るか(driver 抜き)
- **調査**: LineageOS 17.1 tree で mt6739 系 kernel が manifest から入ってるか
- **選定**: Power535 に mt6739 defconfig ある + baseline build 通ればそれで確定、なければ fallback 検討
- **clone**: bacondata `/home/bacon/sunmiandroid/kernel/sunmi-v2-android10/` に配置

### Phase 2-B: Phase 1a driver を新 tree に移植

Phase 1a で書き起こした 6 ファイルを新 kernel-4.9 tree に配置し API 差分 patch:

| ファイル | 予想差分(kernel 4.4 → 4.9) |
|---|---|
| `drivers/misc/spi_printer.c` | 少(SPI framework 安定)|
| `drivers/misc/odm_printer_gpio.c` | 少(GPIO/IRQ 安定)|
| `drivers/power/oz8806/sunmi_oz8806_compat.c` | ほぼゼロ(EXPORT_SYMBOL 中心)|
| `drivers/power/oz8806/parameter.c/table.c` | ほぼゼロ |
| `drivers/power/oz8806/oz8806_battery.c` | 4.9 で `power_supply_desc` が確定的、Phase 1a-3 の port patch そのまま使える見込み |
| `arch/arm/boot/dts/mt6739-sunmi_v2-printer.dtsi` | DTS 構文は互換、include path のみ差異可能性 |

**検証**: `make drivers/misc/spi_printer.o` 等の per-file compile が 4.9 tree 上で通ることを確認。

### Phase 2-C: Android 10 用 kernel config

Iscle BSP の `k39tv1_bsp_1g_defconfig` は Android 8.1 用。Android 10 向けに以下を反映:

- `CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"`(Treble 準拠、pre-Treble ながら Android 10 の init が期待)
- FBE(`CONFIG_FS_ENCRYPTION=y`、`CONFIG_ENCRYPTED_KEYS=y`)
- **AVB=off**(`CONFIG_ANDROID_VERITY` off、Phase 1a-5 の dm-verity 潰し継承)
- **SELinux permissive 前提の cmdline**(G-12 の実測継承、`ro.secure=0`、`ro.debuggable=1`)
- SAR(system-as-root)対応(`CONFIG_ROOTFS_ROOTDEV=y` 等)
- printer + oz8806(Phase 1a-4 と同じ 3 config)

### Phase 2-D: boot.img v2 header + SAR ramdisk

Android 10 の boot header は v2:

- header version 2
- `dtb` field に mt6739-sunmi_v2.dtb を append
- ramdisk は SAR ready(init が rootfs = /system を mount する構造、`ro` mount)
- Sunmi V2 は pre-Treble(`/vendor` = `/system/vendor` symlink 継続)

**素材の再利用**: G-12 の `scratch/boot-permissive2.img`(SELinux permissive + noverity)から ramdisk 部分と cmdline を抽出、新 kernel と組み合わせて `boot-v2.img` 生成。

### Phase 2-E: 検証(端末不要範囲)

- [ ] `make ARCH=arm zImage` 成功、size 妥当(6-10MB)
- [ ] `make ARCH=arm dtbs` 成功、`mt6739-sunmi_v2.dtb` 生成
- [ ] `mkbootimg`(または `bootimg-tool.py` 相当)で `boot-v2.img` 生成
- [ ] Sunmi V2 の実 boot header layout(24MB partition)に size 収まる

## Stop point(本 plan 範囲)

- Phase 2-A/B/C/D/E まで完走で `boot-v2.img` が手元にできる状態
- **実機焼き試験(Phase 1b 相当)は端末必要 = 別セッション、範囲外**

## 検証・受け入れ基準

| Phase | 合格基準 |
|---|---|
| 2-A | Power535 tree で mt6739 baseline defconfig 通し、driver 抜き zImage build 成功 |
| 2-B | Phase 1a 6 driver が 4.9 tree で clean compile |
| 2-C | sunmi_v2_defconfig(Android 10 対応版)通し成功、`.config` に Android 10 前提の CONFIG 反映済 |
| 2-D | `boot-v2.img` 生成、`bootimg-tool.py --unpack` で reverse に元通り抽出可 |
| 2-E | 上記 4 個の artifact hash 記録 |

## 実行時のユーザ確認ポイント

- Phase 2-A 開始前: kernel base tree の選定(Power535 default で ok か)
- Phase 2-B 完了時: driver 移植で追加 patch 必要になった場合の記録方法
- Phase 2-C 開始前: Android 10 config の default set(FBE 必須 or optional 等)
- Phase 2-D 完了時: 焼く前 sanity check(size / magic / cmdline)

## 既知リスク

- **Power535 tree に MT6739 defconfig が無い可能性** → LineageOS manifest 由来 or Iscle 変換で対応
- **kernel-4.9 の DTS binding が 4.4 と微妙に違う** → 実測 DTB は G-12 の logs にあるので照合可能
- **SAR 化に伴う ramdisk 大改造** → G-12 の boot-permissive2.img base を保守
- **Phase 1b で焼いても boot loop する可能性** → 実機必要、本 plan 範囲外だが recovery 経路(BROM + mtkclient)は既に確立済

## 参考

- Phase 1a 成果: `patches/g12-drivers/` + `docs/g12-phase1a-*.md`
- LineageOS 17.1 tree: `bacondata:/home/bacon/sunmiandroid/lineage-17.1/`
- 参考 repo:
  - `Power535/android_kernel_common_MT6763`(Android 10 kernel 4.9)
  - `LineageOS/android_kernel_motorola_sm6225`(参考、oz8806 現代化)
- Phase 1a-5 の環境 patch 累積(次 tree でも参考): `docs/g12-phase1a-build.md`
