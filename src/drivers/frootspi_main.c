// SPDX-License-Identifier: GPL-2.0

#include <linux/cdev.h>  // cdev_*()
// #include <linux/device.h>
#include <linux/fs.h>  // struct file, open, release
// #include <linux/init.h>
// #include <linux/kernel.h>
#include <linux/module.h>  // module_*()
// #include <linux/sched.h>
#include <linux/slab.h>  // kmalloc()
// #include <linux/types.h>
#include <linux/uaccess.h>  // copy_to_user()

MODULE_LICENSE("GPL");

extern int register_hello_dev(void);
extern void unregister_hello_dev(void);
extern int register_spi_dev(void);
extern void unregister_spi_dev(void);

static int frootspi_init(void)
{
	printk("frootspi_init\n");

	register_hello_dev();
	register_spi_dev();

	return 0;
}

static void frootspi_exit(void)
{
	printk("frootspi_exit\n");

	unregister_hello_dev();
	unregister_spi_dev();
}

module_init(frootspi_init);
module_exit(frootspi_exit);
