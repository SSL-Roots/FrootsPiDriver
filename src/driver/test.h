// SPDX-License-Identifier: GPL-2.0

# include <linux/module.h>

static int test_text(void)
{
    printk("Hello!!!!!!!\n");
    return 0;
}