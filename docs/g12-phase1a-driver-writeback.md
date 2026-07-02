# G-12 Phase 1a-1/2: printer driver + DTS 書き起こし

作成日: 2026-07-02

## 成果

- `scratch/g12-driver-writeback/drivers/misc/odm_printer_gpio.c` — printer GPIO+IRQ+sysfs driver(~250 LoC、clean-room)
- `scratch/g12-driver-writeback/drivers/misc/spi_printer.c` — printer SPI + char device + mcu_version 属性(~350 LoC、upstream spidev.c base + Sunmi patch 復元)
- `scratch/g12-driver-writeback/drivers/misc/{Kconfig,Makefile}.snippet` — kernel config entry
- `scratch/g12-driver-writeback/arch/arm/boot/dts/mt6739-sunmi_v2-printer.dtsi` — DT binding(SPI@1100a000 + gpio_printer)

## RE 対応表(odm_printer_gpio.c)

| 関数 RE 位置 | 書き起こし側 |
|---|---|
| `create_sunmi_sysfs` @ 0xC03BBEE8 | `spi_printer.c: create_sunmi_sysfs()`(EXPORT_SYMBOL 込み)|
| `printer_gpio_probe` @ 0xC0858990 | `printer_gpio_probe()`(parse_dt + gpio_request×7 + request_irq×2 + kobject_create + sysfs_create_group)|
| `power_show/store` @ 0xC08582D4/0xC0858098 | `power_show/store` via `GPIO_OUT_ATTR(power, gpio_pwr_en)` |
| `lvl_en_show/store` @ 0xC0858310/0xC0858110 | `lvl_en_show/store` |
| `resume_show/store` @ 0xC085834C/0xC0858188 | `resume_show/store` |
| `sleep_show/store` @ 0xC0858388/0xC0858200 | `sleep_show/store` |
| `reset_show/store` @ 0xC08583C4/0xC0858278 | `reset_show/store` |
| `busy_rts_show` @ 0xC0858400 / store stub @ 0xC0857D48 | `busy_rts_show` / `busy_rts_store_stub` |
| `busy_cts_show` @ 0xC085843C / store stub @ 0xC0857D34 | `busy_cts_show` / `busy_cts_store_stub` |
| `block_rts_show` @ 0xC0858478 / store stub @ 0xC0857D20 | `block_rts_show` (wait_event) / `block_rts_store` (stub) |
| `block_cts_show` @ 0xC08584FC / store stub @ 0xC0857D0C | `block_cts_show` / `block_cts_store` |
| `hardware_free_show` @ 0xC0857E70 / store stub @ 0xC0857D5C | `hardware_free_show` (wake_up_interruptible×2) / stub |
| `gpio_oe_level_status_store` @ 0xC0857EC8 | `gpio_oe_level_status_store`(現状 no-op、要 further RE)|

## RE 対応表(spi_printer.c)

| 関数 RE 位置 | 書き起こし側 |
|---|---|
| `spi_printer_probe` @ 0xC0859D74 | `spi_printer_probe`(kzalloc `sizeof(spi_printer_data)` + class_cdev init + device_create "spidev%d.%d") |
| `class_cdev_init` @ 0xC0859EE8 | `spi_printer_init()`(register_chrdev + class_create "spidev")|
| `spi_dev_ioctl` @ 0xC0859FE8 | `spidev_ioctl`(SPI_IOC_RD/WR_MODE, LSB_FIRST, BITS_PER_WORD, MAX_SPEED_HZ)|
| `mcu_version_show` @ 0xC0858A60 | `mcu_version_show`(spi_sync 経由の MCU 問合せ) |
| `mcu_version_store` @ 0xC085898C | `mcu_version_store`(spi_sync 経由の送信)|

## DT binding(実測 DTB 由来、`logs/g12-permissive/dtb-*.txt`)

### SPI controller

```
spi@1100a000 {
    compatible = "mediatek,mt6739-spi";
    reg = <0 0x1100a000 0 0x1000>;
    interrupts = <0 0x76 8>;       # SPI 118 level-triggered
    clocks = <&topckgen 0x0a>, <&topckgen 0x30>, <&topckgen 0x1b>;
    clock-names = "parent-clk", "sel-clk", "spi-clk";
    mediatek,pad-select = <0>;

    spi_printer@0 {
        compatible = "huaqin,sunmi_printer";
        reg = <0>;
        spi-max-frequency = <50000000>;  # 50 MHz
        status = "okay";
    };
};
```

### GPIO printer

```
gpio_printer {
    compatible = "summi,printer_gpio";   # typo 保存
    printer,pwr_en    = <&pio  7 0>;
    printer,mcu_reset = <&pio  8 0>;
    printer,lvl_en    = <&pio 11 0>;
    printer,sleep     = <&pio 17 0>;
    printer,irq1      = <&pio 19 0>;
    printer,resume    = <&pio 27 0>;
    printer,irq0      = <&pio 28 0>;
};
```

## kernel 統合方針

1. **Iscle BSP kernel-4.4** の `drivers/misc/` に spi_printer.c + odm_printer_gpio.c を配置
2. `drivers/misc/Kconfig` に `Kconfig.snippet` の 2 config を追加
3. `drivers/misc/Makefile` に `Makefile.snippet` の 2 obj-y ラインを追加
4. board DTS(Iscle BSP 側の `arch/arm/boot/dts/mt6739*.dts` にあたるもの)から `#include "mt6739-sunmi_v2-printer.dtsi"` する
5. `sunmi_v2_defconfig` に `CONFIG_SPI_PRINTER=y` + `CONFIG_ODM_PRINTER_GPIO=y`(Phase 1a-4)

## 既知の TODO

- `gpio_oe_level_status_store`: 現状 no-op、実際の GPIO direction toggle は further RE 待ち(factory print flow で叩かれない属性なのでリリース build に必須ではない)
- `mcu_version` の SPI command 詳細:上のコードは placeholder(`0xAA 0x55 0x01 0x00`)。実測は `[   ..]` 起動 dmesg に無く、user space 側の SunmiPrinterService を strace で観測しないと完全再現不可。build には影響なし
- `busy_*` の store が RE では stub(mode 0664 でエントリだけあり、実際何もしない):書き起こし側でも stub

## 検証(Phase 1a-5)

kernel build で以下が通ることが Phase 1a の完了条件:

```
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- sunmi_v2_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- zImage
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- dtbs
```

warning 0 は目指さない(driver 系は upstream 妥当性 warnings 出る)、error 0 と `arch/arm/boot/zImage`, `arch/arm/boot/dts/mt6739-sunmi_v2.dtb` の生成が完了条件。
