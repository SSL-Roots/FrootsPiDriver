#include <linux/cdev.h>  // cdev_*()
#include <linux/fs.h>  // struct file, open, release
#include <linux/module.h>
#include <linux/spi/spi.h>  // spi_*()
#include <linux/uaccess.h>  // copy_to_user()

#define MAX_BUFLEN 64  // copy_to_user用のバッファサイズ
// ---------- SPI driver ----------
#define SPI_DRIVER_NAME "frootspi_mcp23s08"
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
#define MCP23S08_GPIO_LED 0
#define MCP23S08_GPIO_PUSHSW0 1
#define MCP23S08_GPIO_PUSHSW1 2
#define MCP23S08_GPIO_PUSHSW2 3
#define MCP23S08_GPIO_PUSHSW3 4
#define MCP23S08_GPIO_TOGLSW0 5
#define MCP23S08_GPIO_TOGLSW1 6

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

// ---------- Push switches ----------
#define PUSHSW_BASE_MINOR 0
#define PUSHSW_MAX_MINORS 4
#define PUSHSW_DEVICE_NAME "frootspi_pushsw"

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


// ---------- SPI driver functions ----------

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
		printk(KERN_WARNING "%s: spi_sync() failed.\n", __func__);
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
		printk(KERN_ERR "%s: failed to initialize IODIR.\n", __func__);
		return -1;
	}

	return 0;
}

// MCP23S08のGPIOの値を取得
// 失敗した場合は-1を返す
static int mcp23s08_read_gpio(const unsigned char gpio_num)
{
	unsigned char txdata = 0;
	unsigned char rxdata = 0;
	if(mcp23s08_control_reg(MCP23S08_REG_GPIO, MCP23S08_READ, txdata, &rxdata)){
		printk(KERN_ERR "%s: failed to read GPIO.\n", __func__);
		return -1;
	}

	return (rxdata >> gpio_num) & 1;
}

static int mcp23s08_probe(struct spi_device *spi)
{
	printk("mcp23s08_probe\n");
	
	spi->max_speed_hz = mcp23s08_info.max_speed_hz;
	spi->mode = mcp23s08_info.mode;
	spi->bits_per_word = MCP23S08_WORD_SIZE;
	// SPI モード、クロックレート、ワードサイズを設定
	// 異常な値が設定される場合はspi_setup()が失敗する
	if (spi_setup(spi)){
		printk(KERN_ERR "%s: spi_setup() failed\n", __func__);
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
		printk(KERN_ERR "%s: kszalloc() failed\n", __func__);
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
		printk(KERN_ERR "%s: mcp23s08_initialzie_reg() failed\n", __func__);
		return -1;
	}
	printk(KERN_DEBUG "%s: mcp23s08 probed", SPI_DRIVER_NAME);

	return 0;
}

static int mcp23s08_remove(struct spi_device *spi)
{
	printk("mcp23s08_remove\n");

	// ドライバに紐付いたプライベートデータを取得
	struct mcp23s08_drvdata *data;
	data = (struct mcp23s08_drvdata *)spi_get_drvdata(spi);
	// プライベートデータを開放
	kfree(data);

	printk(KERN_DEBUG "%s: mcp23s08 removed", SPI_DRIVER_NAME);

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

int register_spi_dev(void)
{
	// SPIドライバをカーネルに登録
	spi_register_driver(&mcp23s08_driver);

	mcp23s08_info.bus_num = SPI_BUS_NUM;
	mcp23s08_info.chip_select = SPI_CHIP_SELECT;

	// SPIバス番号に紐付いたマスターを取得
	struct spi_master *master = spi_busnum_to_master(mcp23s08_info.bus_num);
	if (!master) {
		printk(KERN_ERR "%s: spi_busnum_to_master returned NULL\n", __func__);
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
		printk(KERN_ERR "%s: spi_new_device returned NULL\n", __func__);
		spi_unregister_driver(&mcp23s08_driver);
		return -1;
	}

	return 0;
}

void unregister_spi_dev(void)
{
	// SPIバス番号に紐付いたマスターを取得
	struct spi_master *master = spi_busnum_to_master(mcp23s08_info.bus_num);
	if(master) {
		spi_remove_device(master, mcp23s08_info.chip_select);
	}else{
		printk(KERN_ERR "mcp23s08 remove error\n");
	}

	// カーネルからドライバを取り除く
	spi_unregister_driver(&mcp23s08_driver);
}

// ---------- Push switches functions ----------

static int pushsw_open(struct inode *inode, struct file *filep)
{
	printk(KERN_DEBUG "pushsw_open\n");

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

	return 0;
}

static int pushsw_release(struct inode *inode, struct file *filep)
{
	printk(KERN_DEBUG "pushsw_close\n");

	// デバイスによっては何もしなかったりする
	// kfree(filep->private_data);
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
		printk(KERN_ERR "%s: mcp23s08_read_gpio() failed.\n", __func__);
		return 0;
	}

	unsigned char buffer[MAX_BUFLEN];
	sprintf(buffer, "%d\n", gpio_value);

	count = strlen(buffer);

	// pushswドライバがもつテキスト情報をユーザ空間へコピーする
	// copy_to_userで、buf宛に、dev_info->bufferのデータを、countバイトコピーする
	// コピーできなかったバイト数が返り値で渡される
	if (copy_to_user((void *)buf, &buffer, count)) {
		printk(KERN_ERR "%s: copy_to_user() failed.\n", __func__);
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

static int register_pushsw_dev(void)
{
	int retval;
	dev_t dev;

	// 動的にメジャー番号を確保する
	retval = alloc_chrdev_region(&dev, PUSHSW_BASE_MINOR, PUSHSW_MAX_MINORS, PUSHSW_DEVICE_NAME);
	if (retval < 0) {
		// 確保できなかったらエラーを返して終了
		printk(KERN_ERR "%s: unable to allocate device number\n", PUSHSW_DEVICE_NAME);
		return retval;
	}

	// デバイスのクラスを登録する(/sys/class/***/ を作成)
	// ドライバが動いてる最中はクラスを保持するため、クラスはグローバル変数である
	pushsw_class	= class_create(THIS_MODULE, PUSHSW_DEVICE_NAME);
	if (IS_ERR(pushsw_class)) {
		// 登録できなかったらエラー処理に移動する
		retval = PTR_ERR(pushsw_class);
		printk(KERN_ERR "%s: class creation failed\n", PUSHSW_DEVICE_NAME);
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

static void unregister_pushsw_dev(void){
	// 基本的にはregister_pushsw_devの逆の手順でメモリを開放していく
	for (int i = 0; i < PUSHSW_MAX_MINORS; i++){
		device_destroy(pushsw_class, MKDEV(pushsw_major, PUSHSW_BASE_MINOR + i));
		cdev_del(&stored_device_info[i].cdev);
	}
	class_destroy(pushsw_class);
	unregister_chrdev_region(MKDEV(pushsw_major, PUSHSW_BASE_MINOR), PUSHSW_MAX_MINORS);
}

// ---------- Functions as a library----------

int register_devfiles_for_mcp23s08(void)
{
	int retval;
	retval = register_spi_dev();
	if(retval){
		printk(KERN_ERR "%s: register_spi_dev() failed.\n", __func__);
		return retval;
	}
	retval = register_pushsw_dev();
	if(retval){
		printk(KERN_ERR "%s: register_pushsw_dev() failed.\n", __func__);
		return retval;
	}
	return retval;
}

void unregister_devfiles_for_mcp23s08(void)
{
	unregister_spi_dev();
	unregister_pushsw_dev();
}
