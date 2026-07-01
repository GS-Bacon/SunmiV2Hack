# Sunmi V2 T5930 downgrade + Magisk 焼きプラン

## Context(なぜやる)

**目的**: 古い Sunmi ROM に downgrade → dafish7 の Magisk-patched boot を焼く → **永続 root を実現する**。

**根拠**:

- 手元の fishybytes firmware は **2021 年 3 月ビルド** = 現行の Sunmi ROM 2.12.1(2026 年 3 月ビルド)より約 5 年古い
- dafish7 は README で「この firmware セット + Magisk-patched boot + SP Flash Tool の Format all + Download で永続 root 化に成功」と明記 → **2021 世代の Sunmi LK は Magisk-patched boot を通していた**証拠
- 現行 2.12.1 で Magisk-patched boot を焼いたら bootloop → 今日実測、**最新 OTA で LK が verify を強化した**のが原因の可能性が高い
- **downgrade できれば 2021 世代の LK に戻る → dafish7 手順が動く**可能性 30-40%
- 復旧経路は Sunmi Recovery の「Recovery the system」で確保(前回実証、現行 stock に戻せる)

**投資対効果**:

- 成功率 30-40%
- 時間: 1-2 時間
- 副次価値: SP Flash Tool + auth 経路の動作確認、Sunmi 独自防御の年代別変化の実測
- 失敗時のコスト: 20-30 分の復旧(Recovery the system → 再セットアップ)

**A-2/C の実験より明らかに投資対効果が良い**(あちらは 99% 失敗の予想検証、こちらは実現の可能性がある)。

---

## 手元にある武器

- `tools/firmware/Sunmiv2 stock with patch.rar`(1.29GB、fishybytes 経由の dafish7 パッケージ)
- 展開済み `tools/firmware/Firmware/` (2021 年 3 月ビルドの全パーティション img)
  - `MT6739-DA.bin` (15MB) — Sunmi 対応 Download Agent
  - `MT6739_Android_scatter_emmc.txt` (13KB) — パーティション定義
  - `preloader_v2.bin` (112KB) — 2021 世代 Preloader
  - `lk.bin` / `lk2.bin` — 2021 世代 LK
  - `boot.img` / `recovery.img` / `system.img` / `cache.img` / `userdata.img` — 2021 世代の全パーティション
- `tools/firmware/auth_sv5.auth` (2.2KB) — Sunmi Preloader との認証
- `tools/firmware/magisk_patched-25200_Lf52l.img` (10MB) — dafish7 の Magisk-patched boot(Magisk 25200 世代)
- `tools/SP_Flash_Tool_Linux/` — gesangtome ミラーの SP Flash Tool Linux 版
- `dump/*.img` — 現行 2.12.1 stock の完全 dump(復旧の保険、SP Flash Tool でも書ける)

---

## リスク階層

### 致命的リスク(物理ブリック)

- **preloader 書き込み中の電源喪失** → SoC が起動できない状態
- **書き込み中に USB ケーブルを抜く**
- **バッテリー低残量での焼き**

対策:

- 事前満充電(バッテリー 90% 以上を確認、AC 電源接続維持)
- 書き込み中の USB / 電源に触らない
- 書き込み中は端末を移動しない

### 復旧可能リスク(bootloop、20-30 分の手戻り)

- **LK rollback protection** で古い LK を蹴る → 起動しない
- **Sunmi の auth 認証が更新済み** で 2021 世代 auth を蹴る → 焼けない
- **Format all で userdata wipe** される → 再セットアップ必須

対策:

- Sunmi Recovery の「Recovery the system」で復旧(実証済み)
- 復旧手順は `docs/system-swap-experiment-plan.md` に詳細記載

### Not-to-do リスト

- SP Flash Tool のパーティションチェックを外して独自組み合わせで焼かない(dafish7 手順に忠実)
- lk 書き込みを個別に外さない(preloader ↔ lk のバージョン整合性が壊れる)
- 焼き途中で SP Flash Tool を Kill しない
- 端末電源キー・音量キーを触らない(自動進行を邪魔しない)

---

## Phase D-0: 事前準備(非破壊)

### 手順

1. **バッテリー確認**: 端末側で 90% 以上を確認、USB 給電接続
2. **fishybytes firmware の完全性検証**:
   - `sha256sum tools/firmware/Firmware/*` を取り、`logs/firmware-sha256.log` に保存
3. **SP Flash Tool Linux 版の依存関係チェック**:
   - `ldd tools/SP_Flash_Tool_Linux/flash_tool` で欠損 lib 確認
   - Qt / X11 が必要になる可能性、apt で追加
4. **現状スナップショット**:
   - 現行 stock の `getprop | tee logs/getprop-before-downgrade.txt`
   - パッケージリスト `pm list packages -s | tee logs/packages-before-downgrade.txt`
   - Magisk / mtk-su 動作確認 `mtk-su -c id` で uid=0
5. **復旧経路の再確認**:
   - Sunmi Recovery に入れるか(音量+ + 電源キー)を心理的に再確認、実行はしない

### 中止判断ライン D-0

- SP Flash Tool が起動しない → apt で依存追加、それでも起動しない場合は Windows VM 経路を検討 or 中止
- バッテリー 70% 以下 → 事前充電、当日中止

---

## Phase D-1: SP Flash Tool 疎通確認(非破壊)

### 手順

1. SP Flash Tool GUI を起動
2. **Scatter file をロード**: `tools/firmware/Firmware/MT6739_Android_scatter_emmc.txt`
3. **Download Agent をロード**: SP Flash Tool 側で `MT6739-DA.bin` を認識(通常は自動)
4. **Authentication ファイルをロード**: `tools/firmware/auth_sv5.auth`
5. Download モードで「**Download Only**」を選択(Format all + Download はまだ選ばない)
6. 何もチェックせず「Download」ボタン
7. **端末を電源長押しで shutdown、USB を抜く**
8. 端末を電源長押し 15 秒 → 完全 OFF
9. USB を PC に挿す(ボタン不要、dafish7 手順)
10. SP Flash Tool の進捗を観察:
    - **Preloader 認識**(0e8d:2008 が SP Flash Tool 側に見える)
    - **Auth handshake 成功**(緑バー始まる)
    - **成功**: 「Download OK」表示
    - **失敗**: エラーコード表示、ログ確認

### 中止判断ライン D-1

- Preloader 認識せず → 中止、mtkclient と同じ失敗
- Auth 拒否 → 中止、2021 世代 auth は現行で無効(=downgrade 経路そのものが死んだ)
- Download 途中エラー → 詳細確認、致命的なら中止+復旧

---

## Phase D-2: 部分焼きテスト(低リスク)

### 手順

D-1 で疎通確認できた場合のみ実行:

1. 「**Download Only**」モードのまま
2. パーティション選択で **logo だけチェック**(他は外す)
3. Download 実行
4. 端末は電源 OFF から USB 挿し直し(D-1 と同じ手順)
5. logo だけ焼ける(実験的、5-10 秒)
6. 完了後、端末を通常起動 → 通常の Sunmi ロゴ表示か、書き換えた logo 表示か観察
   - Sunmi ロゴ表示: SP Flash Tool 経路で logo 焼き成功、SP Flash Tool 経路は動く
   - 起動しない: 中止、復旧

### 中止判断ライン D-2

- 焼けない → 中止
- 焼けたが起動しない → Recovery the system で復旧

---

## Phase D-3: 本番 Format all + Download

### 手順

D-2 で焼きパスが動いたら実行:

1. SP Flash Tool で「**Format all + Download**」を選択
2. **boot パーティションを置き換え設定**:
   - デフォルトの `boot.img` を右クリック → Change → `magisk_patched-25200_Lf52l.img` を指定
3. 全パーティションをチェック
4. **Download** ボタン
5. 端末電源 OFF、USB 挿し直し(dafish7 手順)
6. 5-10 分の焼きプロセスを完全に放置
7. 「Download OK」表示待つ

### 中止判断ライン D-3

- 途中エラー → **絶対に SP Flash Tool を kill しない**、進捗を観察
- Auth エラー → 中止
- 完了しない状態が 30 分以上続く → 端末側の電源状況を確認(USB は抜かない)

---

## Phase D-4: 起動確認 + Magisk 動作テスト

### 手順

1. USB を抜く(ここは抜いてよい、焼き完了後なので)
2. 電源キー短押しで起動
3. Sunmi Welcome / setup wizard が表示されるはず
4. 初期セットアップ完了(Wi-Fi 接続、Sunmi クラウド skip)
5. 開発者オプション → USB debug ON
6. USB 再接続、`adb devices` で認識
7. **Magisk apk をインストール**: `adb install tools/Magisk.apk`
8. Magisk app 起動 → 「Setup」プロンプト → OK → reboot が要求される場合
9. reboot 後、`adb shell 'su -c id'` → **uid=0 なら Magisk 永続 root 成功**
10. `getprop ro.build.date` で 2021 年 3 月ビルドに戻っているか確認

### 中止判断ライン D-4

- 起動しない → Recovery the system で現行 2.12.1 に戻る、downgrade 経路失敗
- 起動するが Magisk が動かない → dafish7 の Magisk-patched が現行 apk と互換ない可能性、別バージョンの apk で試す or 諦める

---

## Phase D-5: 成功時/失敗時のフォロー

### 成功時

- 2021 年 3 月ビルドの Sunmi ROM + Magisk 永続 root
- Flutter kiosk 化を再開(今度は Magisk で SELinux permissive 化なども可能)
- **重要**: Sunmi OTA を Magisk で永久 disable(現行 2.12.1 に戻さないように):
  - `com.sunmi.ota` を pm uninstall(disable ではなく削除)
  - ネットワーク側でも Sunmi OTA サーバへの通信をブロックする方が確実
- 現状 dump を Magisk 適用後の状態で更新

### 失敗時

- Sunmi Recovery → Recovery the system で 2.12.1 stock に戻る
- 初期セットアップ → USB debug ON → adb 疎通
- mtk-su 再 push、Sunmi 剥がし再適用、Flutter kiosk 化(元の路線)に戻る
- WORKLOG に「downgrade 経路失敗、Sunmi の 2.12.1 では auth が更新済み」等を記録

---

## 記録するもの(実験ログ)

- `logs/downgrade-D0-preflight.log`: バッテリー、SHA256、SP Flash Tool 起動状況
- `logs/downgrade-D1-handshake.log`: Preloader 認識、Auth 結果
- `logs/downgrade-D2-logotest.log`: 部分焼きテスト結果
- `logs/downgrade-D3-flashall.log`: 本番焼きのタイムスタンプ、Download OK / エラー
- `logs/downgrade-D4-boot.log`: 起動時間、Magisk root 確認
- `WORKLOG.md`: 各 Phase の要約(成否+得られた情報)

---

## 実行前チェックリスト

- [ ] 端末バッテリー 90% 以上
- [ ] USB ケーブルがしっかり接続、途中で抜けない
- [ ] `tools/firmware/Firmware/` の全ファイル存在確認
- [ ] `dump/*.img` の SHA256SUMS 検証(万一の復旧素材)
- [ ] SP Flash Tool Linux 版が起動できる
- [ ] Sunmi Recovery の入り方を physical に再確認(音量+ + 電源キー)
- [ ] 現状の kiosk 化状態(disable-user 17 個)は失われるが問題ない(復旧後にスクリプト再適用で戻せる)

---

## 全体の予想時間

- D-0(準備): 5-10 分
- D-1(疎通): 5-10 分
- D-2(部分焼き): 5-10 分
- D-3(本番): 15-25 分(実焼き時間 + 端末操作)
- D-4(確認 + Magisk 設定): 15-25 分
- **成功パス合計: 45-80 分**
- 失敗時の復旧: 20-30 分
- **最悪合計: 2 時間**

---

## 判定基準(成功/失敗の切り分け)

### 成功条件(全部満たしたら「downgrade + Magisk 永続 root」達成)

1. 端末が 2021 年 3 月ビルドの Sunmi ROM で通常起動する
2. `getprop ro.build.date.utc` が 2021 年頃を返す
3. Magisk app が「Installed」表示
4. `su -c id` で uid=0
5. reboot しても 3, 4 が維持される
6. プリンタサービスが動作している(`dumpsys activity services woyou.aidlservice.jiuiv5` で active)

### 中止判断(どこかで満たされたら実験終了)

- SP Flash Tool が preloader と通信できない(D-1)
- Auth が拒否される(D-1)
- 焼ききれない(D-3)
- 焼けたが起動しない(D-4)
- Magisk がインストールできない or root 取れない(D-4)
