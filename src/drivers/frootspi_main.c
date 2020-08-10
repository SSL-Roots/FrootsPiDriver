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
extern int register_mcp23s08_driver(void);
extern void unregister_mcp23s08_driver(void);
extern int register_pushsw_dev(void);
extern void unregister_pushsw_dev(void);
extern int register_led_dev(void);
extern void unregister_led_dev(void);
extern int register_dipsw_dev(void);
extern void unregister_dipsw_dev(void);

static int frootspi_init(void)
{
	register_hello_dev();

	if(register_mcp23s08_driver()){
		printk(KERN_ERR "%s: register_mcp23s08_driver() failed.\n", __func__);
	}else{
		register_pushsw_dev();
		register_dipsw_dev();
		register_led_dev();
	}
	return 0;
}

static void frootspi_exit(void)
{
	unregister_hello_dev();
	unregister_pushsw_dev();
	unregister_dipsw_dev();
	unregister_led_dev();
	unregister_mcp23s08_driver();
}

module_init(frootspi_init);
module_exit(frootspi_exit);
