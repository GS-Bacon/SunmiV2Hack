LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),v2)
# vendor blob は PRODUCT_COPY_FILES で直接配置するので、Android.mk 側は minimal
endif
