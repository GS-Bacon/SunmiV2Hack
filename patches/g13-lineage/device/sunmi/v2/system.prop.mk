# System properties for Sunmi V2 (pre-Treble, MT6739)
PRODUCT_PROPERTY_OVERRIDES += \
    ro.mediatek.platform=MT6739 \
    ro.mediatek.chip_ver=S01 \
    ro.hardware=mt6739 \
    ro.hwui.disable_scissor_opt=true \
    persist.mtk.wcn.combo.chipid=0x6739 \
    ro.telephony.default_network=9 \
    ro.telephony.sim.count=2 \
    ro.sf.lcd_density=320 \
    ro.sf.hwrotation=0

# Dalvik heap tuning (Sunmi V2 stock 実測)
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.heapstartsize=16m \
    dalvik.vm.heapgrowthlimit=192m \
    dalvik.vm.heapsize=384m \
    dalvik.vm.heaptargetutilization=0.75 \
    dalvik.vm.heapminfree=512k \
    dalvik.vm.heapmaxfree=8m

# Dex2oat memory (get-product-default-property 経由で読まれる)
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    dalvik.vm.image-dex2oat-Xms=64m \
    dalvik.vm.image-dex2oat-Xmx=64m \
    dalvik.vm.dex2oat-Xms=64m \
    dalvik.vm.dex2oat-Xmx=512m
