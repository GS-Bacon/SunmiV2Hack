# G-12: Sunmi V2 → Android 10 移植計画(printer 保全)

作成日: 2026-07-02
前提: BROM chain 完全突破済(preloader + LK v2 patched)、dm-verity 潰し済(noverity boot 稼働)、SELinux 全 domain permissive 稼働、adb + mtk-su で root 到達可

## 目的

Sunmi V2 T5930(Android 7.1.1, MT6739, 32-bit ARM)を **Android 10 (LineageOS 17.1 相当)** に上げつつ、**内蔵 thermal printer を保全** する。

## 到達済 baseline(G-12 実測)

- 現行 SunmiPrinterService v6.6.25 + MCU firmware v1.14 で **factory print test 成功**(2026-07-02)
- printer sysfs 11 属性、`/dev/spidev0.0`、`/sys/printer_pin/printer/` すべて健全
- 全 partition backup 済(`dump/*.img`)、任意時点で復旧可

---

## 1. printer driver の完全仕様(kernel binary RE 済)

### kernel 側(source path: `drivers/misc/spi_printer.c` + `odm_printer_gpio.c`、v2_ui3.0_release_new tree)

- **spi_printer 部**: upstream `drivers/spi/spidev.c` の fork(~500 LoC)。SPI slave char device として `/dev/spidevN.M` を expose、標準 spidev ioctls(SPI_IOC_RD/WR_MODE, MAX_SPEED_HZ, BITS_PER_WORD, MESSAGE)。read/write は `spi_sync` 経由。
- **odm_printer_gpio 部**: platform_device probe(DT only)。GPIO 5 本 request + IRQ 2 本 register + sysfs 11 属性 group 作成。SPI 通信を仲介せず、周辺信号線制御のみ担当。
- **依存 API**: すべて generic Linux(`<linux/gpio/consumer.h>`, `<linux/spi/spi.h>`, `<linux/interrupt.h>`, `<linux/sysfs.h>`)。**MTK-specific header なし**(mt_gpio.h, mtk_spi.h に依存しない)= 4.4 → 4.9/4.14 の kernel 移植で API 差分 < 30 行と推定。
- **key symbols**(vmlinux load base 0xC0008000)は `scratch/g12-printer-re/addresses.txt` に完全一覧。

### DTB spec(実機 `/sys/firmware/devicetree/base/` から抽出、`logs/g12-permissive/dtb-*` 参照)

#### 独立 SPI controller(Sunmi patch で MT6739 に増設)

```dts
spi@1100a000 {
    compatible = "mediatek,mt6739-spi";
    reg = <0x0 0x1100a000 0x0 0x1000>;
    interrupts = <0 0x76 8>;   /* SPI 118, level triggered */
    clocks = <&clock 0x0a>, <&clock 0x30>, <&clock 0x1b>;
    clock-names = "parent-clk", "sel-clk", "spi-clk";
    mediatek,pad-select = <0>;

    spi_printer@0 {
        compatible = "huaqin,sunmi_printer";
        reg = <0>;                       /* CS 0 */
        spi-max-frequency = <50000000>;  /* 50 MHz */
        status = "okay";
    };
};
```

MT6739 の標準 DTS は spi2/3/4 のみ、この 0x1100a000 は Sunmi/Huaqin 独自増設。

#### gpio_printer platform_device

```dts
gpio_printer {
    compatible = "summi,printer_gpio";  /* 出荷済 typo、driver match 側もそのまま */
    printer,pwr_en    = <&pio  7 0>;
    printer,mcu_reset = <&pio  8 0>;
    printer,lvl_en    = <&pio 11 0>;
    printer,sleep     = <&pio 17 0>;
    printer,irq1      = <&pio 19 0>;
    printer,resume    = <&pio 27 0>;
    printer,irq0      = <&pio 28 0>;
};
```

pinctrl phandle 0x0e = MT6739 標準 pio node。

### SELinux

- `printer_dev_{power,reset,sleep,resume}` = attribute 毎 type(getattr 制限あり)
- `printer_spi_device` = /dev/spidev0.0 type
- sepolicy source は非公開、`.te` を新規に起こす必要あり

### 併発する Sunmi kernel patch(Android 10 化時に一緒に持ち込み対象)

- `oz8806` charger driver:`sunmi_constant_voltage=8438000` 実測、電池認識のため必須
- charger patch 詳細は別途 RE(driver 名は kallsyms から確認)

---

## 2. Android 10 移植戦略

### 参考実装

| repo | 用途 | 状態 |
|---|---|---|
| PeterCxy/android_device_ktouch_i9 | LineageOS 17.0 for MT6739(arm64、prebuilt kernel) | branch `lineage-17.0` |
| PeterCxy/mtk_patches | Android 10 用 MTK 対応パッチ集(frameworks/system) | branch `lineage-17.0` |
| Iscle/OrangePi_4G-IOT_Android_8.1_BSP | MT6739 完全 BSP(kernel-4.4 source 込) | 8.1 base、10 に再 build 必要 |
| fukehan/kernel-4.4 | MT6739 DTS 完備 | reference |

### アーキ選択:**32-bit 貫徹**

Sunmi V2 は 32-bit userspace + zImage 構成。K-touch i9 の arm64 tree を **arm32 に retarget**:
- `TARGET_ARCH := arm`, `TARGET_ARCH_VARIANT := armv7-a-neon`
- kernel: `zImage`(arm) を build
- printer driver も arm32 でコンパイル

64-bit 化しない理由:printer driver / oz8806 charger / DTS を 64-bit で書き直すリスクが「バージョン UP」の焦点をぼかす。RAM 2GB でも Android 10 は 32-bit で快適(Nokia 1、Redmi Go 前例)。

### 3 トラック並行

#### Track K:Kernel

1. **base**: Iscle OrangePi 4G-IOT BSP の kernel-4.4 を fork、arm32 config で build 検証
2. **printer driver 復元**: `scratch/g12-printer-re/addresses.txt` の関数マップから spi_printer.c と odm_printer_gpio.c を **クリーンルーム書き起こし**(spidev.c fork 部分は upstream 4.4 spidev.c をベースに patch を移植)。sysfs 11 属性の semantics は G-12 で完全把握済
3. **DTS 追加**: 上記 spec を `arch/arm/boot/dts/mt6739-sunmi_v2.dts` に追加
4. **oz8806 charger 復元**: 同様に vmlinux RE で driver 復元(別 Ghidra pass 必要)
5. **他の Sunmi patches 洗い出し**: kallsyms から `sunmi_*` / `huaqin_*` symbol 一覧、全部拾って復元
6. **Android 10 config**: FBE, SAR, dm-verity は無効化(AVB もオフ)
7. **build target**: `zImage-dtb`(dtb append 形式、boot header v2 の kernel フィールドに突っ込む)

**成果物**: `boot-v2-lineage17.img`(新 kernel + 新 ramdisk + 新 DTB)

#### Track U:Userland(LineageOS 17.1)

1. **device tree**: PeterCxy K-touch i9 tree を `device/sunmi/v2/` に fork、以下改修:
   - arm64 → arm32 で再 target
   - display(340×800 → Sunmi V2 の 720×1280)
   - touch panel driver ident
   - camera(front/rear 差分)
   - sensor(accelerometer 等)
2. **vendor blobs**: 現行 dump/system.img から `/vendor/lib/hw/*.mt6739.so`(audio/camera/gralloc/gps/hwcomposer/lights/memtrack/power/vulkan)を `vendor/sunmi/v2/proprietary/` に配置。extract-files.sh でハッシュ計上
3. **Sunmi 独自 app 温存**:
   - `/system/app/`: SunmiPrinterService, SunmiOpenService, SunmiBaseService, SunmiPaymentManager, etc.
   - `/system/framework/`: `sunmi.jar`, `sunmi-res.apk`
   - すべて **prebuilt APK** として device.mk 経由で焼き込み
4. **sepolicy**: `.te` を新規:
   - `printer_dev_{power,reset,sleep,resume}` type + rules(system server, printer service から allow)
   - `printer_spi_device` type + `/dev/spidev0.0` file_contexts
5. **build**: `lunch lineage_v2-userdebug && brunch v2`

**成果物**: `system.img`(LineageOS 17.1 + Sunmi apps + vendor blobs)

#### Track B:Boot & Partition

1. **boot.img header v2** に upgrade(SAR 対応、dtb append)
2. **ramdisk**: PeterCxy K-touch i9 の SAR ramdisk 構成を Sunmi V2 用に改修(init.rc、fstab)
3. **fstab**: `/system` を rootfs mount(SAR)、`/vendor` を `/system/vendor` symlink 継続(pre-Treble 継承)
4. **vbmeta**: skip(AVB 無効)
5. **partition size 確認**: 現行 system パーティション 2.6GB(mmcblk0p??)、LineageOS 17 system.img は 1.8GB 目安、収まる

**成果物**: `flashall.sh`(全 partition を BROM で raw flash するスクリプト)

### 復旧経路(brick 対策)

- 各 track 完了時点で個別 flash → 動作確認 → 次へ
- 失敗時は `dump/*.img` を BROM raw flash で完全復旧
- 経験則:preloader-boot0.img が生きてれば手順は再現可能(G-9 実績)

---

## 3. 作業順(推奨)

### Phase 0: 事前準備(端末不要)

- [ ] LineageOS 17.1 source tree 取得(`repo init -u ...`)
- [ ] K-touch i9 device tree fork、Sunmi V2 用に rename
- [ ] Iscle BSP から MT6739 kernel-4.4 tree 抽出、arm32 config で build 通す(printer driver なし)
- [ ] Ghidra ボリューム RE:oz8806 charger + その他 Sunmi patches の洗い出し

### Phase 1: kernel port(printer driver 単体)

- [ ] spi_printer.c と odm_printer_gpio.c を書き起こし
- [ ] DTS に SPI@1100a000 + gpio_printer node 追加
- [ ] クリーンな Android 8.1(既存 OrangePi 8.1 BSP)で試験 build → 動作確認可能なら:
  - 現行 Sunmi V2 boot.img と互換の位置で新 kernel を配置、noverity 環境で焼いて起動 → printer sysfs アクセス確認 → factory print test

### Phase 2: Android 10 kernel

- [ ] kernel を Android 10 config に切替(FBE、SAR、AVB=off)
- [ ] boot.img v2 化、ramdisk SAR 化
- [ ] 新 boot.img を BROM flash、起動確認(この時点では既存 system.img と組み合わせて動く必要はない、UART or fastboot debug で kernel init まで見る)

### Phase 3: userland(LineageOS 17.1)

- [ ] system.img build → BROM flash
- [ ] 起動確認(既存 boot.img と組み合わせ、Track K の kernel + new system)
- [ ] Sunmi apps 動作確認(印刷、決済 SDK)

### Phase 4: 統合

- [ ] Track K + Track U 全部合成、フル書き換え
- [ ] 完全動作確認

### Phase 5: リリース準備

- [ ] flashall.sh 整備
- [ ] 復旧手順書き上げ

---

## 4. 現時点の中断点 / 継続再開に必要な情報

- BROM 進入手順:WORKLOG G-9 に詳細あり、preloader-boot0.img 使用
- 現行 flash:`scratch/boot-permissive2.img`(全 domain permissive、ro.secure=0、ro.debuggable=1、noverity 継承)
- 全 backup:`dump/*.img`

## 5. 開いてる問題(先送り)

- oz8806 charger driver 詳細(Phase 0 で RE)
- Sunmi 独自 sysfs helper `/sys/sunmi/ctrl/*` の他の attribute(cinode、hwmal_report_forward、spk/time)
- SunmiWelcome / SunmiBackup / SunmiOTAUpdate の framework API 依存(hidden API 使ってれば LineageOS 17 で動かない)
- printer 用 sepolicy .te の完全定義(現行は type ラベルだけ判明、allow 文の source policy は Sunmi のみ)

## 6. 参考

- printer driver RE 詳細:`scratch/g12-printer-re/addresses.txt`
- DTB 実測:`logs/g12-permissive/dtb-*`
- adb 収集全ログ:`logs/g12-permissive/`
