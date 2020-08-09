#include <linux/cdev.h>  // cdev_*()
#include <linux/fs.h>  // struct file, open, release
#include <linux/module.h>
#include <linux/spi/spi.h>  // spi_*()
#include <linux/uaccess.h>  // copy_to_user()

#include "mcp23s08_driver.h"

#define PUSHSW_MAX_BUFLEN 64  // copy_to_user用のバッファサイズ
#define PUSHSW_BASE_MINOR 0
#define PUSHSW_MAX_MINORS 4
#define PUSHSW_DEVICE_NAME "frootspi_pushsw"
#define PUSHSW_GPIO_SW0 1
#define PUSHSW_GPIO_SW1 2
#define PUSHSW_GPIO_SW2 3
#define PUSHSW_GPIO_SW3 4

static struct class *pushsw_class;
static int pushsw_major;
struct pushsw_device_info {
	// ここはある程度自由に定義できる
	struct cdev cdev;
	unsigned int device_major;
	unsigned int device_minor;
	unsigned char target_gpio_num;
};
static struct pushsw_device_info stored_device_info[PUSHSW_MAX_MINORS];

extern int mcp23s08_read_gpio(const unsigned char gpio_num);

static int pushsw_open(struct inode *inode, struct file *filep)
{
	struct pushsw_device_info *dev_info;
	// container_of(メンバーへのポインタ, 構造体の型, 構造体メンバの名前)
	dev_info = container_of(inode->i_cdev, struct pushsw_device_info, cdev);

	dev_info->device_major = MAJOR(inode->i_rdev);
	dev_info->device_minor = MINOR(inode->i_rdev);

	switch(dev_info->device_minor) {
	case 0:
		dev_info->target_gpio_num = MCP23S08_GPIO_PUSHSW0;
		break;
	case 1:
		dev_info->target_gpio_num = MCP23S08_GPIO_PUSHSW1;
		break;
	case 2:
		dev_info->target_gpio_num = MCP23S08_GPIO_PUSHSW2;
		break;
	case 3:
		dev_info->target_gpio_num = MCP23S08_GPIO_PUSHSW3;
		break;
	default:
		dev_info->target_gpio_num = 0;
	}

	filep->private_data = dev_info;

	printk(KERN_DEBUG "%s %s: pushsw%d device opened.\n",
        PUSHSW_DEVICE_NAME, __func__,
        dev_info->device_minor);

	return 0;
}

static int pushsw_release(struct inode *inode, struct file *filep)
{
	// デバイスによっては何もしなかったりする
	// kfree(filep->private_data);

	printk(KERN_DEBUG "%s %s: device closed.\n",
        PUSHSW_DEVICE_NAME, __func__);
	return 0;
}

static ssize_t pushsw_read(struct file *filep, char __user *buf, size_t count,
			     loff_t *f_pos)
{
	struct pushsw_device_info *dev_info = filep->private_data;
	// オフセットがあったら正常終了する
	// 短い文字列しかコピーしないので、これで問題なし
	// 長い文字列をコピーするばあいは、オフセットが重要
	if (*f_pos > 0) {
		return 0;  // EOF
	}

	int gpio_value = mcp23s08_read_gpio(dev_info->target_gpio_num);
	if (gpio_value < 0){
		printk(KERN_ERR "%s %s: mcp23s08_read_gpio() failed.\n",
            PUSHSW_DEVICE_NAME, __func__);
		return 0;
	}

	unsigned char buffer[PUSHSW_MAX_BUFLEN];
	sprintf(buffer, "%d\n", gpio_value);

	count = strlen(buffer);

	// pushswドライバがもつテキスト情報をユーザ空間へコピーする
	// copy_to_userで、buf宛に、dev_info->bufferのデータを、countバイトコピーする
	// コピーできなかったバイト数が返り値で渡される
	if (copy_to_user((void *)buf, &buffer, count)) {
		printk(KERN_ERR "%s %s: copy_to_user() failed.\n",
            PUSHSW_DEVICE_NAME, __func__);
		return -1;
	}
	*f_pos += count;

    return count;
}

static struct file_operations pushsw_fops = {
    .open = pushsw_open,
    .release = pushsw_release,
    .read = pushsw_read,
};

int register_pushsw_dev(void)
{
	int retval;
	dev_t dev;

	// 動的にメジャー番号を確保する
	retval = alloc_chrdev_region(&dev, PUSHSW_BASE_MINOR, PUSHSW_MAX_MINORS, PUSHSW_DEVICE_NAME);
	if (retval < 0) {
		// 確保できなかったらエラーを返して終了
		printk(KERN_ERR "%s %s: unable to allocate device number\n",
            PUSHSW_DEVICE_NAME, __func__);
		return retval;
	}

	// デバイスのクラスを登録する(/sys/class/***/ を作成)
	// ドライバが動いてる最中はクラスを保持するため、クラスはグローバル変数である
	pushsw_class	= class_create(THIS_MODULE, PUSHSW_DEVICE_NAME);
	if (IS_ERR(pushsw_class)) {
		// 登録できなかったらエラー処理に移動する
		retval = PTR_ERR(pushsw_class);
		printk(KERN_ERR "%s %s: class creation failed\n",
            PUSHSW_DEVICE_NAME, __func__);
		goto failed_class_create;
	}

	// マイナー番号ごとに(デバイスの数だけ)、ドライバの登録をする
	pushsw_major = MAJOR(dev);
	for (int i = 0; i < PUSHSW_MAX_MINORS; i++){
		// ドライバの初期化する
		// file_operationを登録するので、ここでドライバの機能が決まる
		cdev_init(&stored_device_info[i].cdev, &pushsw_fops);
		stored_device_info[i].cdev.owner = THIS_MODULE;

		// ドライバをカーネルへ登録する
		// 同じドライバを複数作成するときは、cdev_add(*,*, 3)みたいに末尾の数値を増やす
		// 今回はドライバごとにメモリを確保したいので、cdev_add()自体を複数回実行する
		retval = cdev_add(&stored_device_info[i].cdev,
			MKDEV(pushsw_major, PUSHSW_BASE_MINOR + i), 1);
		if (retval < 0) {
			// 登録できなかったらエラー処理へ移動する
			printk(KERN_ERR "%s: minor=%d: chardev registration failed\n",
				PUSHSW_DEVICE_NAME, PUSHSW_BASE_MINOR + i);
			goto failed_cdev_add;
		}

		// ドライバによっては、ここでエラー検出してたりしてなかったりする
		device_create(pushsw_class, NULL, MKDEV(pushsw_major, PUSHSW_BASE_MINOR + i),
			NULL, "%s%u", PUSHSW_DEVICE_NAME, i);
	}

	return 0;

failed_cdev_add:
	class_destroy(pushsw_class);
failed_class_create:
	unregister_chrdev_region(MKDEV(pushsw_major, PUSHSW_BASE_MINOR), PUSHSW_MAX_MINORS);
	return retval;
}

void unregister_pushsw_dev(void){
	// 基本的にはregister_pushsw_devの逆の手順でメモリを開放していく
	for (int i = 0; i < PUSHSW_MAX_MINORS; i++){
		device_destroy(pushsw_class, MKDEV(pushsw_major, PUSHSW_BASE_MINOR + i));
		cdev_del(&stored_device_info[i].cdev);
	}
	class_destroy(pushsw_class);
	unregister_chrdev_region(MKDEV(pushsw_major, PUSHSW_BASE_MINOR), PUSHSW_MAX_MINORS);
}