// SPDX-License-Identifier: GPL-2.0

#include <linux/cdev.h>	   // cdev_*()
#include <linux/fs.h>	   // struct file, open, release
#include <linux/uaccess.h> // copy_to_user()

#include "mcp23s08_driver.h"

#define LED_BASE_MINOR 0
#define LED_MAX_MINORS 1
#define LED_DEVICE_NAME "frootspi_led"

static struct class *led_class;
static int led_major;
struct led_device_info {
	// ここはある程度自由に定義できる
	struct cdev cdev;
	unsigned int device_major;
	unsigned int device_minor;
};
static struct led_device_info stored_device_info[LED_MAX_MINORS];

extern int mcp23s08_write_gpio(
	const unsigned char gpio_num, const unsigned char value);

static int led_open(struct inode *inode, struct file *filep)
{
	struct led_device_info *dev_info;
	// container_of(メンバーへのポインタ, 構造体の型, 構造体メンバの名前)
	dev_info = container_of(inode->i_cdev, struct led_device_info, cdev);

	// ここに何かしらのエラー処理がいるかも

	dev_info->device_major = MAJOR(inode->i_rdev);
	dev_info->device_minor = MINOR(inode->i_rdev);

	filep->private_data = dev_info;

	printk(KERN_DEBUG "%s %s: led device opend.\n", LED_DEVICE_NAME,
		__func__);

	return 0;
}

static int led_release(struct inode *inode, struct file *filep)
{
	// デバイスによっては何もしなかったりする
	// kfree(filep->private_data);
	printk(KERN_DEBUG "%s %s: led device closed.\n", LED_DEVICE_NAME,
		__func__);

	return 0;
}

static ssize_t led_write(
	struct file *filep, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct led_device_info *dev_info = filep->private_data;
	printk("%s %s: led_write, major:%d, minor:%d\n", LED_DEVICE_NAME,
		__func__, dev_info->device_major, dev_info->device_minor);

	// 何文字書き込み指定されようが、こちらが勝手に書き込ませていただきます
	if (count > 0) {
		char value;
		if (copy_from_user(&value, buf, sizeof(char)) != 0) {
			printk(KERN_ERR "%s %s: copy_from_user() failed.\n",
				LED_DEVICE_NAME, __func__);
			return -1;
		}

		// LEDを制御
		if (value == '0') {
			mcp23s08_write_gpio(MCP23S08_GPIO_LED, 0);
		} else if (value == '1') {
			mcp23s08_write_gpio(MCP23S08_GPIO_LED, 1);
		}
		return sizeof(char);
	}
	return 0;
}

static struct file_operations led_fops = {
	.open = led_open,
	.release = led_release,
	.write = led_write,
};

int register_led_dev(void)
{
	int retval;
	dev_t dev;

	// 動的にメジャー番号を確保する
	retval = alloc_chrdev_region(
		&dev, LED_BASE_MINOR, LED_MAX_MINORS, LED_DEVICE_NAME);
	if (retval < 0) {
		// 確保できなかったらエラーを返して終了
		printk(KERN_ERR "%s %s: unable to allocate device number\n",
			LED_DEVICE_NAME, __func__);
		return retval;
	}

	// デバイスのクラスを登録する(/sys/class/***/ を作成)
	// ドライバが動いてる最中はクラスを保持するため、クラスはグローバル変数である
	led_class = class_create(THIS_MODULE, LED_DEVICE_NAME);
	if (IS_ERR(led_class)) {
		// 登録できなかったらエラー処理に移動する
		retval = PTR_ERR(led_class);
		printk(KERN_ERR "%s %s: class creation failed\n",
			LED_DEVICE_NAME, __func__);
		goto failed_class_create;
	}

	// マイナー番号ごとに(デバイスの数だけ)、ドライバの登録をする
	led_major = MAJOR(dev);
	for (int i = 0; i < LED_MAX_MINORS; i++) {
		// ドライバの初期化する
		// file_operationを登録するので、ここでドライバの機能が決まる
		cdev_init(&stored_device_info[i].cdev, &led_fops);
		stored_device_info[i].cdev.owner = THIS_MODULE;

		// ドライバをカーネルへ登録する
		// 同じドライバを複数作成するときは、cdev_add(*,*,
		// 3)みたいに末尾の数値を増やす
		// 今回はドライバごとにメモリを確保したいので、cdev_add()自体を複数回実行する
		retval = cdev_add(&stored_device_info[i].cdev,
			MKDEV(led_major, LED_BASE_MINOR + i), 1);
		if (retval < 0) {
			// 登録できなかったらエラー処理へ移動する
			printk(KERN_ERR "%s %s: minor=%d: chardev registration "
					"failed\n",
				LED_DEVICE_NAME, __func__, LED_BASE_MINOR + i);
			goto failed_cdev_add;
		}

		// ドライバによっては、ここでエラー検出してたりしてなかったりする
		device_create(led_class, NULL,
			MKDEV(led_major, LED_BASE_MINOR + i), NULL, "%s%u",
			LED_DEVICE_NAME, i);
	}

	return 0;

failed_cdev_add:
	class_destroy(led_class);
failed_class_create:
	unregister_chrdev_region(
		MKDEV(led_major, LED_BASE_MINOR), LED_MAX_MINORS);
	return retval;
}

void unregister_led_dev(void)
{
	// 基本的にはregister_led_devの逆の手順でメモリを開放していく
	for (int i = 0; i < LED_MAX_MINORS; i++) {
		device_destroy(led_class, MKDEV(led_major, LED_BASE_MINOR + i));
		cdev_del(&stored_device_info[i].cdev);
	}
	class_destroy(led_class);
	unregister_chrdev_region(
		MKDEV(led_major, LED_BASE_MINOR), LED_MAX_MINORS);
}
