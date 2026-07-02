# G-12 Phase 0-E: Sunmi/Huaqin 独自 kernel patches 全一覧

作成日: 2026-07-02  
kallsyms source: `logs/g12-permissive/kallsyms.txt`(83,214 symbol、全 address zeroed by kptr_restrict)

## 完全マッピング(復元必要な Sunmi 独自 patch)

| # | 位置 | 内容 | 復元必要度 | 参考 |
|---|---|---|---|---|
| 1 | `drivers/misc/spi_printer.c` | thermal printer SPI driver(spidev.c fork)| **必須**(Phase 1a-1)| `scratch/g12-printer-re/addresses.txt` に完全 RE 済 |
| 2 | `drivers/misc/odm_printer_gpio.c` | printer GPIO + IRQ + sysfs 11 attrs | **必須**(Phase 1a-1)| 同上 |
| 3 | `drivers/power/supply/oz8806.c` | O2Micro OZ8806 fuel gauge / gas gauge IC | **必須**(Phase 1a-3、40+ 関数)| 未 RE、Phase 0-E 対象 |
| 4 | (helper、位置未特定) | `create_sunmi_sysfs`(/sys/sunmi/ctrl/ 生成、EXPORT_SYMBOL 済)| **必須**(printer が依存)| 単純な kset/kobject 作成、簡易 |
| 5 | (未詳) | `huaqin_show` / `huaqin_store` + `huaqin_sysfs_ops` + `huaqin_support_emmcs` | 後回し可 | eMMC 変種吸収 helper と推定、Sunmi V2 単一機で必要不明 |

## 詳細 symbol 一覧

### spi_printer / printer_gpio(既 RE、`addresses.txt` 参照)

kallsyms:

- `spi_printer_init`、`spi_printer_probe`、`spi_printer_remove`
- `printer_gpio_init`、`printer_gpio_probe`、`printer_gpio_remove`

### oz8806 fuel gauge(要 RE)

**API export(EXPORT_SYMBOL、`T` 大文字)、他 driver が呼ぶ:**

- `oz8806_get_client` — I2C client 取得
- `oz8806_get_soc`、`oz8806_get_soc_from_ext` — SOC (State Of Charge、残量 %)
- `oz8806_get_remaincap` — 残量 mAh
- `oz8806_get_battry_current`(typo 保存)— 電流測定
- `oz8806_get_battery_temp`、`oz8806_get_simulated_temp` — 温度
- `oz8806_get_battery_voltage`、`oz8806_vbus_voltage` — 電圧
- `oz8806_get_init_status`、`oz8806_get_power_on_time`、`oz8806_get_boot_up_time`
- `oz8806_get_save_capacity` — SOC 保存値
- `oz8806_register_bmu_callback`、`oz8806_set_batt_info_ptr`、`oz8806_set_gas_gauge` — 他 subsystem 連携
- `oz8806_battery_update_data`、`oz8806_wakeup_full_power`

**内部関数(小文字 t):**

- `oz8806_probe`、`oz8806_remove`、`oz8806_shutdown`、`oz8806_init` — driver lifecycle
- `oz8806_suspend_notifier`、`oz8806_battery_work`、`oz8806_battery_func` — workqueue + PM
- `oz8806_read_byte`、`oz8806_write_byte`、`oz8806_write_word`、`oz8806_temp_read` — I2C access
- `oz8806_create_sys` — sysfs 属性生成
- `oz8806_change_data.constprop.11` — const-propagated variant
- `oz8806_update_batt_info`、`oz8806_update_batt_temp`、`oz8806_get_battery_id_voltage_internal`

**sysfs 属性(show/store ペア):**

| 属性 | show | store |
|---|---|---|
| register | `oz8806_register_show` | `oz8806_register_store` |
| debug | `oz8806_debug_show` | `oz8806_debug_store` |
| save_capacity | `oz8806_save_capacity_show` | `oz8806_save_capacity_store` |
| bmu_init_done | `oz8806_bmu_init_done_show` | (read-only) |

**data:**

- `oz8806_of_match` — of_device_id[]、DT 一致テーブル
- `oz8806_id` — i2c_device_id[]

### Huaqin ODM helper(後回し可)

- `huaqin_show`、`huaqin_store` — 汎用 sysfs op(単一 attr group?)
- `huaqin_sysfs_ops` — sysfs_ops 構造体、attribute 用
- `huaqin_support_emmcs` — eMMC バリエーションのサポートリスト(rodata)

Sunmi V2 単一機で復元不要かは Phase 1a-5 の build で判定(未定義 symbol になれば復元、大丈夫なら省略)。

### create_sunmi_sysfs

`0xC03BBEE8 T create_sunmi_sysfs`、EXPORT_SYMBOL(`__ksymtab_create_sunmi_sysfs`)。/sys/sunmi/ctrl/ の kset + kobject を作る helper。printer_gpio_probe() から呼ばれる(`addresses.txt` 参照)。

実装は自明レベル(`kset_create_and_add("sunmi", NULL, NULL)` + `kobject_create_and_add("ctrl", ...)`)。Phase 1a-1 で spi_printer.c に inline で書き起こしても、独立 `drivers/misc/sunmi_sysfs.c` を作っても可。

## MTK 標準の charger stack(復元不要、BSP に含まれる)

参考として、混同回避のため:

- `mtk_charger_*`(mtk_charger.c、user 空間 IOCTL/sysfs)
- `charger_manager_*`(charger_manager.c、power supply 連携)

これらは Iscle BSP / fukehan/kernel-4.4 に含まれるので復元対象外。

## Phase 0-E 完了条件

- [x] kallsyms から Sunmi/Huaqin/oz8806/printer 関連 symbol 全一覧化(本 doc)
- [x] `oz8806` driver の upstream source 特定(Ghidra RE 不要と判明)
- [ ] `create_sunmi_sysfs` の実装確認(vmlinux 上で確認、または `logs/g12-permissive/dmesg-*` から起動時 log 確認)
- [ ] `huaqin_*` helper の要否判定(build まで先送り)

## oz8806 driver: Ghidra RE 不要、upstream fork 利用可

GitHub コード検索の結果、**OZ8806 driver の source が複数 vendor kernel で公開**されてる:

| Vendor / Repo | Path | Size | Sunmi V2 kallsyms との API 一致度 |
|---|---|---|---|
| Vgdn1942/android_kernel_mt6755_3.18.119 | `drivers/misc/mediatek/power/mt6755/o2micro_battery/` | 41,903B | **7/9 API match** ★ 本命(MTK 系)|
| LineageOS/android_kernel_motorola_sm6225 | `drivers/power/sd77426_fg_mmi/` | 42,617B | 同系(BMT/O2Micro reference driver 由来) |
| carlitros900/CUBE-kernel-U1005 | `drivers/power/mt81xx/o2micro_battery/` | 未確認 | MT81xx MTK 系、参考 |
| Dee-UK/RK3188_tablet_kernel_sources | `drivers/power/oz8806_battery.c` | 単独 file | RK3188 版、参考 |

**All are BMT/O2Micro reference driver 由来**、oz8806 は共通の Battery Management Unit(BMU)IC で、driver 本体は「reference design」がそのまま各社に流用されてる。

### 本命 tree(bacondata に取得予定)

`Vgdn1942/android_kernel_mt6755_3.18.119` の `drivers/misc/mediatek/power/mt6755/o2micro_battery/`:

- `Makefile`(194B)
- `battery_config.h`(2,223B)— platform-specific config
- `oz8806_battery.c`(41,903B)— メインドライバ
- `oz8806_regdef.h`(2,947B)— レジスタ定義
- `parameter.c`(5,418B)、`parameter.h`(10,654B)— run-time parameter
- `table.c`(19,445B)、`table.h`(3,299B)— discharge/OCV table

### Sunmi V2 独自の追加 API(kallsyms 差分)

MT6755 版に対して Sunmi V2 が独自追加してる API:

- `oz8806_get_battry_current`(typo 保存、MT6755 版は `_battery_current`)
- `oz8806_get_boot_up_time`(MT6755 版になし)

これらは `oz8806_battery.c` に手で追加すれば済む(元 API 呼び出し + typo エイリアス関数)。

### Phase 1a-3 での作業

1. MT6755 版の 8 ファイルを `kernel/sunmi/mt6739/drivers/power/supply/oz8806/` に配置
2. Sunmi V2 typo + 1 API を追加 patch
3. `battery_config.h` の `sunmi_constant_voltage=8438000` に合わせる
4. Kconfig / Makefile entry 追加

## 次アクション

- Phase 1a-1: printer driver 書き起こし(RE 素材完備、開始可)
- Phase 1a-3(oz8806)は upstream source 取得だけで済む
- Phase 0-C: Iscle BSP kernel-4.4 が来たら、そこにも oz8806 が居るか確認(居れば MT6755 版より優先)
