# G-12 Phase 0-A: bacondata workspace 構築

作成日: 2026-07-02

## Build machine

- Host: `bacondata` (SSH `bacon@100.105.139.80`)
- CPU: 4 core
- RAM: 31GB (available ~29GB)
- Disk: `/dev/sdb3` (system) 464GB total, 435GB free / `/dev/sda1` (Nextcloud data) 22TB, 22TB free
- Nextcloud 33.0.2.2 稼働中(apache2 + mariadb + redis-server + notify_push)

## Nextcloud 破壊禁止領域(作業前確認済)

| Path | 中身 |
|---|---|
| `/data/*/files/` | ユーザーファイル(bacon, misaki, Lilac 等) |
| `/data/__groupfolders/` | グループ共有 |
| `/var/www/nextcloud/` | Nextcloud 本体、config |
| MariaDB `nextcloud` DB | Nextcloud データベース |
| Apache2 / Redis / notify_push | 稼働中サービス |

作業前 baseline(全 active): apache2 / mariadb / redis-server / notify_push  
apt install 後も全 active を確認済。

## Workspace 構造

```
/home/bacon/sunmiandroid/
├── src/         # external git clone (Iscle BSP、K-touch i9 device tree 等)
├── kernel/      # Sunmi V2 用 kernel-4.4 tree(Iscle 由来を fork)
├── device/      # LineageOS device tree fork(Phase 3 で組み込み)
├── scratch/     # 中間成果(RE 用 vmlinux 等)
├── dumps/       # local SunmiV2Hack/dump/ から転送する partition dump
├── docs/        # bacondata 側の doc(local docs/ と対応)
├── logs/        # build log
└── out/         # 成果物(zImage 等、後で local に scp)
```

## 導入した build tools

apt package(dep 衝突なしで install、REMOVED ゼロ):

- `gcc-arm-linux-gnueabi` 13.3.0(cross compiler、arm32 targeting)
- `bison` 3.8.2、`flex` 2.6.4
- `libssl-dev`(kernel の RSA 署名関連)
- `ccache` 4.9.1(max 100GB、`~/.ccache/`)
- `lz4`、`zlib1g-dev`
- `git-lfs` 3.4.1
- `python-is-python3`(kernel build script が `/usr/bin/python` を呼ぶ場合の互換)
- `libncurses-dev`(menuconfig 用)
- `rsync`(既にあった)

Home 内独立導入:

- `~/bin/repo`(Google 公式 repo tool、Phase 3 で LineageOS sync に使う)
- `~/bin` を PATH に追加

Git config: `user.name=GS-Bacon`、`user.email=bacon0817.ix@gmail.com`

## Nextcloud 影響評価

- apt install シミュレーション時に REMOVED / 依存衝突ゼロを事前確認
- install 後の service status: apache2 / mariadb / redis-server / notify_push 全 active
- `sudo -l` で作業に必要な操作は許容範囲(`(ALL) NOPASSWD: ALL` あり)、ただし Nextcloud 領域には触らない方針を維持

## 再現手順

```bash
ssh bacon@100.105.139.80
mkdir -p /home/bacon/sunmiandroid/{src,kernel,device,scratch,dumps,docs,logs,out}
sudo apt-get install -y \
  gcc-arm-linux-gnueabi bison flex libssl-dev \
  ccache lz4 zlib1g-dev git-lfs \
  python-is-python3 libncurses-dev rsync
ccache -M 100G
mkdir -p ~/bin
curl -s https://storage.googleapis.com/git-repo-downloads/repo -o ~/bin/repo
chmod +x ~/bin/repo
grep -q "HOME/bin" ~/.bashrc || echo 'export PATH=$HOME/bin:$PATH' >> ~/.bashrc
```

## 次: Phase 0-C

Iscle BSP から MT6739 kernel-4.4 tree 抽出、arm32 で baseline zImage(printer/charger なし)build 通し。
