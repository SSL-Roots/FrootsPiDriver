#include <linux/cdev.h>  // cdev_*()
#include <linux/fs.h>  // struct file, open, release
// #include <linux/slab.h>  // kmalloc()
#include <linux/uaccess.h>  // copy_to_user()

#define HELLO_BASE_MINOR 0
#define HELLO_MAX_MINORS 3
#define HELLO_DEVICE_NAME "frootspi_hello"
#define HELLO_NUM_BUFFER 256

static struct class *hello_class;
static int hello_major;
struct hello_device_info {
	// ここはある程度自由に定義できる
	struct cdev cdev;
	unsigned int device_major;
	unsigned int device_minor;
    unsigned char buffer[HELLO_NUM_BUFFER];
};
static struct hello_device_info stored_device_info[HELLO_MAX_MINORS];


static int hello_open(struct inode *inode, struct file *filep)
{
	printk("hello_open\n");

	struct hello_device_info *dev_info;
	// container_of(メンバーへのポインタ, 構造体の型, 構造体メンバの名前)
	dev_info = container_of(inode->i_cdev, struct hello_device_info, cdev);

	// ここに何かしらのエラー処理がいるかも

	dev_info->device_major = MAJOR(inode->i_rdev);
	dev_info->device_minor = MINOR(inode->i_rdev);

	filep->private_data = dev_info;

	return 0;
}

static int hello_release(struct inode *inode, struct file *filep)
{
	printk("hello_close\n");

	// デバイスによっては何もしなかったりする
	// kfree(filep->private_data);
	return 0;
}

static ssize_t hello_read(struct file *filep, char __user *buf, size_t count,
			     loff_t *f_pos)
{
	struct hello_device_info *dev_info = filep->private_data;
	printk("helo_read, major:%d, minor:%d\n",
		dev_info->device_major,
		dev_info->device_minor);

	// オフセットがあったら正常終了する
	// 短い文字列しかコピーしないので、これで問題なし
	// 長い文字列をコピーするばあいは、オフセットが重要
	if (*f_pos > 0) {
		return 0;  // EOF
	}

	// readするサイズに変更する
	// nバイトだけreadする、という使い方を想定していない
	count = strlen(dev_info->buffer);

	// helloドライバがもつテキスト情報をユーザ空間へコピーする
	// copy_to_userで、buf宛に、dev_info->bufferのデータを、countバイトコピーする
	// コピーできなかったバイト数が返り値で渡される
	if (copy_to_user((void *)buf, dev_info->buffer, count)) {
		printk(KERN_INFO "ERROR read\n");
		return -1;
	}
	*f_pos += count;
    return count;
}

static ssize_t hello_write(struct file *filep, const char __user *buf,
			      size_t count, loff_t *f_pos)
{
	struct hello_device_info *dev_info = filep->private_data;
	printk("hello_write, major:%d, minor:%d\n",
		dev_info->device_major,
		dev_info->device_minor);

    if (copy_from_user(dev_info->buffer, buf, count) != 0) {
		printk(KERN_INFO "ERROR read\n");
        return -1;
    }

    return count;
}

static struct file_operations hello_fops = {
    .open = hello_open,
    .release = hello_release,
    .read = hello_read,
    .write = hello_write,
};

int register_hello_dev(void)
{
	int retval;
	dev_t dev;

	// 動的にメジャー番号を確保する
	retval = alloc_chrdev_region(&dev, HELLO_BASE_MINOR, HELLO_MAX_MINORS, HELLO_DEVICE_NAME);
	if (retval < 0) {
		// 確保できなかったらエラーを返して終了
		printk(KERN_ERR "%s: unable to allocate device number\n", HELLO_DEVICE_NAME);
		return retval;
	}

	// デバイスのクラスを登録する(/sys/class/***/ を作成)
	// ドライバが動いてる最中はクラスを保持するため、クラスはグローバル変数である
	hello_class	= class_create(THIS_MODULE, HELLO_DEVICE_NAME);
	if (IS_ERR(hello_class)) {
		// 登録できなかったらエラー処理に移動する
		retval = PTR_ERR(hello_class);
		printk(KERN_ERR "%s: class creation failed\n", HELLO_DEVICE_NAME);
		goto failed_class_create;
	}

	// マイナー番号ごとに(デバイスの数だけ)、ドライバの登録をする
	hello_major = MAJOR(dev);
	for (int i = 0; i < HELLO_MAX_MINORS; i++){
		// ドライバの初期化する
		// file_operationを登録するので、ここでドライバの機能が決まる
		cdev_init(&stored_device_info[i].cdev, &hello_fops);
		stored_device_info[i].cdev.owner = THIS_MODULE;

		// ドライバをカーネルへ登録する
		// 同じドライバを複数作成するときは、cdev_add(*,*, 3)みたいに末尾の数値を増やす
		// 今回はドライバごとにメモリを確保したいので、cdev_add()自体を複数回実行する
		retval = cdev_add(&stored_device_info[i].cdev,
			MKDEV(hello_major, HELLO_BASE_MINOR + i), 1);
		if (retval < 0) {
			// 登録できなかったらエラー処理へ移動する
			printk(KERN_ERR "%s: minor=%d: chardev registration failed\n",
				HELLO_DEVICE_NAME, HELLO_BASE_MINOR + i);
			goto failed_cdev_add;
		}

		// ドライバによっては、ここでエラー検出してたりしてなかったりする
		device_create(hello_class, NULL, MKDEV(hello_major, HELLO_BASE_MINOR + i),
			NULL, "%s%u", HELLO_DEVICE_NAME, i);
	}

	return 0;

failed_cdev_add:
	class_destroy(hello_class);
failed_class_create:
	unregister_chrdev_region(MKDEV(hello_major, HELLO_BASE_MINOR), HELLO_MAX_MINORS);
	return retval;
}

void unregister_hello_dev(void){
	// 基本的にはregister_hello_devの逆の手順でメモリを開放していく
	for (int i = 0; i < HELLO_MAX_MINORS; i++){
		device_destroy(hello_class, MKDEV(hello_major, HELLO_BASE_MINOR + i));
		cdev_del(&stored_device_info[i].cdev);
	}
	class_destroy(hello_class);
	unregister_chrdev_region(MKDEV(hello_major, HELLO_BASE_MINOR), HELLO_MAX_MINORS);
}