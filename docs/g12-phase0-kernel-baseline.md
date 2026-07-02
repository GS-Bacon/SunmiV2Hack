# G-12 Phase 0-C: MT6739 kernel-4.4 base tree

作成日: 2026-07-02

## 素材

- **base repo**: `Iscle/OrangePi_4G-IOT_Android_8.1_BSP`(master、Depth=1 clone、877MB)
- **kernel tree**: `Iscle-BSP/kernel-4.4/`(fukehan/kernel-4.4 と比べて defconfig + MTK drivers 完備)
- **配置**: bacondata `/home/bacon/sunmiandroid/kernel/sunmi-v2/`

## defconfig

- **base**: `arch/arm/configs/k39tv1_bsp_1g_defconfig`(K39 = MTK 内部の MT6739 リファレンスプラットフォーム名、1GB RAM variant)
- **derived**: `arch/arm/configs/sunmi_v2_defconfig`(k39tv1 + printer/oz8806 config 追加)

```
CONFIG_MACH_MT6739=y
CONFIG_MTK_PLATFORM="mt6739"

# Sunmi V2 additions (Phase 1a)
CONFIG_SPI_PRINTER=y
CONFIG_ODM_PRINTER_GPIO=y
CONFIG_BATTERY_OZ8806=y
```

## MT6739 DTS 存在(そのまま使える)

- `arch/arm/boot/dts/mt6739.dts` — SoC 本体
- `arch/arm/boot/dts/k39tv1_bsp_1g.dts` — 1GB RAM board
- `arch/arm/boot/dts/k39tv1_bsp_512.dts` — 512MB RAM board
- `arch/arm/boot/dts/k39tv1_bsp.dts` — generic

Sunmi V2 は 2GB RAM だが、`k39tv1_bsp_1g` を base に printer/battery 追加で通す想定。Sunmi V2 の memory node 差し替えは Phase 2 で(現状の Phase 1a では build 通しが目的)。

## build 環境

| 項目 | 値 |
|---|---|
| host | bacondata (Ubuntu 24.04) |
| cross toolchain | `arm-linux-gnueabi-gcc` 13.3.0 |
| host GCC | 13.3.0 |
| Python | 3.12(kernel tools は 一部 Python 2 依存、要 2to3 変換)|
| ccache | 4.9.1、`~/.ccache/` max 100GB |
| out-of-tree | `O=/home/bacon/sunmiandroid/out/kernel-baseline` |

## build 環境の互換パッチ

kernel-4.4 は 2017 頃の実装で、以下の互換問題が発生:

1. **GCC 10+ の `-fno-common` デフォルト化**  
   → `HOSTCFLAGS=-fcommon` と `KBUILD_HOSTCFLAGS=-fcommon` を渡すことで scripts/dtc の multiple def エラー回避

2. **Python 3 化に伴う DrvGen.py の syntax error**  
   → `2to3 -w -n tools/dct/*.py` で書換え(idempotent、header check あり)

## 実行手順(bacondata 上)

```bash
cd /home/bacon/sunmiandroid/kernel/sunmi-v2
make mrproper
2to3 -w -n tools/dct/*.py    # 初回のみ
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabi-
mkdir -p /home/bacon/sunmiandroid/out/kernel-baseline
make sunmi_v2_defconfig O=/home/bacon/sunmiandroid/out/kernel-baseline
make -j4 HOSTCFLAGS=-fcommon KBUILD_HOSTCFLAGS=-fcommon zImage \
     O=/home/bacon/sunmiandroid/out/kernel-baseline
```

## driver 差し込み結果(Phase 1a-1/3 の成果)

以下は Phase 1a で追加した source(kernel tree との差分):

```
drivers/misc/spi_printer.c                    # 新規 (~350 LoC)
drivers/misc/odm_printer_gpio.c               # 新規 (~250 LoC)
drivers/misc/Kconfig                          # SPI_PRINTER + ODM_PRINTER_GPIO 追加
drivers/misc/Makefile                         # 2 obj-y 追加

drivers/power/oz8806/                         # 新規 dir
├── Kconfig                                   # BATTERY_OZ8806 定義
├── Makefile                                  # 4 obj-y (upstream + sunmi compat)
├── battery_config.h                          # MT6755 由来
├── oz8806_battery.c                          # 同上
├── oz8806_regdef.h                           # 同上
├── parameter.c / parameter.h                 # 同上
├── table.c / table.h                         # 同上
└── sunmi_oz8806_compat.c                     # 自作 (typo alias + boot_up_time synonym + sunmi_constant_voltage)

drivers/power/Kconfig                         # oz8806/Kconfig source
drivers/power/Makefile                        # obj-$(CONFIG_BATTERY_OZ8806) += oz8806/

arch/arm/configs/sunmi_v2_defconfig           # k39tv1_bsp_1g + Sunmi 追加 config
```

## 検証

- [x] `k39tv1_bsp_1g_defconfig` 通し試験(driver 抜き)成功
- [x] `sunmi_v2_defconfig` (追加 config 込み)通し成功
- [ ] `zImage` build 成功 → Phase 1a-5 の完了条件、実行中

## 次: Phase 1a-5

zImage build 完了確認 → hash 記録 → 完了。
