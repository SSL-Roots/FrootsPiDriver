#include <linux/module.h>
#include <linux/spi/spi.h>  // spi_*()

#include "mcp23s08_driver.h"

// ---------- SPI driver ----------
#define SPI_DRIVER_NAME "frootspi_mcp23s08_driver"
#define SPI_BUS_NUM 1
#define SPI_CHIP_SELECT 0
#define MCP23S08_PACKET_SIZE 3
#define MCP23S08_WORD_SIZE 8
#define MCP23S08_PIN_A0 0  // A0ピンの値（電位）(1/0)
#define MCP23S08_PIN_A1 0  // A1ピンの値（電位）(1/0)
#define MCP23S08_READ 1
#define MCP23S08_WRITE 0
#define MCP23S08_REG_IODIR   0x00  // 入出力設定
#define MCP23S08_REG_IPOL    0x01  // 入力極性設定（GPIOの論理反転できる）
#define MCP23S08_REG_GPINTEN 0x02  // 割り込みON/OFF設定
#define MCP23S08_REG_DEFVAL  0x03  // 割り込みのデフォルト値設定
#define MCP23S08_REG_INTCON  0x04  // 割り込み制御設定
#define MCP23S08_REG_IOCON   0x05  // エキスパンダ全体の設定
#define MCP23S08_REG_GPPU    0x06  // プルアップ設定
#define MCP23S08_REG_INTF    0x07  // 割り込みフラグ
#define MCP23S08_REG_INTCAP  0x08  // 割り込み時の論理レベル
#define MCP23S08_REG_GPIO    0x09  // GPIO
#define MCP23S08_REG_OLAT    0x0a  // 出力ラッチレジスタ
#define MCP23S08_REG_SIZE    0x0b

// デバイスを識別するテーブル { "name", "好きなデータ"}を追加する
// カーネルはこの"name"をもとに対応するデバイスドライバを探す
// もしmcp23s08用のデバイスドライバが存在したら、それが使われる
//  → mcp23s08ドライバが存在するっぽい
static struct spi_device_id mcp23s08_id_table[] = {
    {"mcp23s08", 0},
	{},
};
MODULE_DEVICE_TABLE(spi, mcp23s08_id_table);

// SPIドライバの設定に使われる構造体
static struct spi_board_info mcp23s08_info = {
	.modalias = "mcp23s08",  // デバイス名
    .max_speed_hz = 1000000,  // SCLKの最大周波数
    .bus_num = SPI_BUS_NUM,  // SPIバス番号。0 or 1
    .chip_select = SPI_CHIP_SELECT,  // チップセレクト番号。0~2
    .mode = SPI_MODE_0,  // SPIモード
};

// SPI通信に使うデータをまとめた構造体
struct mcp23s08_drvdata {
	struct spi_device *spi;
	struct mutex my_mutex;
	// DMAに怒られないために送受信バッファのアラインメントを整える
	unsigned char tx[MCP23S08_PACKET_SIZE] ____cacheline_aligned;
	unsigned char rx[MCP23S08_PACKET_SIZE] ____cacheline_aligned;
	struct spi_transfer xfer ____cacheline_aligned;
	struct spi_message msg ____cacheline_aligned;
};

static unsigned int mcp23s08_control_reg(const unsigned char reg, const unsigned char rw,
	const unsigned char write_data, unsigned char *read_data)
{
	char str[128];

	// SPIバス番号に紐付いたマスターを取得
	struct spi_master *master = spi_busnum_to_master(mcp23s08_info.bus_num);
	// SPIマスターが使用しているバスとCSを文字列に変換
	snprintf(str, sizeof(str), "%s.%u", dev_name(&master->dev),
		 mcp23s08_info.chip_select);

	// 文字列"{bus}.{cs}"をもとに、デバイスを取得
	struct device *dev = bus_find_device_by_name(&spi_bus_type, NULL, str);
	// struct deviceを struct spi_deviceに変換
	struct spi_device *spi = to_spi_device(dev);
	struct mcp23s08_drvdata *data = (struct mcp23s08_drvdata *)spi_get_drvdata(spi);

	// 排他制御開始！
	mutex_lock(&data->my_mutex);
	// tx[0] = Opcode = 0b0100_0{A1}{A0}{R/W}
	data->tx[0] = 0x40;
	data->tx[0] |= MCP23S08_PIN_A1 << 2;
	data->tx[0] |= MCP23S08_PIN_A0 << 1;
	data->tx[0] |= rw << 0;
	data->tx[1] = reg;
	data->tx[2] = write_data;
	int retval = spi_sync(data->spi, &data->msg);
	// 排他制御終了
	mutex_unlock(&data->my_mutex);

	if(retval){
		printk(KERN_WARNING "%s %s: spi_sync() failed.\n",
			SPI_DRIVER_NAME, __func__);
	}else{
		*read_data = data->rx[2];
	}

	return retval;
}

// MCP23S08のレジスタ設定
// GPIOの入出力設定や割り込み設定等をここで行う
static int mcp23s08_initialize_reg(void)
{
	unsigned char txdata = 0;
	unsigned char rxdata = 0;
	// 入出力ピンの設定
	txdata = 0xFF ^ (1 << MCP23S08_GPIO_LED);
	if(mcp23s08_control_reg(MCP23S08_REG_IODIR, MCP23S08_WRITE, txdata, &rxdata)){
		printk(KERN_ERR "%s %s: failed to initialize IODIR.\n",
			SPI_DRIVER_NAME, __func__);
		return -1;
	}

	return 0;
}


static int mcp23s08_probe(struct spi_device *spi)
{
	spi->max_speed_hz = mcp23s08_info.max_speed_hz;
	spi->mode = mcp23s08_info.mode;
	spi->bits_per_word = MCP23S08_WORD_SIZE;
	// SPI モード、クロックレート、ワードサイズを設定
	// 異常な値が設定される場合はspi_setup()が失敗する
	if (spi_setup(spi)){
		printk(KERN_ERR "%s %s: spi_setup() failed\n",
			SPI_DRIVER_NAME, __func__);
		return -1;
	}

	// kzalloc: mallocのカーネル空間版のメモリーゼロクリア版
	// カーネル空間に指定サイズのメモリを確保し、ゼロクリアする
	// GFP_KERNEL: スリープ可、標準的なメモリ確保
	// 他にはGFP_USERや、GFP_DMA、GFP_ATOMICなどがある
	// コメント：drvdataはグローバル変数で静的に確保して良い気がするけどどうなんだろ？
	struct mcp23s08_drvdata *data;
	data = kzalloc(sizeof(struct mcp23s08_drvdata), GFP_KERNEL);
	if (data == NULL) {
		printk(KERN_ERR "%s %s: kszalloc() failed\n",
			SPI_DRIVER_NAME, __func__);
		return -1;
	}

	data->spi = spi;

	// mutexで排他制御する
	// 複数のプロセスがspi_syncを実行すると、通信がぶっ壊れるため
	// 例：SWの状態を監視するスレッドと、LEDの状態を変えるスレッドが同時に動く状況
	mutex_init(&data->my_mutex);

	data->xfer.tx_buf = data->tx;
	data->xfer.rx_buf = data->rx;
	data->xfer.bits_per_word = MCP23S08_WORD_SIZE;
	data->xfer.len = MCP23S08_PACKET_SIZE;
	data->xfer.cs_change = 0;  // 送信完了後のCSの状態
	data->xfer.delay_usecs = 0;  // 送信からCS状態変更までの遅延時間
	data->xfer.speed_hz = 1000000;  // 1MHz

	// spi_transfer 構造体からspi_message構造体を作成する
	// 変更先spi_message, 変更元spi_transfer, transferの数
	spi_message_init_with_transfers(&data->msg, &data->xfer, 1);

	// ドライバ(spi)にプライベートデータ(data)を紐付けて保存する
	// プライベートデータはspi_get_drvdata() or dev_get_drvdata()で取得できる
	spi_set_drvdata(spi, data);

	if(mcp23s08_initialize_reg()){
		printk(KERN_ERR "%s %s: mcp23s08_initialzie_reg() failed\n",
			SPI_DRIVER_NAME, __func__);
		return -1;
	}
	printk(KERN_DEBUG "%s %s: mcp23s08 probed", SPI_DRIVER_NAME, __func__);

	return 0;
}

static int mcp23s08_remove(struct spi_device *spi)
{
	// ドライバに紐付いたプライベートデータを取得
	struct mcp23s08_drvdata *data;
	data = (struct mcp23s08_drvdata *)spi_get_drvdata(spi);
	// プライベートデータを開放
	kfree(data);

	printk(KERN_DEBUG "%s %s: mcp23s08 removed", SPI_DRIVER_NAME, __func__);

	return 0;
}

static struct spi_driver mcp23s08_driver = {
    .driver =
	{
	    .name = SPI_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
    .id_table = mcp23s08_id_table,
    .probe = mcp23s08_probe,
    .remove = mcp23s08_remove,
};

static void spi_remove_device(struct spi_master *master, unsigned int cs)
{
	struct device *dev;
	char str[128];

	// SPIマスターが使用しているバスと指定したChip Selectを文字列に変換
	snprintf(str, sizeof(str), "%s.%u", dev_name(&master->dev), cs);

	// 文字列"{bus}.{cs}"をもとに、デバイスを取得
	dev = bus_find_device_by_name(&spi_bus_type, NULL, str);
	if (dev){  // デバイスが存在していたら削除
		device_del(dev);
	}
}

int register_mcp23s08_driver(void)
{
	// SPIドライバをカーネルに登録
	spi_register_driver(&mcp23s08_driver);

	mcp23s08_info.bus_num = SPI_BUS_NUM;
	mcp23s08_info.chip_select = SPI_CHIP_SELECT;

	// SPIバス番号に紐付いたマスターを取得
	struct spi_master *master = spi_busnum_to_master(mcp23s08_info.bus_num);
	if (!master) {
		printk(KERN_ERR "%s %s: spi_busnum_to_master returned NULL\n",
			SPI_DRIVER_NAME, __func__);
		spi_unregister_driver(&mcp23s08_driver);
		return -1;
	}

	// なんでremoveするの？
	// すでに/dev/spidev0.0や0.1などがSPIバスを使ってるかもしれないので取り除く
	spi_remove_device(master, mcp23s08_info.chip_select);

	// 新しいSPIデバイスをインスタンス化する
	// 成功した直後、mcp23s08_driverのprobeが実行される
	struct spi_device *spi_device = spi_new_device(master, &mcp23s08_info);
	if (!spi_device) {
		printk(KERN_ERR "%s %s: spi_new_device returned NULL\n",
			SPI_DRIVER_NAME, __func__);
		spi_unregister_driver(&mcp23s08_driver);
		return -1;
	}

	return 0;
}

void unregister_mcp23s08_driver(void)
{
	// SPIバス番号に紐付いたマスターを取得
	struct spi_master *master = spi_busnum_to_master(mcp23s08_info.bus_num);
	if(master) {
		spi_remove_device(master, mcp23s08_info.chip_select);
	}else{
		printk(KERN_ERR "%s %s: mcp23s08 remove error\n",
			SPI_DRIVER_NAME, __func__);
	}

	// カーネルからドライバを取り除く
	spi_unregister_driver(&mcp23s08_driver);
}

// MCP23S08のGPIOの値を取得
// 失敗した場合は-1を返す
int mcp23s08_read_gpio(const unsigned char gpio_num)
{
	unsigned char txdata = 0;
	unsigned char rxdata = 0;
	if(mcp23s08_control_reg(MCP23S08_REG_GPIO, MCP23S08_READ, txdata, &rxdata)){
		printk(KERN_ERR "%s %s: failed to read GPIO.\n",
			SPI_DRIVER_NAME, __func__);
		return -1;
	}

	return (rxdata >> gpio_num) & 1;
}

// MCP23S08のGPIOに値をセット
// 失敗した場合は-1を返す
int mcp23s08_write_gpio(const unsigned char gpio_num, const unsigned char value)
{
	unsigned char txdata = 0;
	unsigned char rxdata = 0;

	// 現在のGPIOの状態を取得
	if(mcp23s08_control_reg(MCP23S08_REG_GPIO, MCP23S08_READ, txdata, &rxdata)){
		printk(KERN_ERR "%s %s: failed to read from GPIO.\n",
			SPI_DRIVER_NAME, __func__);
		return -1;
	}

	// 指定されたGPIOビットだけ変更する
	if (value == 0){
		txdata = rxdata & ~(1 << gpio_num);
	}else if(value == 1){
		txdata = rxdata | (1 << gpio_num);
	}else{
		printk(KERN_ERR "%s %s: Invalid value: %d.\n",
			SPI_DRIVER_NAME, __func__, value);
		return -1;
	}

	if(mcp23s08_control_reg(MCP23S08_REG_GPIO, MCP23S08_WRITE, txdata, &rxdata)){
		printk(KERN_ERR "%s %s: failed to write to GPIO.\n",
			SPI_DRIVER_NAME, __func__);
		return -1;
	}

	return 0;
}