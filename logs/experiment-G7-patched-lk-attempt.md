# Experiment G-7: Patched LK 焼き試行 → bootloop → 復旧未完了で今日終了

**日付**: 2026-07-02 00:20 - 01:15  
**結果**: 焼きは成功、boot 失敗、復旧未完了(端末は BROM に居るが mtkclient handshake 通らず)

## 試したこと

### 1. Padded patched LK 焼き(seccfg 変更なし)

前回 F-4 の 508KB patched LK を 0x00 で 1MB に padding → mtkclient で焼き成功。

- lk-patched.img: 508KB(security policy zeroed at offsets 0x40, 0x6c554-0x6c9ax の 41 bytes)
- padded to 1MB
- mtkclient で書き込み成功、SHA256 一致確認

### 2. Boot 観察

USB 抜き差し後: **0e8d:2000 preloader ↔ 切断の bootloop**(device address 018 → 050 と急上昇)
= preloader が patched LK を verify 拒否、F-5 と同じ症状。

**結論**: patched LK 単独では bootloop。seccfg unlock との組み合わせが次の候補。

### 3. seccfg unlock 単独試験(patched LK 焼き前)

- mtkclient `da seccfg unlock` 成功、offset 0x0C: `01` → `03`(SBOOT_STATE_UNLOCKED)、Sej HW crypto で hash 再計算
- reboot → **0e8d:20ff (META mode)** に落ちる、Android 起動せず

**発見**: Sunmi 純正 LK は unlocked seccfg 状態で「安全側」に META mode に落ちる実装。**seccfg unlock も Sunmi は考慮している**。

### 4. 復旧未完了

- 純正 seccfg 書き戻し成功(SHA256 一致)
- 純正 LK 書き戻し途中で bootloop 状態が USB を撹乱
- 最終的に device は **0e8d:0003 BROM** に落ちる
- **mtkclient crash mode 1 回成功**、Target Config を extract:
  ```
  SBC enabled: False
  SLA enabled: False
  DAA enabled: False
  Mem read/write auth: False
  BROM payload addr: 0x100a00
  DA payload addr: 0x201000
  Var1: 0xb4
  ```
- しかし後続の mtkclient セッションで **preloader.init() が完了せず**、"Please disconnect, start mtkclient and reconnect" ループに突入

## 大発見

### BROM Target Config 全 False

MediaTek MT6739 の BROM は **chip level では auth 完全 disabled**(SBC/SLA/DAA/mem auth 全部 False)。**Sunmi の verify は BROM ではなく preloader/LK 内部実装**、これが「BROM は無認証なのに焼いた patched LK が preloader に拒否される」現象の根本原因。

つまり G-1 で分析した preloader 内 `fcn.0021277c` の SBC state 変数は **BROM eFuse とは独立**、preloader 側で seccfg lock_state + 独自ロジックで判定。

### 攻撃の新しい方針

前回撤退時と同様、Sunmi 独自 verify を突破する必要:

- **A**: preloader 自身を書き換え(BROM 認証は無いので可能)。ただし preloader は SBC 相当の内部整合性を持つ可能性、事前調査必要
- **B**: seccfg のより深い解析(critical_lock_state ではなく、preloader が読む他のフィールドを見つける)
- **C**: patched LK + seccfg unlock + kernel/system 一括で「別 vendor firmware」focus

## 未解決の復旧手順

現状 device は **BROM に居る + mtkclient handshake が通らない**状態。試すべき復旧手順(明日 fresh state で):

1. **完全放電後の再挑戦**: USB 抜いて 3-8 時間、バッテリー完全放電、SoC state clear
2. **mtkclient 別バージョン**: git pull で最新、または別 fork
3. **SP Flash Tool v5.1916 + fresh session**: 前回動作実績あり、mtkclient より stable
4. **Sunmi Recovery 音量+ + 電源 30 秒長押し**: 前回 WORKLOG 実績あり、hardware combo

## Files

- `/tmp/mtk-restoreX.log` — 復旧試行の複数ログ(ephemeral)
- `logs/experiment-G7-patched-lk-attempt.md`(このファイル)

## Session status

- G-0 〜 G-6: **完了、記録済み**
- G-7: **試行 → bootloop → 復旧未完了**、明日再挑戦
- G-8: pending

Sunmi V2 が現在の状態から復旧できる可能性は **95% 以上**(BROM が生きている限り mtkclient か SP Flash Tool のどちらかで書き込み可能)。**今日はここで区切り**、明日 fresh state で復旧 → G-7 の combined attack(patched LK + seccfg unlock + LK 側 unlock check bypass)に進む。
