# Sunmi V2 T5930 system 差し替え実験プラン

## Context(なぜやる)

Android バージョン上げ(GSI 焼きで Android 9/10 等)の**現実性を実機で確定させる**。

事前の徹底調査で得た結論は「事実上不可能」。根拠:

- **Sunmi V2 の LK/AVB1 は boot だけでなく system も verify する強力な証拠**
  - 今日実測: Magisk-patched boot 焼きで bootloop
  - コミュニティ報告: `ASSAYED Kitchen で "no modifications" の system.img を再焼きしても起動しない`
- **AVB1 世代**で vbmeta 独立パーティション無し(by-name 一覧未検出)、AVB 無効化の穴口が塞がっている
- **MT6739 で成功した GSI 事例は 1 件も見つからない**
- Ubuntu Touch (Halium) / postmarketOS も MT6739 未ポート + 同じ AVB 障壁

しかし「複数の事前情報から結論を出す」より「実機で確定させる」ことに価値がある。理由:

1. **ダメだったことを一次情報として残せる**(未来の自分・同じ端末を触る人への遺産)
2. **A/B の段階的境界調査で "Sunmi の verify 境界" が具体的に分かる**(kiosk 化作業でも間接的に活きる)
3. 復旧経路は「Recovery the system」で確保済み(前回実証、20 分の手戻りで戻せる)

**プラン全体の投資対効果は低い**(成功率 0.1% × 副次価値中)。実行するのはユーザ判断による。

---

## リスクと復旧経路

### 復旧経路(実証済み)

失敗時は以下で回復可能:

1. 端末電源長押しで shutdown
2. 音量+ 押しながら電源キーで Sunmi Recovery 起動
3. Recovery で「**Recovery the system**」選択
4. 5〜10 分で Sunmi 純正状態に復旧(userdata wipe)
5. 初期セットアップウィザードを再走
6. 開発者オプション → USB デバッグ ON
7. `adb push tools/mtk-su/arm/mtk-su /data/local/tmp/mtk-su && adb shell chmod 755 ...`
8. `adb push /tmp/disable-sunmi-1.sh /data/local/tmp/ && mtk-su -c 'disable-sunmi-1.sh'`
9. reboot

**復旧までの手戻り: 概算 20〜30 分**

### 復旧できないシナリオ

- **`lk` パーティション書き換え** → LK 破損 = Sunmi Recovery すら起動しない = **物理ブリック**。**絶対に触らない**
- `preloader`(mmcblk0boot0)書き換え → **物理ブリック**、**絶対に触らない**
- `tee1`/`tee2` 書き換え → TrustZone 死 = **物理ブリック**、**触らない**
- `sec1`, `seccfg`, `efuse` → セキュリティ領域、**触らない**
- `md1img`, `md1dsp`, `spmfw`, `mcupmfw` → モデム/PMIC ファーム、**触らない**

**触っていいのは: `logo`, `system`, `userdata`, `cache` のみ**。

### 復旧素材(現状揃っている)

- `dump/*.img` 34 パーティション全部の完全 dump + SHA256SUMS
- 特に `dump/boot.img`, `dump/system.img`, `dump/recovery.img` は原本
- `tools/firmware/Firmware/` に fishybytes 純正 firmware 一式(万一 dump が壊れた場合の予備)

---

## Phase A: 書き換え境界の非破壊調査(まずここ)

**目的**: どのパーティションが Sunmi LK に verify されているかを、失敗しても復旧が軽い順に確定する。

### A-1: logo パーティション書き換え

**リスク**: 極小(起動画像だけ、Recovery でも通常起動でも救われる)

**手順**:

1. `dump/logo.img` を PC 側でコピー、末尾の 1 バイトを別値に変更した `logo-modified.img` を作る
2. `adb push logo-modified.img /sdcard/`
3. `mtk-su + dd` で logo パーティションに焼く
4. reboot
5. 起動時のアニメーション観察:
   - **通常の Sunmi ロゴ + 起動成功**: logo は verify **されていない** → 書き換え自由
   - **通常起動するが壊れた logo 表示**: logo 単独 verify なし、書き換え反映される
   - **bootloop**: logo も verify されている(強力な verify、system も verify 濃厚)
6. 判定 → A-2 に進むか中止判断

**中止判断ライン A-1**: bootloop したら Phase B/C 中止。原状復帰は `dump/logo.img` を焼き戻し。

### A-2: system.img 末尾の 1 バイト書き換え

**リスク**: 中(system 破損で bootloop の可能性)

**前提**: A-1 が成功 or 一部 verify のみだった場合に実行

**手順**:

1. `dump/system.img` (2.5GB) をコピー、**末尾 1 バイトだけ**変更した `system-1byte.img` を作る
   - 末尾を選ぶ理由: ext4 の後ろにパディング領域があり、file system 破損しにくい
2. `adb push system-1byte.img /sdcard/system-test.img`(3GB 転送、数分)
3. `mtk-su + dd` で system パーティション書き換え
4. reboot
5. 判定:
   - **通常起動**: system の一部書き換えは verify されない、AVB は弱い → Phase C 実験の希望が生まれる
   - **bootloop**: system 全体を厳密 verify、GSI 焼きは 100% 無理 → Phase C 中止

**中止判断ライン A-2**: bootloop したら Phase C 中止。復旧は Recovery the system(20 分の手戻り)。

### A-3: vbmeta 相当の探索

**リスク**: なし(パーティション読むだけ)

**手順**:

1. `binwalk` を PC に入れる
2. Sunmi V2 の疑いパーティション(`sec1`, `seccfg`, `protect1`, `protect2`, `tee1`, `tee2`, `logo`, `boot`, `recovery`)の先頭 4KB を hexdump
3. AVB magic (`AVB0`) や vbmeta 構造の痕跡を探す
4. 見つかれば A-4 で無効化試行、見つからなければ AVB 全体を切る経路は無し

**判定**:
- どこかに AVB0 magic → Phase B に進む余地あり
- どこにもない → Sunmi は完全独自の verify、AVB 無効化不可、Phase C 断念

---

## Phase B: AVB 無効化試行(A-3 で候補が見つかった場合のみ)

**リスク**: 中〜高(候補パーティションの破壊で復旧困難な可能性)

**手順**:

1. A-3 で見つけたパーティションの dump を保管(念のため)
2. `dd if=/dev/zero` で該当パーティションを空 (0x00) で埋める
3. reboot
4. 判定:
   - 起動する + 検証なし表示 → AVB 無効化成功、Phase C 実行可能
   - bootloop → 該当パーティションは書き換え禁止領域 → 復旧して中止

**中止判断ライン B**: bootloop したら Recovery the system で復旧、Phase C 中止。

---

## Phase C: 実際の GSI 焼き実験(A-1 or A-2 で "verify なし" が確定した場合のみ)

**前提**: Phase A で「system 書き換えが verify されない」ことが実測できた場合のみ実行

**手順**:

1. MT6739 用 arm32 GSI を入手(候補):
   - **LineageOS 17.1 arm** (Android 10) — phhusson の Trebled build
   - **PixelExperience arm** (Android 10 or 11)
   - **AndroidOSiP arm** (Android 9)
2. GSI サイズが system パーティション (2.5GB) に収まるか確認
   - GSI が小さければパディングして焼く(dd で sparse image を通常 image に展開)
   - GSI が大きければ焼けない → 別 GSI を探すか諦める
3. `adb push gsi.img /sdcard/`
4. `mtk-su + dd` で system パーティションに焼く
5. reboot
6. 判定:
   - 起動 + AOSP 画面 → 大成功、Android バージョン上げ実現
   - bootloop → Sunmi の verify に負けた、Recovery the system で復旧

**中止判断ライン C**: bootloop したら Recovery the system で復旧、Android 上げ実験終了。Flutter kiosk 化路線に戻る。

---

## 実行ログ設計

各 Phase の実行前後で以下を `logs/experiment-<phaseId>-<timestamp>.log` に保存:

- 対象パーティションの SHA256 (before/after)
- 書き込みの `dd` 出力(bytes transferred, rate)
- reboot 後の `adb devices` 状態(60 秒以内に device として認識されたか)
- 認識された場合の `getprop sys.boot_completed` と `getprop ro.build.fingerprint`
- 認識されなかった場合の `lsusb` 出力(何モードに落ちたか)
- 復旧を実施した場合の手順記録
- 所要時間

WORKLOG.md にも実験のたびに 1 セクション追記(実行前は「開始」、実行後は「結果」)。

---

## 実行前チェックリスト

- [ ] `dump/*.img` の存在確認: `ls ~/SunmiV2Hack/dump/*.img | wc -l` = 34
- [ ] `dump/SHA256SUMS` の完全性: `cd dump && sha256sum -c SHA256SUMS`
- [ ] 現状の mtk-su root 動作: `adb shell "/data/local/tmp/mtk-su -c 'id'"` → uid=0
- [ ] Sunmi Recovery に入れることを physical に再確認(音量+ + 電源キー)
- [ ] `tools/firmware/Firmware/system.img` が予備として存在(万一 dump が壊れたときの二次バックアップ)

---

## 実験全体の予想時間

- Phase A-1: 30 秒(書き換え)+ reboot 60 秒 = 約 2 分
- Phase A-2: system push 5 分 + 書き換え 6 秒 + reboot = 約 7 分
- Phase A-3: 5 分(binwalk 解析)
- Phase B: 数分(該当パーティション書き換え + reboot)
- Phase C: GSI DL(数百 MB, 数分)+ push 数分 + 書き換え 6 秒 + reboot = 約 10 分
- 復旧が必要になった場合: 20-30 分の手戻り

**最悪ケース(全 Phase 失敗 + 復旧 3 回): 約 2 時間**
**平均ケース(A-1 で中止): 3 分**

---

## 期待値と "何が得られるか"

- **成功(0.1%)**: Android バージョン上げの経路が判明、Sunmi V2 で新しい Android が動く実機を得る
- **失敗 A-1(50%)**: 「Sunmi は logo すら verify」の一次情報、Flutter kiosk に即戻れる
- **失敗 A-2(30%)**: 「Sunmi は system 全体を厳密 verify」の一次情報、GSI 経路は絶対無理と確定
- **失敗 A-3(10%)**: 「vbmeta 相当が特定できない = AVB 全体を切る経路無し」の情報
- **失敗 B(9%)**: AVB 無効化経路も潰した
- **失敗 C(0.9%)**: GSI 焼きそのものが動かないと最終確定

すべての失敗は Flutter kiosk 化路線に戻るだけなので、**取り返しがつかない損失は起きない**。

---

## Not-to-do リスト(絶対にやらない)

- LK/preloader/tee/nvram/nvdata/proinfo/seccfg/sec1/efuse/md1*/spmfw/mcupmfw の書き換え
- boot パーティションの書き換え(Magisk 焼きの再挑戦は無意味、既に検証済み)
- userdata の書き換え(復旧に影響ないので触る必要なし)
- 端末の物理分解(test point)、SoC への電気的介入
- Sunmi サーバや Sunmi Cloud への接続実験(端末の遠隔ロックリスク)
