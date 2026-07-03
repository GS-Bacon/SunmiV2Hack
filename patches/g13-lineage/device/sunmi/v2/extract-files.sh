#!/bin/bash
#
# Sunmi V2 vendor blob extract script
#
# Phase 2-D で dump/system.img と dump/sunmi.img から Sunmi 独自 vendor blob を抽出、
# vendor/sunmi/v2/proprietary/ に配置する。
#
# 前提: bacondata の /home/bacon/sunmiandroid/dump/{system.img,sunmi.img} を mount 済
#       (offset 1024 で ext4 化して mount)
#
# Usage: ./extract-files.sh <SYSTEM_MOUNT_POINT>
#   例: ./extract-files.sh /mnt/sunmi-v2-system

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <SYSTEM_MOUNT_POINT>"
    echo "  例: $0 /mnt/sunmi-v2-system"
    exit 1
fi

SYSTEM_ROOT="$1"
DEVICE_TREE_DIR="$(dirname "$(readlink -f "$0")")"
VENDOR_TARGET_DIR="$(cd "$DEVICE_TREE_DIR/../../../" && pwd)/vendor/sunmi/v2/proprietary"

echo "[extract-files] SYSTEM_ROOT=$SYSTEM_ROOT"
echo "[extract-files] VENDOR_TARGET_DIR=$VENDOR_TARGET_DIR"

mkdir -p "$VENDOR_TARGET_DIR"/{vendor/lib/hw,vendor/bin/hw,vendor/etc/init/hw,system/bin,system/lib}

# ---- MTK HAL (vendor/lib/hw or system/lib/hw の両方あり得る) ----
for src in system/vendor/lib/hw system/lib/hw vendor/lib/hw; do
    for pattern in "gralloc.mt6739.so" "hwcomposer.mt6739.so" "memtrack.mt6739.so" \
                   "audio.primary.mt6739.so" "audio.a2dp.default.so" \
                   "sensors.mt6739.so" "camera.mt6739.so" \
                   "power.mt6739.so" "lights.mt6739.so" \
                   "bluetooth.default.so" "keystore.mt6739.so"; do
        if [ -f "$SYSTEM_ROOT/$src/$pattern" ]; then
            cp -v "$SYSTEM_ROOT/$src/$pattern" "$VENDOR_TARGET_DIR/vendor/lib/hw/"
        fi
    done
done

# ---- MTK common libs (vendor/lib or system/vendor/lib) ----
for pattern in "libcamera_client_mtk.so" "libmediahal_mtk.so" "libmtkjpeg.so" \
               "libaudio_param_parser-1.0.so" "libbluetooth_mtk.so" "libaal.so" \
               "libmtk-ril.so" "libwifi-hal-mtk.so" "libsunmi_hal.so"; do
    for src in system/vendor/lib system/lib vendor/lib; do
        if [ -f "$SYSTEM_ROOT/$src/$pattern" ]; then
            cp -v "$SYSTEM_ROOT/$src/$pattern" "$VENDOR_TARGET_DIR/vendor/lib/"
            break
        fi
    done
done

# ---- Sunmi 固有 binary ----
for pattern in "guard" "logo_customize"; do
    if [ -f "$SYSTEM_ROOT/system/bin/$pattern" ]; then
        cp -v "$SYSTEM_ROOT/system/bin/$pattern" "$VENDOR_TARGET_DIR/system/bin/"
    fi
done

# ---- MTK vendor init.rc 系 ----
for rc in init.mt6739.rc init.connectivity.rc init.project.rc init.aee.rc \
          init.ril.rc init.trustonic.rc init.common_svc.rc init.microtrust.rc \
          init.sensor.rc init.modem.rc init.mt6739.usb.rc ueventd.mt6739.rc; do
    for src in system/etc/init/hw system/etc; do
        if [ -f "$SYSTEM_ROOT/$src/$rc" ]; then
            cp -v "$SYSTEM_ROOT/$src/$rc" "$VENDOR_TARGET_DIR/vendor/etc/init/hw/"
            break
        fi
    done
done

# ---- Sunmi 独自 init.sunmi.rc は ramdisk 側にあり、既に本 device tree の rootdir/etc/ に配置済 ----

echo "[extract-files] 完了: $(find "$VENDOR_TARGET_DIR" -type f | wc -l) ファイル extract"
