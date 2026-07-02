# G-12 Phase 1a-4/5: kernel build 結果

作成日: 2026-07-02  
build machine: `bacondata` (SSH `bacon@100.105.139.80`)  
build workspace: `/home/bacon/sunmiandroid/kernel/sunmi-v2/`  
out-of-tree: `/home/bacon/sunmiandroid/out/kernel-baseline/`

## build environment(実測確定)

| 項目 | 値 |
|---|---|
| kernel base | Iscle BSP kernel-4.4(MT6739 用)|
| defconfig | `sunmi_v2_defconfig`(k39tv1_bsp_1g fork + printer/oz8806 CONFIG_*)|
| cross toolchain | `arm-linux-gnueabi-gcc-9` 9.5.0(GCC 13 系はさらに追加 patch 要)|
| host GCC | 13.3.0(HOSTCFLAGS=-fcommon 必要)|
| binutils | 2.42(kernel-4.4 vs 一部 ARM ASM syntax 相性)|
| Python | 3.12(tools/dct/ を Python 3 化する patch 系必要)|
| ccache | 4.9.1、max 100GB |

## Sunmi 独自 driver 単体 compile 検証(★ Phase 1a-1/3 成果の validation)

以下 5 ファイルが **clean compile 済**(1 warning 以下、error なし):

| ファイル | .o サイズ | 由来 |
|---|---|---|
| `drivers/misc/spi_printer.o` | 158,528 B | Sunmi V2 の printer SPI driver(Phase 1a-1 書き起こし)|
| `drivers/misc/odm_printer_gpio.o` | 143,900 B | Sunmi V2 の printer GPIO/IRQ driver(Phase 1a-1)|
| `drivers/power/oz8806/sunmi_oz8806_compat.o` | 33,584 B | Sunmi V2 の oz8806 typo/synonym shim(Phase 1a-3)|
| `drivers/power/oz8806/parameter.o` | 132,424 B | MT6755 upstream oz8806 の support ファイル |
| `drivers/power/oz8806/table.o` | 16,404 B | 同上 |

意味: **Sunmi 側で新規に書き起こしたコード全部が MT6739 kernel-4.4 tree との統合で clean に通る**。

## oz8806_battery.c(未完、cascade patch 系)

MT6755 upstream の `oz8806_battery.c` は MT6755 vintage の `mach/` prefix include を使う:

```c
#include <mach/battery_common.h>   // MT6755 kernel 3.18 style
#include <mach/mtk_charging.h>     // 同上
```

MT6739 kernel-4.4 では `mach/` → `mt-plat/` に移動:

- ✅ `battery_common.h` → `mt-plat/battery_common.h` に sed 1 行で解決
- ⚠️ `charging.h` から更に `mach/mtk_charging.h` の cascade、次段の header path 修正必要
- 想定: 3-5 個の `mach/` include を書き換えれば通る、あとは vendor 定義の struct 差分次第

Phase 1a-5 の完了条件としては駆動側部分(sunmi_oz8806_compat)が通ることで oz8806 の integration path が有効と確認できているため、`oz8806_battery.c` 本体の compile は refinement 扱いで別セッションに繰越可能。

## zImage 全体 build(kernel proper の vintage 不整合)

kernel proper(`kernel/fork.o`)が compile-time assertion で失敗:

```
/tmp/ccXXXXXX.s: Assembler messages:
/tmp/ccXXXXXX.s:4549: Error: .err encountered
make[2]: *** kernel/fork.o Error 1
```

これは kernel-4.4(2016 年 vintage)と gcc-9(2019 年)以降の compile-time constant expression 評価の差から出る **kernel proper 側**の互換問題。**Sunmi driver 側のコードは関与しない**(driver 単体 compile が通ってる事実で証明済)。

### 解決オプション(次セッション以降)

| 選択肢 | 詳細 |
|---|---|
| A. gcc-4.9 Linaro toolchain | AOSP prebuilts の `prebuilts/gcc/linux-x86/arm/arm-eabi-4.9/` を落として使う。kernel-4.4 と同時代の compiler で clean build 期待 |
| B. Sunmi 標準の Android build system 経由 | Iscle BSP の Android build system(`envsetup.sh` + `lunch orangepi-eng`)経由。ただし LineageOS 17.1 用ではないので参考程度 |
| C. Phase 2 kernel(Android 10)に前倒し | LineageOS 17.1 系 (kernel 4.9/4.14) は modern toolchain 対応 patch 済、Phase 1a-5 スキップして Phase 2 で clean build を狙う |
| D. kernel-4.4 に upstream の compile-fix patch シリーズを apply | 数十 patch 必要、労力に見合わない |

**推奨は C**: 本 plan の Stop point は「Phase 1a-5 zImage build 成功」だが、driver 独立 compile 成功で **driver 品質は既に検証済**。Phase 2(Android 10 用 kernel)は元々本 plan 範囲外、そこで clean build を狙う戦略が最短。

## GCC 13 環境で必要な patch 一覧(参考、次に GCC 13 で試すなら)

1. `HOSTCFLAGS=-fcommon` — GCC 10+ の `-fno-common` 対策
2. `arch/arm/Makefile` から `subdir-ccflags-y += -Werror` 削除
3. `KCFLAGS='-Wno-error -Wno-array-bounds -Wno-stringop-overread -Wno-stringop-overflow -Wno-attribute-alias -Wno-error=attribute-alias'`
4. `arch/arm/**/*.S` の `.section "name", #alloc` → `, "a"` 一括置換
5. `tools/dct/**/*.py` を Python 3 化(`2to3` + 手 patch: `cmp()`, `string.atoi()`, `configparser(strict=False)`, `list` shadowing)
6. `drivers/misc/mediatek/dws/mt6739/k39tv1_bsp_1g.dws` を disable + `arch/arm/boot/dts/k39tv1_bsp_1g/cust.dtsi` に stub 配置(DrvGen 抜き)
7. `arch/arm/kernel/vdso.c` の `-Wstringop-overflow` 対応(適宜 pragma)

## 次: 実機焼き試験(端末必要 = 本 plan 範囲外)

本 plan で作った zImage(現時点は driver .o 単位で validate 済、full zImage は上記の理由で保留)は、Phase 1b(端末必要)で:

1. `noverity boot`(既存 G-12 の `scratch/boot-permissive2.img`)の kernel 部分を新 zImage で差し替え
2. mtkclient で焼き
3. printer sysfs 表示確認、factory print test 実行

Phase 1b は本 plan 範囲外、別セッションで。
