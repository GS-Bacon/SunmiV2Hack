# G-12 Phase 0 / Phase 1a 進捗表

作成日: 2026-07-02  
build machine: `bacondata` (SSH `bacon@100.105.139.80`)

## Phase 0(端末不要準備)

| # | 内容 | 状態 | Doc |
|---|---|---|---|
| 0-A | Workspace + build tool 準備 | ✅ 完了 | [g12-phase0-workspace.md](./g12-phase0-workspace.md) |
| 0-B | LineageOS 17.1 tree | 🔄 bacondata でバックグラウンド sync 起動済(2026-07-02 15:23)、shallow / --no-clone-bundle / --no-tags で最小化。完了確認は次セッション | `/home/bacon/sunmiandroid/lineage-17.1/` |
| 0-C | MT6739 kernel-4.4 base(Iscle BSP)+ baseline defconfig | ✅ tree 準備完了、build 中 | [g12-phase0-kernel-baseline.md](./g12-phase0-kernel-baseline.md) |
| 0-D | K-touch i9 device tree + mtk_patches clone | ✅ 完了(単独 clone、統合は Phase 3)| [g12-phase0-device-tree.md](./g12-phase0-device-tree.md) |
| 0-E | oz8806 charger と sunmi_/huaqin_ symbol 洗い出し | ✅ 完了(Ghidra RE 不要、MT6755 upstream 発見)| [g12-phase0-sunmi-patches.md](./g12-phase0-sunmi-patches.md) |

## Phase 1a(kernel port、端末不要範囲)

| # | 内容 | 状態 | Doc |
|---|---|---|---|
| 1a-1 | spi_printer.c / odm_printer_gpio.c 書き起こし | ✅ 完了(kernel tree に配置済)| [g12-phase1a-driver-writeback.md](./g12-phase1a-driver-writeback.md) |
| 1a-2 | mt6739-sunmi_v2-printer.dtsi 作成 | ✅ 完了(scratch 側)、mt6739.dts への include は Phase 1a-5 統合時 | 同上 |
| 1a-3 | oz8806 charger driver 組み込み(MT6755 upstream + Sunmi shim)| ✅ 完了(kernel tree に配置済)| [g12-phase0-sunmi-patches.md](./g12-phase0-sunmi-patches.md) |
| 1a-4 | sunmi_v2_defconfig 更新(printer + oz8806 CONFIG_*)| ✅ 完了 | [g12-phase0-kernel-baseline.md](./g12-phase0-kernel-baseline.md) |
| 1a-5 | driver 単体 compile(spi_printer / odm_printer_gpio / sunmi_oz8806_compat + parameter/table + **oz8806_battery**)| ✅ 完了(**6 ファイル全部 clean compile**、oz8806_battery.o=216KB 追加)| [g12-phase1a-build.md](./g12-phase1a-build.md) |
| 1a-5+ | zImage full build | ⚠️ kernel proper(fork.o)が kernel-4.4 vs modern toolchain の compile-time assertion で失敗 — driver 側は健全と別途検証済、次セッションで Linaro 4.9 toolchain 導入 or Phase 2 の Android 10 kernel で clean build を狙う | 同上 |

## bacondata workspace layout

```
/home/bacon/sunmiandroid/
├── src/
│   ├── Iscle-BSP/                          # Android 8.1 BSP (kernel-4.4 tree の由来)
│   ├── fukehan-kernel-4.4/                 # 参考、MT6739 DTS 完備
│   ├── ktouch-i9-device/                   # LineageOS 17.0 device tree base (Phase 3)
│   └── mtk-patches/                        # Android 10 用 MTK 対応 patches (Phase 3)
├── kernel/
│   └── sunmi-v2/                           # ★ Iscle BSP kernel-4.4 fork + Sunmi drivers
│       ├── arch/arm/configs/sunmi_v2_defconfig
│       ├── drivers/misc/{spi_printer.c, odm_printer_gpio.c}
│       └── drivers/power/oz8806/            # 10 files
├── scratch/
│   ├── g12-printer-re/                     # local dump 転送 (addresses.txt)
│   ├── g12-driver-writeback/               # 書き起こし drivers + DTS
│   ├── oz8806-mt6755/                      # MT6755 upstream backup
│   └── bootimg-tool.py                     # boot.img 解析ツール
├── dumps/
│   ├── boot.img                            # 24MB
│   └── SHA256SUMS
├── logs/g12-permissive/                    # local G-12 実測 log 転送
├── docs/                                   # (未使用、local が正)
└── out/kernel-baseline/                    # build 出力 dir
```

## 本 plan 完了条件

Phase 1a-5 の zImage build 成功 = 本 plan の Stop point。

- 成果物: `/home/bacon/sunmiandroid/out/kernel-baseline/arch/arm/boot/zImage`
- ここから先(実機焼き試験、Android 10 化)は別セッション

## 完了後の scp 回収コマンド(参考)

```bash
scp bacon@100.105.139.80:/home/bacon/sunmiandroid/out/kernel-baseline/arch/arm/boot/zImage \
    /home/bacon/SunmiV2Hack/out/
```
