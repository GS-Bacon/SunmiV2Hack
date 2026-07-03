# Non-open-source vendor blobs
$(call inherit-product-if-exists, vendor/sunmi/v2/v2-vendor.mk)

# A/B は使わない (Sunmi V2 は non-A/B partition layout)
AB_OTA_UPDATER := false

# ---- Overlays ----
DEVICE_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay

# ---- Screen density (Sunmi V2 stock ro.sf.lcd_density=320) ----
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

# ---- Init & fstab ----
PRODUCT_PACKAGES += \
    fstab.mt6739 \
    init.mt6739.rc

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/fstab.mt6739:$(TARGET_COPY_OUT_RAMDISK)/fstab.mt6739 \
    $(LOCAL_PATH)/rootdir/etc/init.mt6739.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.mt6739.rc \
    $(LOCAL_PATH)/rootdir/etc/init.sunmi.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.sunmi.rc

# ---- Health HAL (bat) ----
PRODUCT_PACKAGES += \
    android.hardware.health@2.0-impl \
    android.hardware.health@2.0-service

# ---- Audio HAL (stub、実 impl は Sunmi stock vendor blob 側) ----
PRODUCT_PACKAGES += \
    android.hardware.audio.effect@5.0-impl \
    android.hardware.audio@5.0-impl \
    audio.a2dp.default \
    audio.bluetooth.default \
    audio.primary.default \
    audio.usb.default \
    audio.r_submix.default

# ---- Graphics HAL ----
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.composer@2.1-impl \
    android.hardware.graphics.composer@2.1-service \
    android.hardware.graphics.mapper@2.0-impl \
    android.hardware.memtrack@1.0-impl \
    android.hardware.memtrack@1.0-service

# ---- WiFi HAL ----
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service \
    libkeystore-wifi-hidl \
    libkeystore-engine-wifi-hidl

# ---- Bluetooth HAL ----
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service

# ---- Sensors ----
PRODUCT_PACKAGES += \
    android.hardware.sensors@1.0-impl \
    android.hardware.sensors@1.0-service

# ---- Thermal ----
PRODUCT_PACKAGES += \
    android.hardware.thermal@1.0-impl \
    android.hardware.thermal@1.0-service

# ---- Camera (POS 端末では minimal) ----
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.4-impl \
    android.hardware.camera.provider@2.4-service

# ---- Configstore ----
PRODUCT_PACKAGES += \
    android.hardware.configstore@1.1-service

# ---- Trust HAL (LineageOS) ----
PRODUCT_PACKAGES += \
    lineage.trust@1.0-service

# ---- Permissions (base) ----
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.software.secure_lock_screen.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/android.software.secure_lock_screen.xml

# ---- Properties (system-side) ----
$(call inherit-product, $(LOCAL_PATH)/system.prop.mk)
