# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_BATTERY_OZ8806) += oz8806_battery.o parameter.o table.o
obj-$(CONFIG_BATTERY_OZ8806) += sunmi_oz8806_compat.o

# MTK 独自 header (mach/xxx.h、mt-plat/xxx.h) を認識させる
# drivers/misc/mediatek/Makefile と同じ path を再現
MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
