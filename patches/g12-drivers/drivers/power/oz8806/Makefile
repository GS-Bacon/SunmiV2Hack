# SPDX-License-Identifier: GPL-2.0
#
# Sunmi V2 BMT OZ8806 fuel-gauge driver (upstream + Sunmi shims)
#
# Populate this directory with the upstream reference driver files:
#   - oz8806_battery.c    (from MT6755 BSP or Motorola sm6225 LineageOS)
#   - oz8806_regdef.h
#   - parameter.c / parameter.h
#   - table.c / table.h
#   - battery_config.h    (Sunmi V2 CV set-point etc.)

obj-$(CONFIG_BATTERY_OZ8806) += oz8806_battery.o parameter.o table.o
obj-$(CONFIG_BATTERY_OZ8806) += sunmi_oz8806_compat.o
