# Sunmi V2 T5930 現状把握サマリ (2026-07-01)

## ハードウェア確定
- SoC: **MediaTek MT6739** (Cortex-A53 4c ARMv7l モード)
- RAM: **888,868 kB (実質 1GB モデル)** ← Android 10+ GSI は絶望的
- eMMC: mmcblk0 = 7,471,104 kB (8GB)、パーティション 35 個
  - p32 (2,621,440 kB = 2.5GB) → system 相当
  - p33 (442,368 kB) → cache 相当
  - p34 (3,964,911 kB = 3.8GB) → userdata 相当
- Wi-Fi chip: CONSYS_MT6739 (mediatek 内蔵)

## ソフトウェア
- Android 7.1.1, kernel 4.4.22+
- Build fingerprint: `alps/full_rlk6580_we_c_m/rlk6580_we_c_m:7.1.1/N6F26Q/1773911932`
- SUNMI ROM 2.12.1 (2026-03-19 ビルド、最新 OTA 済み)
- SELinux mode: 不明(root で確認要)
- USB serial: VB3920C924272

## 重要パッケージ判別
### 絶対残す
- `woyou.aidlservice.jiuiv5` ← **プリンタ AIDL サービス本体**、flutter_sunmi_printer が bind
- `com.sunmi.printer.firmware` ← プリンタファーム管理
- `com.sunmi.baseservice`, `com.sunmi.sunmiopenservice` ← Sunmi SDK 基盤
- `com.sunmi.sidekey` ← ハードキー処理(スキャナ物理ボタン等)
- `com.android.settings`, `com.android.systemui`, `com.mediatek.*`

### 消す候補
- `com.woyou.launcher` ← Sunmi ホーム(自作アプリを Home に置くために削除)
- `com.sunmi.welcome`, `com.sunmi.wallpaper`, `com.sunmi.gallery`
- `com.sunmi.usercenter`, `com.sunmi.newuserfeedback`, `com.sunmi.agreement`
- `com.sunmi.ota` ← 勝手に更新されないように
- `com.sunmi.remotecontrol.pro` ← Sunmi サーバに繋がる可能性
- `com.sunmi.monitor`, `com.sunmi.toolbox.log.v2` ← テレメトリ系
- `woyou.market` ← Sunmi App Store
- `com.sunmi.payment`, `com.sunmi.payment.mobile` ← 決済(不要なら)
- `com.sunmi.input.pinyin` ← 中国語 IME

### 判断保留
- `com.sunmi.dataService`, `com.sunmi.instruction`
- `com.sunmi.toolbox`, `com.sunmi.documentsui`
- `sunmi` (system 統合?)
