// SPDX-License-Identifier: GPL-2.0
/*
 * odm_printer_gpio.c
 *
 * Sunmi V2 (Huaqin ODM) thermal printer GPIO/IRQ driver.
 * Clean-room re-implementation from kallsyms + RE data (G-12).
 *
 * Provides:
 *   /sys/printer_pin/printer/ with 11 sysfs attributes controlling 7 GPIOs
 *   and 2 IRQ lines that gate SPI printer TX (RTS/CTS handshake).
 *
 * Companion driver: drivers/misc/spi_printer.c
 * DT binding:       compatible = "summi,printer_gpio" (typo preserved
 *                   from OEM DTS shipped on Sunmi V2 T5930)
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>       /* TASK_INTERRUPTIBLE, TASK_KILLABLE, schedule() */
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/kernel.h>

struct printer_gpio_data {
	/* pdata offsets are inferred from RE (scratch/g12-printer-re/addresses.txt) */
	int gpio_cts;            /* +0x00 input, irq0 */
	int gpio_rts;            /* +0x04 input, irq1 */
	int gpio_sleep;          /* +0x08 output */
	int gpio_resume;         /* +0x0c output */
	int gpio_mcu_reset;      /* +0x10 output */
	int gpio_lvl_en;         /* +0x14 output */
	int gpio_pwr_en;         /* +0x18 output */
	u32 block_rts_flag;      /* +0x1c 0=blocked, 1=free */
	u32 block_cts_flag;      /* +0x20 */
	wait_queue_head_t rts_wq;
	wait_queue_head_t cts_wq;
	int irq0;
	int irq1;
	struct kobject *root_kobj;   /* /sys/printer_pin */
	struct kobject *printer_kobj;/* /sys/printer_pin/printer */
};

static struct printer_gpio_data *g_pdata;

/* ---------- GPIO output attrs (kstrtoint → gpio_set_value) ---------- */
#define GPIO_OUT_ATTR(name, field) \
static ssize_t name##_show(struct device *dev, struct device_attribute *a, char *buf) \
{ \
	return sprintf(buf, "%d\n", gpio_get_value(g_pdata->field)); \
} \
static ssize_t name##_store(struct device *dev, struct device_attribute *a, \
			    const char *buf, size_t count) \
{ \
	int v; \
	if (kstrtoint(buf, 0, &v)) return -EINVAL; \
	gpio_set_value(g_pdata->field, !!v); \
	return count; \
} \
static DEVICE_ATTR_RW(name)

GPIO_OUT_ATTR(power,  gpio_pwr_en);
GPIO_OUT_ATTR(lvl_en, gpio_lvl_en);
GPIO_OUT_ATTR(resume, gpio_resume);
GPIO_OUT_ATTR(sleep,  gpio_sleep);
GPIO_OUT_ATTR(reset,  gpio_mcu_reset);

/* ---------- GPIO input flag attrs (RW per RE: mode 0664, show + store-stub) ---------- */
static ssize_t busy_rts_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sprintf(buf, "%d\n", gpio_get_value(g_pdata->gpio_rts));
}
static ssize_t busy_cts_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sprintf(buf, "%d\n", gpio_get_value(g_pdata->gpio_cts));
}

/* ---------- block_rts/cts: block until IRQ frees flag ---------- */
static ssize_t block_rts_show(struct device *dev, struct device_attribute *a, char *buf)
{
	wait_event_interruptible(g_pdata->rts_wq, g_pdata->block_rts_flag != 0);
	g_pdata->block_rts_flag = 0;
	return sprintf(buf, "0\n");
}
static ssize_t block_cts_show(struct device *dev, struct device_attribute *a, char *buf)
{
	wait_event_interruptible(g_pdata->cts_wq, g_pdata->block_cts_flag != 0);
	g_pdata->block_cts_flag = 0;
	return sprintf(buf, "0\n");
}
/* write-side is a stub per RE (0xC0857D20/D0C just return count) */
static ssize_t block_rts_store(struct device *dev, struct device_attribute *a,
			       const char *buf, size_t count) { return count; }
static ssize_t block_cts_store(struct device *dev, struct device_attribute *a,
			       const char *buf, size_t count) { return count; }
static DEVICE_ATTR_RW(block_rts);
static DEVICE_ATTR_RW(block_cts);

/* ---------- hardware_free: wake up both RTS+CTS blocks ---------- */
static ssize_t hardware_free_show(struct device *dev, struct device_attribute *a, char *buf)
{
	g_pdata->block_rts_flag = 1;
	g_pdata->block_cts_flag = 1;
	wake_up_interruptible(&g_pdata->rts_wq);
	wake_up_interruptible(&g_pdata->cts_wq);
	return sprintf(buf, "0\n");
}
static ssize_t hardware_free_store(struct device *dev, struct device_attribute *a,
				   const char *buf, size_t count) { return count; }
static DEVICE_ATTR_RW(hardware_free);

/* ---------- busy_{rts,cts}_store: stubs per RE ---------- */
static ssize_t busy_rts_store_stub(struct device *dev, struct device_attribute *a,
				   const char *buf, size_t count) { return count; }
static ssize_t busy_cts_store_stub(struct device *dev, struct device_attribute *a,
				   const char *buf, size_t count) { return count; }

/* ---------- gpio_oe_level_status: parse hex, toggle direction of level_status ---------- */
static ssize_t gpio_oe_level_status_store(struct device *dev, struct device_attribute *a,
					  const char *buf, size_t count)
{
	unsigned int v;
	if (kstrtouint(buf, 16, &v)) return -EINVAL;
	/* RE: parses hex; direction_input/output on some level_status GPIO.
	 * Sunmi V2 shipping firmware doesn't drive this in factory print flow.
	 * Leave as no-op to preserve stub semantics until further RE. */
	(void)v;
	return count;
}
static DEVICE_ATTR(gpio_oe_level_status, 0644, NULL, gpio_oe_level_status_store);

/* Redeclare busy_{rts,cts} with mode 0664 and store stub, matching RE table */
static struct device_attribute dev_attr_busy_rts_rw = {
	.attr = { .name = "busy_rts", .mode = 0664 },
	.show = busy_rts_show,
	.store = busy_rts_store_stub,
};
static struct device_attribute dev_attr_busy_cts_rw = {
	.attr = { .name = "busy_cts", .mode = 0664 },
	.show = busy_cts_show,
	.store = busy_cts_store_stub,
};

static struct attribute *printer_attrs[] = {
	&dev_attr_gpio_oe_level_status.attr,
	&dev_attr_power.attr,
	&dev_attr_lvl_en.attr,
	&dev_attr_resume.attr,
	&dev_attr_sleep.attr,
	&dev_attr_reset.attr,
	&dev_attr_hardware_free.attr,
	&dev_attr_busy_rts_rw.attr,
	&dev_attr_busy_cts_rw.attr,
	&dev_attr_block_rts.attr,
	&dev_attr_block_cts.attr,
	NULL,
};
static const struct attribute_group printer_attr_group = {
	.attrs = printer_attrs,
};

/* ---------- IRQ handlers (RE: tagged "GH1" / "GH2") ---------- */
static irqreturn_t printer_irq0_handler(int irq, void *dev_id)
{
	/* IRQ0 = CTS transition. Signal block_cts_show waiter. */
	g_pdata->block_cts_flag = 1;
	wake_up_interruptible(&g_pdata->cts_wq);
	return IRQ_HANDLED;
}
static irqreturn_t printer_irq1_handler(int irq, void *dev_id)
{
	/* IRQ1 = RTS transition. Signal block_rts_show waiter. */
	g_pdata->block_rts_flag = 1;
	wake_up_interruptible(&g_pdata->rts_wq);
	return IRQ_HANDLED;
}

/* ---------- DT parse ---------- */
static int parse_dt(struct device *dev, struct printer_gpio_data *pdata)
{
	struct device_node *np = dev->of_node;

	pdata->gpio_pwr_en    = of_get_named_gpio(np, "printer,pwr_en",    0);
	pdata->gpio_mcu_reset = of_get_named_gpio(np, "printer,mcu_reset", 0);
	pdata->gpio_lvl_en    = of_get_named_gpio(np, "printer,lvl_en",    0);
	pdata->gpio_sleep     = of_get_named_gpio(np, "printer,sleep",     0);
	pdata->gpio_resume    = of_get_named_gpio(np, "printer,resume",    0);
	pdata->gpio_rts       = of_get_named_gpio(np, "printer,irq1",      0);
	pdata->gpio_cts       = of_get_named_gpio(np, "printer,irq0",      0);
	if (!gpio_is_valid(pdata->gpio_pwr_en)  ||
	    !gpio_is_valid(pdata->gpio_mcu_reset) ||
	    !gpio_is_valid(pdata->gpio_lvl_en)  ||
	    !gpio_is_valid(pdata->gpio_sleep)   ||
	    !gpio_is_valid(pdata->gpio_resume)  ||
	    !gpio_is_valid(pdata->gpio_rts)     ||
	    !gpio_is_valid(pdata->gpio_cts))
		return -EINVAL;
	return 0;
}

/* ---------- probe/remove ---------- */
static int printer_gpio_probe(struct platform_device *pdev)
{
	struct printer_gpio_data *pdata;
	int rc;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "odm_printer_gpio driver only supports device tree probe\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	rc = parse_dt(&pdev->dev, pdata);
	if (rc)
		return rc;

	init_waitqueue_head(&pdata->rts_wq);
	init_waitqueue_head(&pdata->cts_wq);

	/* Request output GPIOs low, inputs default */
	rc = devm_gpio_request_one(&pdev->dev, pdata->gpio_pwr_en,    GPIOF_OUT_INIT_LOW, "printer_pwr_en");
	if (rc) return rc;
	rc = devm_gpio_request_one(&pdev->dev, pdata->gpio_mcu_reset, GPIOF_OUT_INIT_LOW, "printer_reset");
	if (rc) return rc;
	rc = devm_gpio_request_one(&pdev->dev, pdata->gpio_lvl_en,    GPIOF_OUT_INIT_LOW, "printer_lvl_en");
	if (rc) return rc;
	rc = devm_gpio_request_one(&pdev->dev, pdata->gpio_sleep,     GPIOF_OUT_INIT_LOW, "printer_sleep");
	if (rc) return rc;
	rc = devm_gpio_request_one(&pdev->dev, pdata->gpio_resume,    GPIOF_OUT_INIT_LOW, "printer_resume");
	if (rc) return rc;
	rc = devm_gpio_request_one(&pdev->dev, pdata->gpio_rts, GPIOF_IN, "printer_rts");
	if (rc) return rc;
	rc = devm_gpio_request_one(&pdev->dev, pdata->gpio_cts, GPIOF_IN, "printer_cts");
	if (rc) return rc;

	pdata->irq0 = gpio_to_irq(pdata->gpio_cts);
	pdata->irq1 = gpio_to_irq(pdata->gpio_rts);
	rc = devm_request_irq(&pdev->dev, pdata->irq0, printer_irq0_handler,
			      IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "GH1", NULL);
	if (rc) return rc;
	rc = devm_request_irq(&pdev->dev, pdata->irq1, printer_irq1_handler,
			      IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "GH2", NULL);
	if (rc) return rc;

	/* Create /sys/printer_pin/printer/ */
	pdata->root_kobj = kobject_create_and_add("printer_pin", NULL);
	if (!pdata->root_kobj) return -ENOMEM;
	pdata->printer_kobj = kobject_create_and_add("printer", pdata->root_kobj);
	if (!pdata->printer_kobj) {
		kobject_put(pdata->root_kobj);
		return -ENOMEM;
	}
	rc = sysfs_create_group(pdata->printer_kobj, &printer_attr_group);
	if (rc) {
		kobject_put(pdata->printer_kobj);
		kobject_put(pdata->root_kobj);
		return rc;
	}

	platform_set_drvdata(pdev, pdata);
	g_pdata = pdata;
	dev_info(&pdev->dev, "odm_printer_gpio probed\n");
	return 0;
}

static int printer_gpio_remove(struct platform_device *pdev)
{
	struct printer_gpio_data *pdata = platform_get_drvdata(pdev);

	if (pdata->printer_kobj) {
		sysfs_remove_group(pdata->printer_kobj, &printer_attr_group);
		kobject_put(pdata->printer_kobj);
	}
	if (pdata->root_kobj)
		kobject_put(pdata->root_kobj);
	g_pdata = NULL;
	return 0;
}

static const struct of_device_id printer_gpio_of_match[] = {
	{ .compatible = "summi,printer_gpio" },  /* typo preserved from OEM DTS */
	{ .compatible = "sunmi,printer_gpio" },  /* accept correct spelling too */
	{}
};
MODULE_DEVICE_TABLE(of, printer_gpio_of_match);

static struct platform_driver printer_gpio_driver = {
	.probe = printer_gpio_probe,
	.remove = printer_gpio_remove,
	.driver = {
		.name = "odm_printer_gpio",
		.of_match_table = printer_gpio_of_match,
	},
};
module_platform_driver(printer_gpio_driver);

MODULE_DESCRIPTION("Sunmi V2 thermal printer GPIO/IRQ driver");
MODULE_LICENSE("GPL v2");
