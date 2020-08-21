// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>  // usleep();
#include <linux/i2c.h> // i2c_*()
#include <linux/module.h>  // MODULE_DEVICE_TABLE()

#define I2C_DRIVER_NAME "frootspi_aqm0802a_driver"
#define WAIT_TIME_USEC_MIN 27
#define WAIT_TIME_USEC_MAX 100

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

// I2C通信に使うデータをまとめた構造体
struct aqm0802a_drvdata{
	struct i2c_client *client;
	struct mutex my_mutex;
};

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

static int aqm0802a_write_line(struct i2c_client *client,
	const unsigned char second_line, char *text)
{
	// LCDの1行に文字列を書き込む関数
	// 書き込む行(second_line = 0 or 1)を選択する
	// アスキーコードと半角カタカナに対応。それ以外の文字は空白になる
	// 2バイトや4バイト文字を入力されるとバグるので注意

	if(second_line){
		aqm0802a_set_address(client, 0x40);
	}else{
		aqm0802a_set_address(client, 0x00);
	}

	// 入力された文字のバイト数だけ繰り返す
	size_t text_size = strlen(text);
	for(int i = 0; i < text_size; i++){
		unsigned char converted_char = 0xa0;  // 空白

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

	struct aqm0802a_drvdata *data;
	data = kzalloc(sizeof(struct aqm0802a_drvdata), GFP_KERNEL);
	if (data == NULL) {
		printk(KERN_ERR "%s %s: kszalloc() failed\n", I2C_DRIVER_NAME,
			__func__);
		return -1;
	}
	data->client = client;
	i2c_set_clientdata(client, data);
	mutex_init(&data->my_mutex);

	// LCDの初期化
	aqm0802a_init_device(client);
	aqm0802a_write_line(client, 0, "FrootsPi");
	aqm0802a_write_line(client, 1, "ﾌﾙｰﾂﾊﾟｲ!");

	return 0;
}

static int aqm0802a_remove(struct i2c_client *client)
{
	struct aqm0802a_drvdata *data;
	data = i2c_get_clientdata(client);
	kfree(data);

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
