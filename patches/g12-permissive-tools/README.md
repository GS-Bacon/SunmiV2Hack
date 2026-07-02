# G-12 permissive boot 化ツール群

Sunmi V2 の boot.img ramdisk 内 sepolicy を「全 domain permissive」化する 2 ステップ手順。

## 手順

1. **`permissive-all.c` を build**(host, libsepol-dev 必要)

```bash
gcc -O2 -o permissive-all permissive-all.c /usr/lib/x86_64-linux-gnu/libsepol.a
```

2. **ramdisk 内 `sepolicy` を permissive 化**

```bash
./permissive-all rd/sepolicy sepolicy-permissive   # 1625/1657 types に permissive flag
```

3. **`default.prop` を編集**

```
ro.secure=0
ro.debuggable=1
```

4. **`cpio-replace.py` で cpio archive 内の該当 entry 差し替え**(size-changing 対応)

```bash
python3 cpio-replace.py ramdisk.cpio sepolicy sepolicy-permissive step1.cpio
python3 cpio-replace.py step1.cpio default.prop default-permissive.prop ramdisk-permissive.cpio
gzip -9 -n ramdisk-permissive.cpio       # → ramdisk-permissive.gz
```

5. **boot.img repack + header cmdline に `androidboot.selinux=permissive` 追加**

## 動作条件

- Android 7.1 user build 上で、`androidboot.selinux=permissive` は init 側の build type check で無視されるため、sepolicy binary 直改変が必要
- `getenforce` は Enforcing のまま(意図通り、log 表示は Enforcing)
- 全 domain permissive なので実質全アクセス許可、audit は permissive= 1 で通る
