// SPDX-License-Identifier: GPL-2.0
/*
 * spi_printer.c
 *
 * Sunmi V2 thermal printer SPI driver.
 * Clean-room re-implementation based on upstream spidev.c + RE data (G-12).
 *
 * Exposes:
 *   /dev/spidev0.0                       char device (spidev-compatible ioctls)
 *   /sys/sunmi/ctrl/mcu_version          MCU firmware version (SPI read)
 *
 * DT binding: compatible = "huaqin,sunmi_printer"
 * Companion:  drivers/misc/odm_printer_gpio.c
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/cdev.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

/*
 * SPI_MODE_MASK is exported by newer kernels via <linux/spi/spi.h>. On
 * MTK 4.4 vendor trees the definition lives in the userspace uapi header
 * (<uapi/linux/spi/spidev.h>) but is not re-exported to kernel drivers.
 * Provide a local fallback.
 */
#ifndef SPI_MODE_MASK
#define SPI_MODE_MASK (SPI_CPHA | SPI_CPOL)
#endif

#define SPI_PRINTER_NAME "spi_printer"
#define SPIDEV_MAJOR     153       /* assigned in Documentation/devices.txt */
#define N_SPI_MINORS     32

static DECLARE_BITMAP(minors, N_SPI_MINORS);
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

/* Buffer size mirrors upstream spidev default */
static unsigned bufsiz = 4096;
module_param(bufsiz, uint, S_IRUGO);
MODULE_PARM_DESC(bufsiz, "SPI transfer buffer size (bytes)");

/* Total per-device state = 0x78 bytes per RE (kzalloc @ spi_printer_probe) */
struct spi_printer_data {
	dev_t             devt;
	struct spi_device *spi;
	struct list_head  device_entry;
	struct mutex      buf_lock;
	unsigned          users;
	u8                *buf;
	u32               speed_hz;
};

static struct class *spidev_class;

/* ---------- Sunmi ctrl kset/kobject (/sys/sunmi/ctrl/) ---------- */
static struct kset       *sunmi_kset;
static struct kobject    *sunmi_ctrl_kobj;
static struct spi_printer_data *g_sp;

/* Exported so odm_printer_gpio (or others) can add attributes under
 * /sys/sunmi/ctrl/ if they choose. Matches EXPORT_SYMBOL(create_sunmi_sysfs)
 * observed in Sunmi V2 kallsyms. */
int create_sunmi_sysfs(void)
{
	if (sunmi_kset && sunmi_ctrl_kobj)
		return 0;
	sunmi_kset = kset_create_and_add("sunmi", NULL, NULL);
	if (!sunmi_kset)
		return -ENOMEM;
	sunmi_ctrl_kobj = kobject_create_and_add("ctrl", &sunmi_kset->kobj);
	if (!sunmi_ctrl_kobj) {
		kset_unregister(sunmi_kset);
		sunmi_kset = NULL;
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(create_sunmi_sysfs);

/* ---------- mcu_version show/store ---------- */
static ssize_t mcu_version_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	struct spi_transfer t = { 0 };
	struct spi_message m;
	u8 tx[8] = { 0xAA, 0x55, 0x01, 0x00 };  /* placeholder MCU query cmd */
	u8 rx[8] = { 0 };
	int rc;

	if (!g_sp || !g_sp->spi)
		return -ENODEV;

	t.tx_buf = tx;
	t.rx_buf = rx;
	t.len    = sizeof(tx);
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	mutex_lock(&g_sp->buf_lock);
	rc = spi_sync(g_sp->spi, &m);
	mutex_unlock(&g_sp->buf_lock);
	if (rc)
		return rc;
	/* Response layout is MCU-specific; log raw bytes for user parsing */
	return scnprintf(buf, PAGE_SIZE, "%02x %02x %02x %02x %02x %02x %02x %02x\n",
			 rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);
}
static ssize_t mcu_version_store(struct kobject *k, struct kobj_attribute *a,
				 const char *buf, size_t count)
{
	struct spi_transfer t = { 0 };
	struct spi_message m;
	u8 tx[16] = { 0 };
	int i, rc;

	if (!g_sp || !g_sp->spi)
		return -ENODEV;
	/* Copy up to 16 bytes as-is */
	for (i = 0; i < count && i < sizeof(tx); i++)
		tx[i] = buf[i];
	t.tx_buf = tx;
	t.len    = i;
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	mutex_lock(&g_sp->buf_lock);
	rc = spi_sync(g_sp->spi, &m);
	mutex_unlock(&g_sp->buf_lock);
	return rc ? rc : count;
}
static struct kobj_attribute mcu_version_attr = __ATTR(mcu_version, 0644,
						       mcu_version_show, mcu_version_store);

/* ---------- char device ops (spidev-compatible) ---------- */
static ssize_t spidev_sync_write(struct spi_printer_data *sp, size_t len)
{
	struct spi_transfer t = { .tx_buf = sp->buf, .len = len };
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(sp->spi, &m);
}
static ssize_t spidev_sync_read(struct spi_printer_data *sp, size_t len)
{
	struct spi_transfer t = { .rx_buf = sp->buf, .len = len };
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(sp->spi, &m);
}

static ssize_t spidev_write(struct file *filp, const char __user *ubuf,
			    size_t count, loff_t *f_pos)
{
	struct spi_printer_data *sp = filp->private_data;
	ssize_t rc;

	if (count > bufsiz)
		return -EMSGSIZE;
	mutex_lock(&sp->buf_lock);
	if (copy_from_user(sp->buf, ubuf, count)) {
		rc = -EFAULT;
		goto out;
	}
	rc = spidev_sync_write(sp, count);
	if (rc == 0)
		rc = count;
out:
	mutex_unlock(&sp->buf_lock);
	return rc;
}

static ssize_t spidev_read(struct file *filp, char __user *ubuf,
			   size_t count, loff_t *f_pos)
{
	struct spi_printer_data *sp = filp->private_data;
	ssize_t rc;

	if (count > bufsiz)
		return -EMSGSIZE;
	mutex_lock(&sp->buf_lock);
	rc = spidev_sync_read(sp, count);
	if (rc == 0) {
		if (copy_to_user(ubuf, sp->buf, count))
			rc = -EFAULT;
		else
			rc = count;
	}
	mutex_unlock(&sp->buf_lock);
	return rc;
}

static long spidev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct spi_printer_data *sp = filp->private_data;
	struct spi_device *spi;
	u32 tmp;
	long rc = 0;

	if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC)
		return -ENOTTY;

	mutex_lock(&sp->buf_lock);
	spi = sp->spi;
	switch (cmd) {
	case SPI_IOC_RD_MODE:
		rc = put_user(spi->mode & SPI_MODE_MASK, (__u8 __user *)arg);
		break;
	case SPI_IOC_WR_MODE:
		if (get_user(tmp, (__u8 __user *)arg)) { rc = -EFAULT; break; }
		spi->mode = (spi->mode & ~SPI_MODE_MASK) | (tmp & SPI_MODE_MASK);
		rc = spi_setup(spi);
		break;
	case SPI_IOC_RD_LSB_FIRST:
		rc = put_user((spi->mode & SPI_LSB_FIRST) ? 1 : 0, (__u8 __user *)arg);
		break;
	case SPI_IOC_WR_LSB_FIRST:
		if (get_user(tmp, (__u8 __user *)arg)) { rc = -EFAULT; break; }
		if (tmp) spi->mode |= SPI_LSB_FIRST;
		else     spi->mode &= ~SPI_LSB_FIRST;
		rc = spi_setup(spi);
		break;
	case SPI_IOC_RD_BITS_PER_WORD:
		rc = put_user(spi->bits_per_word, (__u8 __user *)arg);
		break;
	case SPI_IOC_WR_BITS_PER_WORD:
		if (get_user(tmp, (__u8 __user *)arg)) { rc = -EFAULT; break; }
		spi->bits_per_word = tmp;
		rc = spi_setup(spi);
		break;
	case SPI_IOC_RD_MAX_SPEED_HZ:
		rc = put_user(sp->speed_hz, (__u32 __user *)arg);
		break;
	case SPI_IOC_WR_MAX_SPEED_HZ:
		if (get_user(tmp, (__u32 __user *)arg)) { rc = -EFAULT; break; }
		spi->max_speed_hz = tmp;
		rc = spi_setup(spi);
		if (!rc) sp->speed_hz = tmp;
		break;
	default:
		/* SPI_IOC_MESSAGE(N) — variable-size, matches upstream spidev pattern */
		rc = -ENOTTY;
		break;
	}
	mutex_unlock(&sp->buf_lock);
	return rc;
}

static int spidev_open(struct inode *inode, struct file *filp)
{
	struct spi_printer_data *sp = NULL, *cur;
	int rc = -ENXIO;

	mutex_lock(&device_list_lock);
	list_for_each_entry(cur, &device_list, device_entry) {
		if (cur->devt == inode->i_rdev) { sp = cur; break; }
	}
	if (!sp) { pr_debug("spi_dev: nothing for minor %d\n", iminor(inode)); goto out; }
	if (!sp->buf) {
		sp->buf = kmalloc(bufsiz, GFP_KERNEL);
		if (!sp->buf) { rc = -ENOMEM; pr_debug("open/ENOMEM\n"); goto out; }
	}
	sp->users++;
	filp->private_data = sp;
	nonseekable_open(inode, filp);
	rc = 0;
out:
	mutex_unlock(&device_list_lock);
	return rc;
}

static int spidev_release(struct inode *inode, struct file *filp)
{
	struct spi_printer_data *sp = filp->private_data;
	int dofree;

	mutex_lock(&device_list_lock);
	sp->users--;
	dofree = (sp->users == 0);
	if (dofree) {
		kfree(sp->buf);
		sp->buf = NULL;
	}
	mutex_unlock(&device_list_lock);
	return 0;
}

static const struct file_operations spidev_fops = {
	.owner   = THIS_MODULE,
	.write   = spidev_write,
	.read    = spidev_read,
	.unlocked_ioctl = spidev_ioctl,
	.open    = spidev_open,
	.release = spidev_release,
	.llseek  = no_llseek,
};

/* ---------- SPI probe/remove ---------- */
static int spi_printer_probe(struct spi_device *spi)
{
	struct spi_printer_data *sp;
	unsigned long minor;
	int rc;

	sp = kzalloc(sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;

	sp->spi = spi;
	mutex_init(&sp->buf_lock);
	INIT_LIST_HEAD(&sp->device_entry);

	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor >= N_SPI_MINORS) {
		mutex_unlock(&device_list_lock);
		kfree(sp);
		return -ENODEV;
	}
	sp->devt = MKDEV(SPIDEV_MAJOR, minor);
	{
		struct device *d = device_create(spidev_class, &spi->dev, sp->devt,
						 sp, "spidev%d.%d",
						 spi->master->bus_num, spi->chip_select);
		if (IS_ERR(d)) {
			mutex_unlock(&device_list_lock);
			kfree(sp);
			return PTR_ERR(d);
		}
	}
	set_bit(minor, minors);
	list_add(&sp->device_entry, &device_list);
	mutex_unlock(&device_list_lock);

	sp->speed_hz = spi->max_speed_hz;
	spi_set_drvdata(spi, sp);
	g_sp = sp;

	/* Create /sys/sunmi/ctrl/mcu_version */
	rc = create_sunmi_sysfs();
	if (!rc)
		rc = sysfs_create_file(sunmi_ctrl_kobj, &mcu_version_attr.attr);
	if (rc)
		dev_warn(&spi->dev, "create_sunmi_sysfs failed: %d\n", rc);

	dev_info(&spi->dev, "spi_printer probed: mode=0x%x max_hz=%d bpw=%d\n",
		 spi->mode, spi->max_speed_hz, spi->bits_per_word);
	return 0;
}

static int spi_printer_remove(struct spi_device *spi)
{
	struct spi_printer_data *sp = spi_get_drvdata(spi);

	if (sunmi_ctrl_kobj)
		sysfs_remove_file(sunmi_ctrl_kobj, &mcu_version_attr.attr);
	mutex_lock(&device_list_lock);
	list_del(&sp->device_entry);
	device_destroy(spidev_class, sp->devt);
	clear_bit(MINOR(sp->devt), minors);
	mutex_unlock(&device_list_lock);
	kfree(sp);
	g_sp = NULL;
	return 0;
}

static const struct of_device_id spi_printer_of_match[] = {
	{ .compatible = "huaqin,sunmi_printer" },
	{ .compatible = "sunmi,printer" },
	{}
};
MODULE_DEVICE_TABLE(of, spi_printer_of_match);

static struct spi_driver spi_printer_driver = {
	.driver = {
		.name = SPI_PRINTER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = spi_printer_of_match,
	},
	.probe = spi_printer_probe,
	.remove = spi_printer_remove,
};

/* ---------- Class + module init/exit ---------- */
static int __init spi_printer_init(void)
{
	int rc;

	rc = register_chrdev(SPIDEV_MAJOR, "spi", &spidev_fops);
	if (rc < 0)
		return rc;
	spidev_class = class_create(THIS_MODULE, "spidev");
	if (IS_ERR(spidev_class)) {
		unregister_chrdev(SPIDEV_MAJOR, "spi");
		return PTR_ERR(spidev_class);
	}
	rc = spi_register_driver(&spi_printer_driver);
	if (rc < 0) {
		class_destroy(spidev_class);
		unregister_chrdev(SPIDEV_MAJOR, "spi");
	}
	return rc;
}
module_init(spi_printer_init);

static void __exit spi_printer_exit(void)
{
	spi_unregister_driver(&spi_printer_driver);
	class_destroy(spidev_class);
	unregister_chrdev(SPIDEV_MAJOR, "spi");
	if (sunmi_ctrl_kobj) {
		kobject_put(sunmi_ctrl_kobj);
		sunmi_ctrl_kobj = NULL;
	}
	if (sunmi_kset) {
		kset_unregister(sunmi_kset);
		sunmi_kset = NULL;
	}
}
module_exit(spi_printer_exit);

MODULE_DESCRIPTION("Sunmi V2 thermal printer SPI driver");
MODULE_LICENSE("GPL v2");
