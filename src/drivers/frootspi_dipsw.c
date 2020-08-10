// SPDX-License-Identifier: GPL-2.0

#include <linux/cdev.h>	   // cdev_*()
#include <linux/fs.h>	   // struct file, open, release
#include <linux/uaccess.h> // copy_to_user()

#include "mcp23s08_driver.h"

#define DIPSW_MAX_BUFLEN 64 // copy_to_user用のバッファサイズ
#define DIPSW_BASE_MINOR 0
#define DIPSW_MAX_MINORS 2
#define DIPSW_DEVICE_NAME "frootspi_dipsw"

static struct class *dipsw_class;
static int dipsw_major;
struct dipsw_device_info {
	// ここはある程度自由に定義できる
	struct cdev cdev;
	unsigned int device_major;
	unsigned int device_minor;
	unsigned char target_gpio_num;
};
static struct dipsw_device_info stored_device_info[DIPSW_MAX_MINORS];

extern int mcp23s08_read_gpio(const unsigned char gpio_num);

static int dipsw_open(struct inode *inode, struct file *filep)
{
	struct dipsw_device_info *dev_info;
	// container_of(メンバーへのポインタ, 構造体の型, 構造体メンバの名前)
	dev_info = container_of(inode->i_cdev, struct dipsw_device_info, cdev);

	dev_info->device_major = MAJOR(inode->i_rdev);
	dev_info->device_minor = MINOR(inode->i_rdev);

	switch (dev_info->device_minor) {
	case 0:
		dev_info->target_gpio_num = MCP23S08_GPIO_DIPSW0;
		break;
	case 1:
		dev_info->target_gpio_num = MCP23S08_GPIO_DIPSW1;
		break;
	default:
		dev_info->target_gpio_num = 0;
	}

	filep->private_data = dev_info;

	printk(KERN_DEBUG "%s %s: dipsw%d device opened.\n", DIPSW_DEVICE_NAME,
		__func__, dev_info->device_minor);

	return 0;
}

static int dipsw_release(struct inode *inode, struct file *filep)
{
	// デバイスによっては何もしなかったりする
	// kfree(filep->private_data);

	printk(KERN_DEBUG "%s %s: device closed.\n", DIPSW_DEVICE_NAME,
		__func__);
	return 0;
}

static ssize_t dipsw_read(
	struct file *filep, char __user *buf, size_t count, loff_t *f_pos)
{
	struct dipsw_device_info *dev_info = filep->private_data;
	// オフセットがあったら正常終了する
	// 短い文字列しかコピーしないので、これで問題なし
	// 長い文字列をコピーするばあいは、オフセットが重要
	if (*f_pos > 0) {
		return 0; // EOF
	}

	int gpio_value = mcp23s08_read_gpio(dev_info->target_gpio_num);
	if (gpio_value < 0) {
		printk(KERN_ERR "%s %s: mcp23s08_read_gpio() failed.\n",
			DIPSW_DEVICE_NAME, __func__);
		return 0;
	}

	unsigned char buffer[DIPSW_MAX_BUFLEN];
	sprintf(buffer, "%d\n", gpio_value);

	count = strlen(buffer);

	// dipswドライバがもつテキスト情報をユーザ空間へコピーする
	// copy_to_userで、buf宛に、dev_info->bufferのデータを、countバイトコピーする
	// コピーできなかったバイト数が返り値で渡される
	if (copy_to_user((void *)buf, &buffer, count)) {
		printk(KERN_ERR "%s %s: copy_to_user() failed.\n",
			DIPSW_DEVICE_NAME, __func__);
		return -1;
	}
	*f_pos += count;

	return count;
}

static struct file_operations dipsw_fops = {
	.open = dipsw_open,
	.release = dipsw_release,
	.read = dipsw_read,
};

int register_dipsw_dev(void)
{
	int retval;
	dev_t dev;

	// 動的にメジャー番号を確保する
	retval = alloc_chrdev_region(
		&dev, DIPSW_BASE_MINOR, DIPSW_MAX_MINORS, DIPSW_DEVICE_NAME);
	if (retval < 0) {
		// 確保できなかったらエラーを返して終了
		printk(KERN_ERR "%s %s: unable to allocate device number\n",
			DIPSW_DEVICE_NAME, __func__);
		return retval;
	}

	// デバイスのクラスを登録する(/sys/class/***/ を作成)
	// ドライバが動いてる最中はクラスを保持するため、クラスはグローバル変数である
	dipsw_class = class_create(THIS_MODULE, DIPSW_DEVICE_NAME);
	if (IS_ERR(dipsw_class)) {
		// 登録できなかったらエラー処理に移動する
		retval = PTR_ERR(dipsw_class);
		printk(KERN_ERR "%s %s: class creation failed\n",
			DIPSW_DEVICE_NAME, __func__);
		goto failed_class_create;
	}

	// マイナー番号ごとに(デバイスの数だけ)、ドライバの登録をする
	dipsw_major = MAJOR(dev);
	for (int i = 0; i < DIPSW_MAX_MINORS; i++) {
		// ドライバの初期化する
		// file_operationを登録するので、ここでドライバの機能が決まる
		cdev_init(&stored_device_info[i].cdev, &dipsw_fops);
		stored_device_info[i].cdev.owner = THIS_MODULE;

		// ドライバをカーネルへ登録する
		// 同じドライバを複数作成するときは、cdev_add(*,*,
		// 3)みたいに末尾の数値を増やす
		// 今回はドライバごとにメモリを確保したいので、cdev_add()自体を複数回実行する
		retval = cdev_add(&stored_device_info[i].cdev,
			MKDEV(dipsw_major, DIPSW_BASE_MINOR + i), 1);
		if (retval < 0) {
			// 登録できなかったらエラー処理へ移動する
			printk(KERN_ERR
				"%s: minor=%d: chardev registration failed\n",
				DIPSW_DEVICE_NAME, DIPSW_BASE_MINOR + i);
			goto failed_cdev_add;
		}

		// ドライバによっては、ここでエラー検出してたりしてなかったりする
		device_create(dipsw_class, NULL,
			MKDEV(dipsw_major, DIPSW_BASE_MINOR + i), NULL, "%s%u",
			DIPSW_DEVICE_NAME, i);
	}

	return 0;

failed_cdev_add:
	class_destroy(dipsw_class);
failed_class_create:
	unregister_chrdev_region(
		MKDEV(dipsw_major, DIPSW_BASE_MINOR), DIPSW_MAX_MINORS);
	return retval;
}

void unregister_dipsw_dev(void)
{
	// 基本的にはregister_dipsw_devの逆の手順でメモリを開放していく
	for (int i = 0; i < DIPSW_MAX_MINORS; i++) {
		device_destroy(
			dipsw_class, MKDEV(dipsw_major, DIPSW_BASE_MINOR + i));
		cdev_del(&stored_device_info[i].cdev);
	}
	class_destroy(dipsw_class);
	unregister_chrdev_region(
		MKDEV(dipsw_major, DIPSW_BASE_MINOR), DIPSW_MAX_MINORS);
}