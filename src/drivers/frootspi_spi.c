#include <linux/cdev.h>  // cdev_*()
#include <linux/fs.h>  // struct file, open, release
#include <linux/module.h>
#include <linux/spi/spi.h>  // spi_*()
#include <linux/uaccess.h>  // copy_to_user()


// デバイスを識別するテーブル { "name", "好きなデータ"}を追加する
// 同じ？SPIデバイス（IC)が複数あればここに追加する
#define SPI_DRIVER_NAME "frootspi_spi"
#define SPI_BUS_NUM 0
#define SPI_CHIP_SELECT 0
#define MCP23S08_PACKET_SIZE 3

static struct spi_device_id mcp23s08_id_table[] = {
    {"mcp23s08", 0},
	{},
};
MODULE_DEVICE_TABLE(spi, mcp23s08_id_table);

static struct spi_board_info mcp23s08_info = {
	.modalias = "mcp23s08",
    .max_speed_hz = 100000,
    .bus_num = 0,
    .chip_select = 0,
    .mode = SPI_MODE_3,
};

struct mcp23s08_drvdata {
	struct spi_device *spi;
	struct mutex lock;
	unsigned char tx[MCP23S08_PACKET_SIZE] ____cacheline_aligned;
	unsigned char rx[MCP23S08_PACKET_SIZE] ____cacheline_aligned;
	struct spi_transfer xfer ____cacheline_aligned;
	struct spi_message msg ____cacheline_aligned;
};


static int mcp23s08_probe(struct spi_device *spi)
{
	printk("mcp23s08_probe\n");
	
	struct mcp23s08_drvdata *data;

	spi->max_speed_hz = mcp23s08_info.max_speed_hz;
	spi->mode = mcp23s08_info.mode;
	spi->bits_per_word = 8;

	if (spi_setup(spi)){
		printk(KERN_ERR "%s: spi_setup() failed\n", __func__);
		return -ENODEV;
	}

	data = kzalloc(sizeof(struct mcp23s08_drvdata), GFP_KERNEL);
	if (data == NULL) {
		printk(KERN_ERR "%s: kszalloc() failed\n", __func__);
		return -ENODEV;
	}

	data->spi = spi;

	mutex_init(&data->lock);

	data->xfer.tx_buf = data->tx;
	data->xfer.rx_buf = data->rx;
	data->xfer.bits_per_word = 8;
	data->xfer.len = MCP23S08_PACKET_SIZE;
	data->xfer.cs_change = 0;
	data->xfer.delay_usecs = 0;
	data->xfer.speed_hz = 100000;  // 100kHz

	spi_message_init_with_transfers(&data->msg, &data->xfer, 1);

	spi_set_drvdata(spi, data);

	printk(KERN_INFO "%s: mcp23s08 probed", SPI_DRIVER_NAME);

	return 0;
}

static int mcp23s08_remove(struct spi_device *spi)
{
	printk("mcp23s08_remove\n");

	struct mcp23s08_drvdata *data;
	data = (struct mcp23s08_drvdata *)spi_get_drvdata(spi);
	kfree(data);

	printk(KERN_INFO "%s: mcp23s08 removed", SPI_DRIVER_NAME);

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

	snprintf(str, sizeof(str), "%s.%u", dev_name(&master->dev), cs);

	dev = bus_find_device_by_name(&spi_bus_type, NULL, str);

	if (dev){
		device_del(dev);
	}
}

int register_spi_dev(void)
{
	struct spi_master *master;
	struct spi_device *spi_device;

	spi_register_driver(&mcp23s08_driver);

	mcp23s08_info.bus_num = SPI_BUS_NUM;
	mcp23s08_info.chip_select = SPI_CHIP_SELECT;

	master = spi_busnum_to_master(mcp23s08_info.bus_num);

	if (!master) {
		printk(KERN_ERR "%s: spi_busnum_to_master returned NULL\n", __func__);
		spi_unregister_driver(&mcp23s08_driver);
		return -ENODEV;
	}

	// なんでremoveするの・・・？
	spi_remove_device(master, mcp23s08_info.chip_select);

	spi_device = spi_new_device(master, &mcp23s08_info);
	if (!spi_device) {
		printk(KERN_ERR "%s: spi_new_device returned NULL\n", __func__);
		spi_unregister_driver(&mcp23s08_driver);
		return -ENODEV;
	}

	return 0;
}

void unregister_spi_dev(void)
{
	struct spi_master *master;

	master = spi_busnum_to_master(mcp23s08_info.bus_num);

	if(master) {
		spi_remove_device(master, mcp23s08_info.chip_select);
	}else{
		printk(KERN_ERR "mcp23s08 remove error\n");
	}

	spi_unregister_driver(&mcp23s08_driver);
}