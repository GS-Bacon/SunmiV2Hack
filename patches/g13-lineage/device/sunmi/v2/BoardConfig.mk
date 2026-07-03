DEVICE_PATH := device/sunmi/v2

# ---- Arch (32-bit ARM 一貫、Sunmi V2 stock は armeabi-v7a only) ----
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := generic
TARGET_CPU_SMP := true

TARGET_BOARD_PLATFORM := mt6739
TARGET_BOOTLOADER_BOARD_NAME := mt6739
TARGET_NO_BOOTLOADER := true

# ---- Kernel (Sunmi stock zImage 温存) ----
TARGET_KERNEL_ARCH := arm
TARGET_KERNEL_HEADER_ARCH := arm
BOARD_KERNEL_IMAGE_NAME := zImage
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/zImage
TARGET_NO_KERNEL := false

BOARD_KERNEL_CMDLINE := bootopt=64S3,32S1,32S1 buildvariant=user androidboot.selinux=permissive
BOARD_KERNEL_BASE := 0x40008000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_TAGS_OFFSET := 0x0df88000
BOARD_RAMDISK_OFFSET := 0x04ff8000
BOARD_MKBOOTIMG_ARGS := --base $(BOARD_KERNEL_BASE) \
    --pagesize $(BOARD_KERNEL_PAGESIZE) \
    --ramdisk_offset $(BOARD_RAMDISK_OFFSET) \
    --tags_offset $(BOARD_KERNEL_TAGS_OFFSET) \
    --header_version 0

BOARD_BOOTIMG_HEADER_VERSION := 0

# ---- Partition layout (Sunmi stock 実測値) ----
BOARD_BOOTIMAGE_PARTITION_SIZE := 25165824
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 62914560
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 2684354560
BOARD_USERDATAIMAGE_PARTITION_SIZE := 8589934592
BOARD_FLASH_BLOCK_SIZE := 131072

BOARD_HAS_LARGE_FILESYSTEM := true
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := false

# ---- SAR 無効 (Sunmi V2 は従来型 boot + system 分離) ----
BOARD_BUILD_SYSTEM_ROOT_IMAGE := false
BOARD_USES_RECOVERY_AS_BOOT := false
TARGET_NO_RECOVERY := true

# ---- pre-Treble 継続 (/system/vendor 統合) ----
PRODUCT_FULL_TREBLE_OVERRIDE := false
TARGET_COPY_OUT_VENDOR := system/vendor
BOARD_VNDK_VERSION := current

# ---- SELinux (G-12 継承、cmdline permissive fallback) ----
BOARD_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy

# ---- AVB / dm-verity は G-12 実測に基づき無効 ----
BOARD_AVB_ENABLE := false
BOARD_BUILD_DISABLED := false

# ---- Recovery ----
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.mt6739
TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"

# ---- SoC-specific: MTK MT6739 ----
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_MTK := true
BOARD_HAS_MTK_HARDWARE := true

# ---- Audio (Android 10 は XML 前提、legacy .conf 非対応) ----
USE_XML_AUDIO_POLICY_CONF := 1

# ---- Malloc ----
MALLOC_SVELTE := true

# ---- Manifest (pre-Treble なので minimal) ----
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/manifest.xml
DEVICE_MATRIX_FILE := $(DEVICE_PATH)/compatibility_matrix.xml

# ---- Board name for build ----
TARGET_SCREEN_HEIGHT := 800
TARGET_SCREEN_WIDTH := 480

# Include vendor board config if present (extracted vendor blobs)
-include vendor/sunmi/v2/BoardConfigVendor.mk
