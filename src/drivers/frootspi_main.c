// SPDX-License-Identifier: GPL-2.0

#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>

MODULE_LICENSE("GPL");

#define DRIVER_NAME "frootspi"

static const unsigned int MINOR_BASE = 0;
static const unsigned int MINOR_NUM = 2;

static unsigned int mydevice_major;
static struct cdev mydevice_cdev;

static struct class *mydevice_class = NULL;


static int frootspi_open(struct inode *inode, struct file *file)
{
	printk("frootspi_open\n");
	return 0;
}

static int frootspi_close(struct inode *inode, struct file *file)
{
	printk("frootspi_close\n");
	return 0;
}

static ssize_t frootspi_read(struct file *filp, char __user *buf, size_t count,
			     loff_t *f_pos)
{
	printk("frootspi_read\n");
	buf[0] = 'A';
	return 1;
}

static ssize_t frootspi_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *f_pos)
{
	printk("frootspi_write\n");
	return 1;
}

static struct file_operations frootspi_fops = {
    .open = frootspi_open,
    .release = frootspi_close,
    .read = frootspi_read,
    .write = frootspi_write,
};

static int frootspi_init(void)
{
	int alloc_ret = 0;
	int cdev_err = 0;
	dev_t dev;

	printk("frootspi_init\n");

	alloc_ret =
	    alloc_chrdev_region(&dev, MINOR_BASE, MINOR_NUM, DRIVER_NAME);
	if (alloc_ret != 0) {
		printk(KERN_ERR "alloc_chrdev_region = %d\n", alloc_ret);
		return -1;
	}

	mydevice_major = MAJOR(dev);
	dev = MKDEV(mydevice_major, MINOR_BASE);

	cdev_init(&mydevice_cdev, &frootspi_fops);
	mydevice_cdev.owner = THIS_MODULE;

	// cdev(challactor device)をカーネルに登録する
	cdev_err = cdev_add(&mydevice_cdev, dev, MINOR_NUM);
	if (cdev_err != 0) {
		printk(KERN_ERR "cdev_add = %d\n", cdev_err);
		unregister_chrdev_region(dev, MINOR_NUM);
		return -1;
	}

	// create /sys/class/frootspi
	mydevice_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(mydevice_class)) {
		printk(KERN_ERR "class_create\n");
		cdev_del(&mydevice_cdev);
		unregister_chrdev_region(dev, MINOR_NUM);
		return -1;
	}

	// create /sys/class/frootspi/frootspi*
	for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++) {
		device_create(mydevice_class, NULL,
			      MKDEV(mydevice_major, minor), NULL, "%s%d",
			      DRIVER_NAME, minor);
	}

	return 0;
}

static void frootspi_exit(void)
{
	dev_t dev = MKDEV(mydevice_major, MINOR_BASE);
	printk("frootspi_exit\n");

	// delete /sys/class/frootspi/frootspi*
	for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++) {
		device_destroy(mydevice_class, MKDEV(mydevice_major, minor));
	}

	// delete /sys/class/frootspi/
	class_destroy(mydevice_class);

	// delete cdev
	cdev_del(&mydevice_cdev);

	// unregister major number
	unregister_chrdev_region(dev, MINOR_NUM);
}

module_init(frootspi_init);
module_exit(frootspi_exit);
