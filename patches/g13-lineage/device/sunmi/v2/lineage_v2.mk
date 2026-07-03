# AOSP base(PRODUCT_BOOT_JARS 等)を先に継承(Lineage config はこれを継承しないので明示)
$(call inherit-product, $(SRC_TARGET_DIR)/product/handheld_system.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/telephony_system.mk)

# Inherit some common Lineage stuff (pre-Treble device なので 32-bit only)
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Inherit device configuration
$(call inherit-product, $(LOCAL_PATH)/device.mk)

# Device identifier
PRODUCT_DEVICE := v2
PRODUCT_NAME := lineage_v2
PRODUCT_BRAND := SUNMI
PRODUCT_MODEL := V2
PRODUCT_MANUFACTURER := SUNMI

PRODUCT_GMS_CLIENTID_BASE := android-sunmi

# pre-Treble
PRODUCT_FULL_TREBLE_OVERRIDE := false
PRODUCT_SHIPPING_API_LEVEL := 25

TARGET_VENDOR := sunmi

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="V2-user 10 QQ3A.200805.001 eng.lineage $(shell date -u +%Y%m%d.%H%M%S) release-keys"

BUILD_FINGERPRINT := SUNMI/V2/V2:10/QQ3A.200805.001/$(shell date -u +%Y%m%d.%H%M%S):user/release-keys
