// SPDX-License-Identifier: GPL-2.0

#include <linux/cdev.h>	   // cdev_*()
#include <linux/delay.h>  // usleep();
#include <linux/fs.h>	   // struct file, open, release
#include <linux/i2c.h> // i2c_*()
#include <linux/module.h>  // MODULE_DEVICE_TABLE()
#include <linux/uaccess.h> // copy_to_user()

#define I2C_DRIVER_NAME "frootspi_aqm0802a_driver"
#define WAIT_TIME_USEC_MIN 27
#define WAIT_TIME_USEC_MAX 100
#define LCD_BASE_MINOR 0
#define LCD_MAX_MINORS 1
#define LCD_DEVICE_NAME "frootspi_lcd"

// ---------- I2Cドライバ用 ----------
// デバイスを識別するテーブル { "name", "好きなデータ"}を追加する
// カーネルはこの"name"をもとに対応するデバイスドライバを探す
static struct i2c_device_id aqm0802a_id_table[] = {
	{"aqm0802a", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, aqm0802a_id_table);

// I2Cドライバの設定に使われる構造体
static struct i2c_board_info aqm0802a_info = {
	I2C_BOARD_INFO("aqm0802a", 0x3e)
};

// I2Cクライアント
// 生成・削除するためにグローバル変数として記憶する
static struct i2c_client *aqm0802a_client = NULL;

// ---------- I2Cドライバ、キャラクタデバイス共用 ----------
struct lcd_device_info {
	// ここはある程度自由に定義できる
	struct cdev cdev;
	struct class *device_class;
	unsigned int device_major;
	unsigned int device_minor;
	struct i2c_client *client;
	struct mutex my_mutex;
};


// キャラクタデバイスで使うAQM0802Aの関数は前方宣言する
static int aqm0802a_write_lines(struct i2c_client *client, char *text);

static int lcd_open(struct inode *inode, struct file *filep)
{
	struct lcd_device_info *dev_info;
	// container_of(メンバーへのポインタ, 構造体の型, 構造体メンバの名前)
	dev_info = container_of(inode->i_cdev, struct lcd_device_info, cdev);

	if (dev_info == NULL || dev_info->client == NULL){
		printk(KERN_ERR "%s %s: dev_info or dev_info->client is NULL.\n",
			LCD_DEVICE_NAME, __func__);
	}

	// ここに何かしらのエラー処理がいるかも

	dev_info->device_minor = MINOR(inode->i_rdev);
	filep->private_data = dev_info;

	printk(KERN_DEBUG "%s %s: lcd device opend.\n", LCD_DEVICE_NAME,
		__func__);
	return 0;
}

static int lcd_release(struct inode *inode, struct file *filep)
{
	// デバイスによっては何もしなかったりする
	// kfree(filep->private_data);
	printk(KERN_DEBUG "%s %s: lcd device closed.\n", LCD_DEVICE_NAME,
		__func__);

	return 0;
}

static ssize_t lcd_write(
	struct file *filep, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct lcd_device_info *dev_info = filep->private_data;
	struct i2c_client *client = dev_info->client;

	unsigned char text_buffer[255];

	if (copy_from_user(text_buffer, buf, count) != 0) {
		printk(KERN_ERR "%s %s: copy_from_user() failed.\n",
			LCD_DEVICE_NAME, __func__);
		return -1;
	}

	// 1行書き込む
	aqm0802a_write_lines(client, text_buffer);

	return count;
}

static struct file_operations lcd_fops = {
	.open = lcd_open,
	.release = lcd_release,
	.write = lcd_write,
};

static int register_lcd_dev(struct lcd_device_info *dev_info)
{
	int retval;
	dev_t dev;

	// 動的にメジャー番号を確保する
	retval = alloc_chrdev_region(
		&dev, LCD_BASE_MINOR, LCD_MAX_MINORS, LCD_DEVICE_NAME);
	if (retval < 0) {
		// 確保できなかったらエラーを返して終了
		printk(KERN_ERR "%s %s: unable to allocate device number\n",
			LCD_DEVICE_NAME, __func__);
		return retval;
	}

	// マイナー番号ごとに(デバイスの数だけ)、ドライバの登録をする
	dev_info->device_major = MAJOR(dev);

	// デバイスのクラスを登録する(/sys/class/***/ を作成)
	// ドライバが動いてる最中はクラスを保持するため、クラスはグローバル変数である
	dev_info->device_class = class_create(THIS_MODULE, LCD_DEVICE_NAME);
	if (IS_ERR(dev_info->device_class)) {
		// 登録できなかったらエラー処理に移動する
		retval = PTR_ERR(dev_info->device_class);
		printk(KERN_ERR "%s %s: class creation failed\n",
			LCD_DEVICE_NAME, __func__);
		goto failed_class_create;
	}
	// ドライバの初期化する
	// file_operationを登録するので、ここでドライバの機能が決まる
	cdev_init(&dev_info->cdev, &lcd_fops);
	dev_info->cdev.owner = THIS_MODULE;

	// ドライバをカーネルへ登録する
	// 同じドライバを複数作成するときは、cdev_add(*,*,
	// 3)みたいに末尾の数値を増やす
	// 今回はドライバごとにメモリを確保したいので、cdev_add()自体を複数回実行する
	retval = cdev_add(&dev_info->cdev,
		MKDEV(dev_info->device_major, LCD_BASE_MINOR), 1);
	if (retval < 0) {
		// 登録できなかったらエラー処理へ移動する
		printk(KERN_ERR "%s %s: minor=%d: chardev registration "
				"failed\n",
			LCD_DEVICE_NAME, __func__,
			LCD_BASE_MINOR);
		goto failed_cdev_add;
	}

	// ドライバによっては、ここでエラー検出してたりしてなかったりする
	device_create(dev_info->device_class, NULL,
		MKDEV(dev_info->device_major, LCD_BASE_MINOR), NULL, "%s%u",
		LCD_DEVICE_NAME, LCD_BASE_MINOR);

	return 0;

failed_cdev_add:
	class_destroy(dev_info->device_class);
failed_class_create:
	unregister_chrdev_region(
		MKDEV(dev_info->device_major, LCD_BASE_MINOR), LCD_MAX_MINORS);
	return retval;
}

void unregister_lcd_dev(struct lcd_device_info *dev_info)
{
	// 基本的にはregister_lcd_devの逆の手順でメモリを開放していく
	device_destroy(
		dev_info->device_class, MKDEV(dev_info->device_major, LCD_BASE_MINOR));
	cdev_del(&dev_info->cdev);
	class_destroy(dev_info->device_class);
	unregister_chrdev_region(
		MKDEV(dev_info->device_major, LCD_BASE_MINOR), LCD_MAX_MINORS);
}

static int aqm0802a_write_command_byte(struct i2c_client *client,
	const unsigned char data)
{
	const unsigned char CONTROL_COMMAND_BYTE = 0x00;
	int retval = i2c_smbus_write_byte_data(client, CONTROL_COMMAND_BYTE, data);
	if(retval < 0){
		printk(KERN_ERR "%s %s: write_byte_data 0x%x failed. error: %d\n",
			I2C_DRIVER_NAME, __func__,
			data, retval);
		return -1;
	}
	usleep_range(WAIT_TIME_USEC_MIN, WAIT_TIME_USEC_MAX);
	return 0;
}

static int aqm0802a_set_function(struct i2c_client *client,
	const unsigned char bus_8bit,
	const unsigned char display_2line, const unsigned char double_height_font,
	const unsigned char instruction_table)
{
	unsigned char data = 0x20;
	data |= bus_8bit << 4;
	data |= display_2line << 3;
	data |= double_height_font << 2;
	data |= instruction_table << 0;
	
	return aqm0802a_write_command_byte(client, data);
}

static int aqm0802a_set_osc_freq(struct i2c_client *client,
	const unsigned char bias,
	const unsigned char f2, const unsigned char f1, const unsigned char f0)
{
	// Instruction Table を1にしないと設定できない
	// 内部クロックの周波数を決める
	// 詳細はST7032のデータシートの表を見て
	// https://strawberry-linux.com/pub/ST7032i.pdf
	unsigned char data = 0x10;
	data |= bias << 3;
	data |= f2 << 2;
	data |= f1 << 1;
	data |= f0 << 0;

	return aqm0802a_write_command_byte(client, data);
}

static int aqm0802a_set_contrast_lowbyte(struct i2c_client *client,
	const unsigned char c3, const unsigned char c2,
	const unsigned char c1, const unsigned char c0)
{
	// Instruction Table を1にしないと設定できない
	// コントラスト(C5 ~ C0)のうちLow Byteをセットする
	// 詳細はST7032のデータシートの表を見て
	// https://strawberry-linux.com/pub/ST7032i.pdf
	unsigned  char data = 0x70;
	data |= c3 << 3;
	data |= c2 << 2;
	data |= c1 << 1;
	data |= c0 << 0;

	return aqm0802a_write_command_byte(client, data);
}

static int aqm0802a_set_power_and_contruct_highbits(struct i2c_client *client,
	const unsigned char icon_display_on,
	const unsigned char booster_on,
	const unsigned char c5, const unsigned char c4)
{
	// Instruction Table を1にしないと設定できない
	// ICONと昇圧回路をON/OFFする
	// コントラスト(C5 ~ C0)のうちC5, C4をセットする
	// 詳細はST7032のデータシートの表を見て
	// https://strawberry-linux.com/pub/ST7032i.pdf
	unsigned char data = 0x50;
	data |= icon_display_on << 3;
	data |= booster_on << 2;
	data |= c5 << 1;
	data |= c4 << 0;

	return aqm0802a_write_command_byte(client, data);
}

static int aqm0802a_set_follower_control(struct i2c_client *client,
	const unsigned char follower_on,
	const unsigned char rab2, const unsigned char rab1,
	const unsigned char rab0)
{
	// Instruction Table を1にしないと設定できない
	// ボルテージフォロワをON/OFFし、増幅率設定用の抵抗Rab2~0を設定する
	// 詳細はST7032のデータシートの表を見て
	// https://strawberry-linux.com/pub/ST7032i.pdf
	unsigned char data = 0x60;
	data |= follower_on << 3;
	data |= rab2 << 2;
	data |= rab1 << 1;
	data |= rab0 << 0;

	int retval = aqm0802a_write_command_byte(client, data);
	msleep(200);  // 電源が安定するまで待機
	return retval;
}

static int aqm0802a_turn_on_display(struct i2c_client *client,
	const unsigned char display_on,
	const unsigned char cursor_on,
	const unsigned char cursor_blink_on)
{
	// ディスプレイ、カーソルの表示ON/OFF設定

	unsigned char data = 0x08;
	data |= display_on << 2;
	data |= cursor_on << 1;
	data |= cursor_blink_on << 0;

	return aqm0802a_write_command_byte(client, data);
}

static int aqm0802a_clear_display(struct i2c_client *client)
{
	// ディスプレイ全消去(RAMクリア)
	return aqm0802a_write_command_byte(client, 0x01);
}

static int aqm0802a_set_address(struct i2c_client *client,
	const unsigned char address)
{
	// カーソルを移動（RAMのアドレスを変更）
	// アドレスとディスプレイの関係
	// 1行目: 0x00 01 02 03 04 05 06 07
	// 2行目: 0x40 41 42 43 44 45 46 47
	if(address <= 0x07 || (address >= 0x40 && address <= 0x47)){
		unsigned char data = 0x80 | address;
		return aqm0802a_write_command_byte(client, data);
	}else{
		printk(KERN_ERR "%s %s: invalid LCD RAM address: %x\n", I2C_DRIVER_NAME,
			__func__, address);
		return -1;
	}
}

static int aqm0802a_write_data_byte(struct i2c_client *client,
	const unsigned char data)
{
	const unsigned char CONTROL_DATA_BYTE = 0x40;
	int retval = i2c_smbus_write_byte_data(client, CONTROL_DATA_BYTE, data);
	if(retval < 0){
		printk(KERN_ERR "%s %s: write_byte_data 0x%x failed. error: %d\n",
			I2C_DRIVER_NAME, __func__,
			data, retval);
		return -1;
	}
	usleep_range(WAIT_TIME_USEC_MIN, WAIT_TIME_USEC_MAX);
	return 0;
}

static int aqm0802a_write_lines(struct i2c_client *client, char *text)
{
	// LCDの全行に文字列を書き込む関数
	// 文字列を書き込む前に、LCDの表示をクリアする
	// textの中に改行コードが含まれていたら、書き込む行を変える
	// アスキーコードと半角カタカナに対応。それ以外の文字は空白になる
	// 2バイトや4バイト文字を入力されるとバグるので注意

	aqm0802a_clear_display(client);
	aqm0802a_set_address(client, 0x00);

	// 入力された文字のバイト数だけ繰り返す
	size_t text_size = strlen(text);
	for(int i = 0; i < text_size; i++){
		unsigned char converted_char = 0xa0;  // 空白

		if(text[i] == 0x0a){  // 改行
			aqm0802a_set_address(client, 0x40);
			continue;
		}

		if(text[i] < 0x7e){  // ASCII
			// ASCIIなのでそのまま書き込める
			converted_char = text[i];
		}else if(text[i] == 0xef){  // 半角カタカナ
			if(text[i+1] == 0xbd){
				converted_char = text[i+2];
			}else if(text[i+1] == 0xbe){
				converted_char = text[i+2] + 0x40;
			}
			i += 2;  // 3バイト文字なので、その分インクリメントする
		}
		
		aqm0802a_write_data_byte(client, converted_char);
	}

	return 0;
}

static int aqm0802a_init_device(struct i2c_client *client)
{
	// AQM0802Aの初期設定
	// Ref: http://akizukidenshi.com/download/ds/xiamen/AQM0802.pdf
	unsigned char bus_8bit = 1;
	unsigned char display_2line = 1;
	unsigned char double_height_font = 0;
	unsigned char instruction_table = 0;
	aqm0802a_set_function(client, bus_8bit, display_2line, double_height_font,
		instruction_table);

	instruction_table = 1;
	aqm0802a_set_function(client, bus_8bit, display_2line, double_height_font,
		instruction_table);
	aqm0802a_set_osc_freq(client, 0, 1, 0, 0);
	aqm0802a_set_contrast_lowbyte(client, 0, 0, 0, 0);
	aqm0802a_set_power_and_contruct_highbits(client, 0, 1, 1, 0);
	aqm0802a_set_follower_control(client, 1, 1, 0, 0);

	instruction_table = 0;
	aqm0802a_set_function(client, bus_8bit, display_2line, double_height_font,
		instruction_table);

	aqm0802a_turn_on_display(client, 1, 0, 0);
	aqm0802a_clear_display(client);
	return 0;
}

static int aqm0802a_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	printk(KERN_INFO "%s: new i2c device probed, id.name=%s, "
			 "id.driver_data=%d, addr=0x%x\n",
			 I2C_DRIVER_NAME, id->name, (int)(id->driver_data),
			 client->addr);

	struct lcd_device_info *dev_info;
	dev_info = kzalloc(sizeof(struct lcd_device_info), GFP_KERNEL);
	if (dev_info == NULL) {
		printk(KERN_ERR "%s %s: kszalloc() failed\n", I2C_DRIVER_NAME,
			__func__);
		return -1;
	}
	dev_info->client = client;
	i2c_set_clientdata(client, dev_info);
	mutex_init(&dev_info->my_mutex);

	// LCDの初期化
	aqm0802a_init_device(client);
	aqm0802a_write_lines(client, "FrootsPI\nﾌﾙｰﾂﾊﾟｲ!");

	// キャラクタデバイスの登録
	return register_lcd_dev(dev_info);
}

static int aqm0802a_remove(struct i2c_client *client)
{
	struct lcd_device_info *dev_info;
	dev_info = i2c_get_clientdata(client);
	unregister_lcd_dev(dev_info);
	kfree(dev_info);

	printk(KERN_INFO "%s %s: i2c device removed.\n",
		I2C_DRIVER_NAME, __func__);
	return 0;
}

static struct i2c_driver aqm0802a_driver = {
	.driver =
		{
			.name = I2C_DRIVER_NAME,
			.owner = THIS_MODULE,
		},
	.id_table = aqm0802a_id_table,
	.probe = aqm0802a_probe,
	.remove = aqm0802a_remove,
};

int register_aqm0802a_driver(void)
{
	printk(KERN_INFO "%s %s: register.\n", I2C_DRIVER_NAME, __func__);
	// I2Cドライバをカーネルに登録

	int retval = i2c_add_driver(&aqm0802a_driver);
	if(retval){
		printk(KERN_ERR "%s %s: i2c_add_driver() failed.\n",
			I2C_DRIVER_NAME, __func__);
		return retval;
	}

	struct i2c_adapter *i2c_adap = i2c_get_adapter(1);
	aqm0802a_client = i2c_new_device(i2c_adap, &aqm0802a_info);
	i2c_put_adapter(i2c_adap);

	return 0;
}

void unregister_aqm0802a_driver(void)
{
	printk(KERN_INFO "%s %s: unregister.\n", I2C_DRIVER_NAME, __func__);
	i2c_del_driver(&aqm0802a_driver);
	if(aqm0802a_client)
		i2c_unregister_device(aqm0802a_client);
}
