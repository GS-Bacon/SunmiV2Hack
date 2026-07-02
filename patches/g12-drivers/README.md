# G-12 Sunmi V2 kernel drivers(Phase 1a 成果物)

Phase 0-E で行った RE 成果 + Phase 1a-1/2/3 の clean-room 書き起こしまとめ。

## 適用対象

MT6739 用 Linux kernel 4.4 tree(例: Iscle BSP `Iscle/OrangePi_4G-IOT_Android_8.1_BSP` の `kernel-4.4/`)。

## 中身

```
drivers/misc/
├── spi_printer.c                   # 新規、thermal printer SPI + /dev/spidev0.0 + /sys/sunmi/ctrl/mcu_version
├── odm_printer_gpio.c              # 新規、printer GPIO + IRQ + /sys/printer_pin/printer/ 11 属性
├── Kconfig.snippet                 # 上流 Kconfig の endmenu 直前に append
└── Makefile.snippet                # 上流 Makefile 末尾に append

drivers/power/oz8806/
├── sunmi_oz8806_compat.c           # 新規、typo alias + boot_up_time synonym + sunmi_constant_voltage
├── Kconfig                         # BATTERY_OZ8806 定義
└── Makefile                        # obj-y (upstream + sunmi shim 合算)

arch/arm/boot/dts/
└── mt6739-sunmi_v2-printer.dtsi    # 新規、SPI@1100a000 + gpio_printer node
```

**外部 fetch 必要**(kernel tree に含める、`drivers/power/oz8806/` に配置):

- `Vgdn1942/android_kernel_mt6755_3.18.119` の `drivers/misc/mediatek/power/mt6755/o2micro_battery/`:
  - `oz8806_battery.c`(要 `mach/` → `mt-plat/` include 修正)
  - `oz8806_regdef.h`、`battery_config.h`
  - `parameter.c/h`
  - `table.c/h`

## 適用手順

```bash
KERN=/path/to/kernel-4.4    # e.g. Iscle BSP の kernel-4.4/

# 1. driver 配置
cp drivers/misc/spi_printer.c      $KERN/drivers/misc/
cp drivers/misc/odm_printer_gpio.c $KERN/drivers/misc/

# 2. Kconfig / Makefile 統合
cat drivers/misc/Kconfig.snippet   >> $KERN/drivers/misc/Kconfig
cat drivers/misc/Makefile.snippet  >> $KERN/drivers/misc/Makefile

# 3. oz8806 dir 配置
mkdir -p $KERN/drivers/power/oz8806
cp drivers/power/oz8806/*.c       $KERN/drivers/power/oz8806/
cp drivers/power/oz8806/Kconfig   $KERN/drivers/power/oz8806/
cp drivers/power/oz8806/Makefile  $KERN/drivers/power/oz8806/
# + MT6755 upstream から oz8806_battery.c 等を配置

# 4. drivers/power 統合
sed -i 's|^endif # POWER_SUPPLY|source "drivers/power/oz8806/Kconfig"\nendif # POWER_SUPPLY|' $KERN/drivers/power/Kconfig
echo 'obj-$(CONFIG_BATTERY_OZ8806) += oz8806/' >> $KERN/drivers/power/Makefile

# 5. DTS
cp arch/arm/boot/dts/mt6739-sunmi_v2-printer.dtsi $KERN/arch/arm/boot/dts/
# board DTS で #include "mt6739-sunmi_v2-printer.dtsi" する

# 6. defconfig
cat >> $KERN/arch/arm/configs/sunmi_v2_defconfig <<EOF
CONFIG_SPI_PRINTER=y
CONFIG_ODM_PRINTER_GPIO=y
CONFIG_BATTERY_OZ8806=y
EOF
```

## 検証(bacondata で確認済)

以下 5 ファイルの clean compile(1 warning 以下、error 0):

- `drivers/misc/spi_printer.o`(158 KB)
- `drivers/misc/odm_printer_gpio.o`(144 KB)
- `drivers/power/oz8806/sunmi_oz8806_compat.o`(34 KB)
- `drivers/power/oz8806/parameter.o`(132 KB)
- `drivers/power/oz8806/table.o`(16 KB)

`oz8806_battery.o` は `mach/` include cascade 修正で通る(所要 5-10 個の header path 書換え、Phase 1a-3 refinement)。

## driver ↔ 実測 RE 対応表

`docs/g12-phase1a-driver-writeback.md` に完全リスト。特に:

- printer sysfs 11 属性は `scratch/g12-printer-re/addresses.txt` の Device Attribute Table @ 0xC13A3D5C と一対一
- oz8806 の 40+ 関数は `logs/g12-permissive/kallsyms.txt` の `oz8806_*` symbol と対応(upstream MT6755 版とほぼ完全一致、Sunmi V2 独自は typo alias と `_boot_up_time` の 2 個のみ)
- DTB は `logs/g12-permissive/dtb-*.txt` の実測 hex を再構成
