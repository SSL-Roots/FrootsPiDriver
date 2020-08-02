// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>

#include "test.h"

static int test_init(void)
{
	printk("Hello my module\n");
	test_text();
	return 0;
}

static void test_exit(void)
{
	printk("Bye bye my module\n");
}

module_init(test_init);
module_exit(test_exit);