// SPDX-License-Identifier: GPL-2.0
/*
 * sunmi_oz8806_compat.c
 *
 * Sunmi V2 compatibility shims over the upstream BMT OZ8806 driver.
 * Adds:
 *   - oz8806_get_battry_current : typo alias for OEM userspace/drivers
 *                                 (dmesg + kallsyms preserve the typo)
 *   - oz8806_get_boot_up_time   : synonym for oz8806_get_power_on_time
 *   - sunmi_constant_voltage    : module_param used in check_charger_full path
 *                                 (dmesg-printer.txt shows 8438000)
 *
 * Rationale: the shipped Sunmi V2 kernel exports these three symbols in addition
 * to the upstream MT6755 driver surface. Any Sunmi-side charger/thermal service
 * that binds by symbol name would fail to load without them.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>

/* Bring in upstream prototypes */
extern int             oz8806_get_battery_current(void);
extern unsigned long   oz8806_get_power_on_time(void);

/* Charger CV set-point in microvolts.
 * Sunmi V2 ships with 8.438 V (2-cell pack, ~4.219 V/cell).
 * Kernel command line or module param can override for other SKUs. */
int sunmi_constant_voltage = 8438000;
EXPORT_SYMBOL(sunmi_constant_voltage);
module_param(sunmi_constant_voltage, int, 0644);
MODULE_PARM_DESC(sunmi_constant_voltage,
		 "Battery constant-voltage set-point in microvolts (default 8438000)");

/* OEM typo alias — DO NOT rename. Multiple in-tree callers and userspace
 * services on Sunmi V2 depend on the mis-spelled export name. */
int oz8806_get_battry_current(void)
{
	return oz8806_get_battery_current();
}
EXPORT_SYMBOL(oz8806_get_battry_current);

unsigned long oz8806_get_boot_up_time(void)
{
	return oz8806_get_power_on_time();
}
EXPORT_SYMBOL(oz8806_get_boot_up_time);

MODULE_DESCRIPTION("Sunmi V2 OZ8806 compatibility shims");
MODULE_LICENSE("GPL v2");

/*
 * wake_up_bat_bmu(): MTK-side charger wakes the battery BMU. On Sunmi V2
 * kernel 4.4 tree, this symbol is not exported by mtk_charger; the
 * upstream MT6755 driver uses it inside #ifdef MTK_MACH_SUPPORT to force
 * a battery-callback wakeup. Provide a no-op stub — the periodic
 * battery_work in oz8806 already handles the polling case.
 */
void wake_up_bat_bmu(void) { }
EXPORT_SYMBOL(wake_up_bat_bmu);
