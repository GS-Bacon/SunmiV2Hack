# WORKLOG - Sunmi V2 T5930 kiosk 化プロジェクト

## 記入ルール

新しいエントリは、このセクション直下(先頭)に追記する。逆時系列(最新が上)。

書式:
- 見出し: `## YYYY-MM-DD HH:MM (ブランチ名)`
- 変更概要 / 主な変更ファイル / コミット / 次のTODO

---

## 2026-07-01 18:40 (main) — Sunmi Recovery "Recovery the system" で復旧成功、方針転換

### 変更概要

前回の bootloop 状態から、**Sunmi Recovery のメニュー「Recovery the system」** を選択したところ、これが実質 **Factory Reset + boot 修復** 動作をしたようで、端末が Sunmi 純正の初期セットアップ画面(Hello ロゴ)から起動できるように **完全復旧**。ユーザに setup wizard を再走してもらい、USB debug 再有効化 → adb 復活。**Sunmi V2 の Recovery は復旧の切り札として使える**ことが実地で判明した。

### 復旧で判明した Sunmi V2 T5930 の性質

1. **「Recovery the system」= 純正 factory reset + boot 修復**。Magisk-patched で壊れた boot を元に戻してくれる(userdata は wipe される)。**ブリック時の最終手段として非常に価値が高い**
2. **Magisk 永続 root は無理**。Sunmi の LK による AVB1 検証で patched boot は必ず起動失敗する。Magisk 焼きは今後禁止
3. **書き換え経路の全体像**が固まった:
   - Android 起動状態: adb + mtk-su(CVE-2020-0069)で一時 root、boot/system は dd で書ける
   - Sunmi Recovery: 復旧専用、adb 不可
   - META (0e8d:20ff) / Cyrus Preloader (0e8d:2008): mtkclient handshake 不可、Sunmi の auth 必須、実質使えない
   - BROM (0e8d:0003): 物理コンボで進入不可、test point 未確認
4. **auth_sv5.auth や DA / preloader / scatter を渡しても mtkclient は 0e8d:2008 と通信できず**。SP Flash Tool ならいけるかもしれないが、Recovery の切り札があるので当面不要

### 方針転換

- **Magisk 焼き:中止**。以後、root は毎回 mtk-su の一時 root で行う
- **GSI 実験(Phase 4):削除**。復旧経路の余裕がないのでリスク大き過ぎ
- **Phase 3 継続**: Sunmi 剥がし(disable-user)+ Flutter kiosk 化 で完成させる

### 復旧後の作業

- mtk-su を /data/local/tmp/ に再 push、root 動作確認 (uid=0)
- Sunmi 剥がしスクリプト(17 パッケージ disable-user)再適用
- reboot 実施、post-reboot 状態:
  - Sunmi/woyou プロセス: 0 個(前回 reboot 後は 3 個残り、今回は完全に 0)
  - Focused Activity: com.woyou.launcher(Sunmi Launcher が Home に居座り、まだ剥がしていない)
  - プリンタサービス `woyou.aidlservice.jiuiv5` は Service として active、lazy load 動作確認
  - MemAvailable 501,480 kB (490 MB)
- Firmware パッケージは復旧不要になったが、`tools/firmware/` に保存済み(将来またブリックしたときの保険)
- Flutter snap インストール中(前回 exit 0 だったが実際は失敗していた、再度実行中)

### 得られたファイル

- `tools/firmware/Sunmiv2 stock with patch.rar` (1.29 GB) — fishybytes 経由の Sunmi V2 gen1 純正 firmware。含むもの: MT6739-DA.bin, MT6739_Android_scatter_emmc.txt, auth_sv5.auth, preloader_v2.bin, 各パーティション img, dafish7 の magisk_patched-25200_Lf52l.img
- `tools/firmware/Firmware/*.img` — 展開済み。system.img 2.5GB, userdata.img 4.0GB を含む
- `tools/SP_Flash_Tool_Linux/` — gesangtome ミラーの SP Flash Tool Linux 版。auth 対応必要なら次はこれで焼く

### 変更ファイル

- 更新: `WORKLOG.md`(このエントリ)
- 追加: `tools/firmware/Sunmiv2 stock with patch.rar`, `tools/firmware/Firmware/*`, `tools/SP_Flash_Tool_Linux/`
- 端末: 全 disable-user 済みパッケージリストは前回と同じ 17 個

### 次のTODO

1. Flutter インストール確認、`flutter doctor` を通す
2. `flutter create` で kiosk アプリの雛形作成 (`~/SunmiV2Hack/app/`)
3. flutter_sunmi_printer と camera パッケージを pubspec.yaml に追加
4. AndroidManifest に CATEGORY_HOME を追加して自作アプリを Launcher に
5. Sunmi Launcher (`com.woyou.launcher`) を disable
6. プリンタと カメラの動作確認、成功したら区切りでコミット
7. 途中で 再度 root 必要になったら `/data/local/tmp/mtk-su -c '/path/to/script.sh'` パターンで実行

### 学び

- **Sunmi 端末で永続 root を試すのは無理筋**。Magisk 焼きは Sunmi LK が絶対に許さない。mtk-su の一時 root で運用継続するのが唯一の道
- Sunmi Recovery「Recovery the system」は Sunmi 独自のリカバリ機能で、実質 Factory Reset。userdata を wipe するが boot が正しい純正に戻る。**次回ブリックしたら真っ先にこれを試す**
- SP Flash Tool + auth_sv5.auth + Sunmi 純正 firmware は「万一 Recovery でも救えない」ケースの保険。今回は使わずに済んだが、`tools/firmware/` に温存する
- 復旧を経て、**Sunmi V2 の書き換え耐性は POS 端末として非常に高い**ことが実地で確認できた。GSI で Android 上げは実質不可能

---

## 2026-07-01 17:35 (main) — Magisk 焼きで bootloop、経路詰まり

### 変更概要

「Sunmi V2 T5930 を kiosk 化」目的。Ubuntu 26.04 でツール一式(adb / mtkclient / Magisk / Flutter snap)を導入し、Phase 0/1/2 を完了。フル eMMC dump 取得+ dd 書き戻しリハーサル成功まで到達したが、**Magisk-patched boot を焼いた瞬間から bootloop**、以降 USB 経由の書き換え経路が全て塞がっている状態。

### ハードウェア/ソフトウェア確定情報

- SoC: **MediaTek MT6739** (Cortex-A53 4c ARMv7l モード, ro.mediatek.platform=MT6739)
- RAM: 909680 kB (実質 1GB モデル)
- eMMC: mmcblk0 = 7,471,104 kB (8GB)、パーティション 35 個
- kernel: 4.4.22+ (2026-03-19 ビルド)
- Android 7.1.1, SUNMI ROM 2.12.1 (最新 OTA 済み), 2024-04 セキュリティパッチ
- USB serial: VB3920C924272
- build fingerprint: `alps/full_rlk6580_we_c_m/rlk6580_we_c_m:7.1.1/N6F26Q/1773911932:user/release-keys`
- ro.boot.verifiedbootstate=green, ro.boot.flash.locked=1
- プリンタサービス: `woyou.aidlservice.jiuiv5/sunmi.inner.pkg.service.PrinterService` (`SunmiPrinterService_v6.6.25`)
- Sunmi Launcher: `com.woyou.launcher/com.android.launcher3.Launcher`

### 各起動モードでの USB VID:PID

| 状態 | VID:PID | 備考 |
|---|---|---|
| Android 通常起動 | `0e8d:201d` MediaTek V2 | adb 疎通可能(USB debug ON 済) |
| 音量+ + USB | (Sunmi Recovery 画面表示) | **adb / USB 無効化されている** |
| 音量- + USB | `0e8d:20ff` MediaTek Android | META mode。mtkclient は反応せず |
| 音量+ + 音量- + USB | `0e8d:20ff` | 同上 |
| bootloop 中(現状) | `0e8d:2008` MediaTek "Cyrus Technology CS 24" | Sunmi 独自 Preloader-like、mtkclient handshake 通らず |
| BROM(狙い) | `0e8d:0003` | **どのボタンコンボでも入れていない** |

### mtkclient で試したこと(全部タイムアウト or handshake 失敗)

- `printgpt --pid 0x20ff` / `--pid 0x2008` : タイムアウト
- `crash --pid 0x2008` : タイムアウト
- `plstage --pid 0x2008` : タイムアウト
- `meta FASTBOOT --pid 0x20ff` : タイムアウト
- `meta ADVEMETA --pid 0x20ff` : タイムアウト
- `--stock --pid 0x2008 printgpt` : タイムアウト
- `--preloader dump/preloader-boot0.img --pid 0x2008 printgpt` : タイムアウト

**結論: Sunmi ROM 2.12.1 は BROM/Preloader/META mode 全て塞ぎ、Sunmi Recovery からも adb を無効化している。書き換え経路は 3-boot 前は Android 起動状態の mtk-su 経路のみだった。**

### Phase 進捗

- **Phase 0**: 完了。adb / mtkclient (venv, pycryptodomex 手動追加要) / Magisk v30.7 / Flutter snap / udev rules 全部揃った
- **Phase 1**: 完了。全 property, 全 partition レイアウト, パッケージ列挙, プリンタサービス構造把握
- **Phase 2**: 完了。34 パーティション計 3.0 GB を `dump/*.img` に保存、SHA256SUMS 生成、書き戻しリハーサル成功(SHA256 一致で書き込みパスの正常動作を実証)
- **Phase 3-1 root 化**: 一時 root は成功していた(pikpikcu/mtk-su_r20 の arm バイナリで CVE-2020-0069、SELinux も permissive に)
- **Phase 3-2 Sunmi 剥がし**: 17 パッケージ disable-user 完了(UI/telemetry/OTA/decision/Store/中国語 IME)、reboot 後 Sunmi プロセス 12 → 3
- **Phase 3-3 永続 root(Magisk)**: patch script は成功、`new-boot.img` 生成、書き戻しも 6.088s で完了。**reboot したら bootloop**
- **Phase 4 (GSI)**: 未着手、現状無理

### 得られた重要な観察

1. **Sunmi Recovery のメニューは 3 項目のみ**: "Recovery the system" / "Factory data reset" / "Restart"。 sideload なし
2. **CVE-2020-0069 (mtk-su) は 2026-03 の Sunmi kernel でも刺さる**。まだパッチ済みではなかった
3. **書き戻しリハーサルは動いた**が、これは「同じ内容の書き戻し」であって、「Magisk-patched の内容」が Sunmi LK の verification に通るかは別の話 = 通らなかった
4. **Magisk `boot_patch.sh` の "Failed to patch"** が 3 回出ていた(dtbs パッチ失敗)= Sunmi の boot 内 dtb がスタンダードではない可能性

### 主な変更ファイル

- 新規: `WORKLOG.md`, `logs/current-state.md`, `logs/getprop-initial.txt`, `logs/packages-system.txt`, `logs/dump-progress.log`, `logs/magisk-patch.log`, ほか多数
- 新規(バイナリ): `dump/*.img` (34 パーティション + preloader boot0/1, 計 3.0 GB), `dump/new-boot-magisk.img` (Magisk-patched, 焼いた本人)
- 新規(ツール): `tools/mtkclient/`, `tools/mtk-su/`, `tools/Magisk.apk`, `tools/magisk-extracted/`, `tools/magisk-cli/`
- スクリプト各種: `/tmp/dump-all.sh`, `/tmp/disable-sunmi-1.sh`, `/tmp/magisk-patch.sh`, `/tmp/flash-magisk-boot.sh`, `/tmp/restore-boot.sh` など(全部 `/data/local/tmp/` にも push 済み、端末側に残っている)

### 次のTODO

1. **「Recovery the system」を試す**: Sunmi 純正の system 復元機能に賭ける。Magisk が上書きされて Sunmi 純正に戻る想定
2. **他アプローチの深掘り検索**: Cyrus Technology 0e8d:2008 プロトコル / Sunmi V2 test point 位置 / SP Flash Tool Windows で 2008 mode 対応するか
3. Recovery ダメなら:
   - bootloop 放置で電池切れ → 完全放電状態から音量+/- + USB で BROM が入れるか
   - 物理 test point 短絡で BROM 強制進入(破壊リスク)
   - Sunmi サポート送付修理

### 学び(次回同種プロジェクトで活かす)

- Sunmi の 2.12.1 は Android 7.1 世代の緩さと、Sunmi 独自の書き換え防御の組み合わせ。最新 OTA を当ててしまうと Magisk 焼きが実質不可能になるので、**OTA 前に root を確定させるべき**
- 「AVB1 でも起動できる」と勝手に期待して焼くのは危険。**書き戻しリハーサルの成功は "書き込みが可能" の証明であって "自作イメージが起動する" の証明ではない**
- mtk-su は Android 起動状態が前提。boot 焼き失敗で bootloop に入ったら mtk-su は無力
- 書き換え直前の最終確認として、**boot に純正 boot.img を焼き戻して通常起動できることは実証済みだったので、今の状態も理論上は「Magisk-patched の起動が失敗しているだけ」で復旧余地は残っている**
