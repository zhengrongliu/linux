/*
 * i2c-am335x.c
 *
 *  Created on: Apr 2, 2018
 *      Author: Zhengrong.liu
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>

#define DRIVE_NAME "am335x-i2c"

#define I2C_REVNB_LO		0x0
#define I2C_REVNB_HI		0x4
#define I2C_SYSC		010
#define I2C_IRQSTATUS_RAW	0x24
#define I2C_IRQSTATUS		0x28
#define I2C_IRQENABLE_SET	0x2c
#define I2C_IRQENABLE_CLR	0x30
#define I2C_WE			0x34
#define I2C_DMARXENABLE_SET	0x38
#define I2C_DMATXENABLE_SET	0x3c
#define I2C_DMARXENABLE_CLR	0x40
#define I2C_DMATXENABLE_CLR	0x44
#define I2C_DMARXWAKE_EN	0x48
#define I2C_DMATXWAKE_EN	0x4c
#define I2C_SYSS		0x90
#define I2C_BUF			0x94
#define I2C_CNT			0x98
#define I2C_DATA		0x9c
#define I2C_CON			0xa4
#define I2C_OA			0xa8
#define I2C_SA			0xac
#define I2C_PSC			0xb0
#define I2C_SCLL		0xb4
#define I2C_SCLH		0xb8
#define I2C_SYSTEST		0xbc
#define I2C_BUFSTAT		0xc0
#define I2C_OA1			0xc4
#define I2C_OA2			0xc8
#define I2C_OA3			0xcc
#define I2C_ACTOA		0xd0
#define I2C_SBLOCK		0xd4

/*
 * irq event
 */
#define I2C_EVENT_AL		BIT(0)
#define I2C_EVENT_NACK		BIT(1)
#define I2C_EVENT_ARDY		BIT(2)
#define I2C_EVENT_RRDY		BIT(3)
#define I2C_EVENT_XRDY		BIT(4)
#define I2C_EVENT_GC		BIT(5)
#define I2C_EVENT_STC		BIT(6)
#define I2C_EVENT_AERR		BIT(7)
#define I2C_EVENT_BF		BIT(8)
#define I2C_EVENT_AAS		BIT(9)
#define I2C_EVENT_XUDF		BIT(10)
#define I2C_EVENT_ROVR		BIT(11)
#define I2C_EVENT_BB		BIT(12)
#define I2C_EVENT_RDR		BIT(13)
#define I2C_EVENT_XDR		BIT(14)

/*
 * I2C_SYSC register
 */
#define I2C_SYSC_AUTOIDLE_ENABLE	(1<<0)
#define I2C_SYSC_AUTOIDLE_DISABLE	(0<<0)
#define I2C_SYSC_SRST_ON		(1<<1)
#define I2C_SYSC_SRST_OFF		(0<<1)
#define I2C_SYSC_ENWAKEUP_ENABLE	(1<<2)
#define I2C_SYSC_ENWAKEUP_DISABLE	(0<<2)
#define I2C_SYSC_IDLEMODE_IDLE		(0<<3)
#define I2C_SYSC_IDLEMODE_NONE		(1<<3)
#define I2C_SYSC_IDLEMODE_SMART		(2<<3)
#define I2C_SYSC_IDLEMODE_WAKEUP	(3<<3)
#define I2C_SYSC_CLKACTIVITY_BOTH_OFF	(0<<8)
#define I2C_SYSC_CLKACTIVITY_SYS_OFF	(1<<8)
#define I2C_SYSC_CLKACTIVITY_OCP_OFF	(2<<8)
#define I2C_SYSC_CLKACTIVITY_BOTH_ON	(3<<8)

/*
 * I2C_SYSS register
 */
#define I2C_SYSS_RDONE			BIT(0)

/*
 * I2C_CON register
 */
#define I2C_CON_STT			BIT(0)
#define I2C_CON_STP			BIT(1)
#define I2C_CON_XOA3			BIT(4)
#define I2C_CON_XOA2			BIT(5)
#define I2C_CON_XOA1			BIT(6)
#define I2C_CON_XOA0			BIT(7)
#define I2C_CON_SA_7BIT			(0<<8)
#define I2C_CON_SA_10BIT		(1<<8)
#define I2C_CON_TRASMIT			(1<<9)
#define I2C_CON_RECEIVE			(0<<9)
#define I2C_CON_MASTER			(1<<10)
#define I2C_CON_SLAVE			(0<<10)
#define I2C_CON_STB			BIT(11)
#define I2C_CON_OPMODE_FS		(0<<12)
#define I2C_CON_EN			BIT(15)


struct am335x_i2c_bus {
	struct i2c_adapter adap;
	struct completion complete;
	void __iomem *base;
	uint32_t clock_freq;
	uint32_t event_mask;
	struct i2c_msg *msgs;
	int msgs_count;
	int msgs_index;
	int mbuf_index;
};

static inline void am335x_i2c_wait_bus_idle(struct am335x_i2c_bus *bus)
{
	while(readl(bus->base + I2C_IRQSTATUS_RAW) & I2C_EVENT_BB);
}

static void am335x_i2c_xfer_msg(struct am335x_i2c_bus *bus,struct i2c_msg *msg)
{
	uint8_t slave_addr = msg->addr << 1;
	uint32_t value = I2C_CON_EN|I2C_CON_MASTER|I2C_CON_STT|I2C_CON_STP;


	bus->mbuf_index = 0;

	if (msg->flags & I2C_M_RD) {
		value |= I2C_CON_RECEIVE;
		slave_addr |= 1;
	} else {
		value |= I2C_CON_TRASMIT;
	}


	am335x_i2c_wait_bus_idle(bus);

	writel(slave_addr,bus->base + I2C_SA);
	writel(msg->len,bus->base + I2C_CNT);


	writel(value,bus->base + I2C_CON);
}

static void am335x_i2c_start_xfer(struct am335x_i2c_bus *bus)
{

	am335x_i2c_xfer_msg(bus,&bus->msgs[0]);
}

static void am335x_i2c_transmit_data(struct am335x_i2c_bus *bus)
{

}

static void am335x_i2c_receive_data(struct am335x_i2c_bus*bus)
{

}


static int am335x_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num)
{
	struct am335x_i2c_bus *bus = i2c_get_adapdata(adap);
	unsigned long time_left;

	reinit_completion(&bus->complete);

	bus->msgs = msgs;
	bus->msgs_index = 0;
	bus->msgs_count = num;


	am335x_i2c_start_xfer(bus);

	time_left = wait_for_completion_timeout(&bus->complete,
						bus->adap.timeout);

	if(time_left == 0){
		return -ETIMEDOUT;
	}

	return 0;
}

static u32 am335x_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm am335x_i2c_algo = {
	.master_xfer	= am335x_i2c_master_xfer,
	.functionality	= am335x_i2c_functionality,
};

static int am335x_i2c_init(struct am335x_i2c_bus *bus)
{
	return 0;
}

static int am335x_i2c_probe(struct platform_device *pdev)
{
	struct am335x_i2c_bus *bus;
	int ret;
	ret = am335x_i2c_init(bus);
	if(ret){
		return ret;
	}

	ret = i2c_add_adapter(adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add i2c adapter to core");
		return ret;
	}

	platform_set_drvdata(pdev, bus);

	return 0;
}

static int am335x_i2c_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id am335x_i2c_of_match[] = {
	{ .compatible = "ti,am335x-i2c", },
	{}
};

static struct platform_driver am335x_i2c_driver = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= DRIVE_NAME,
		.of_match_table = am335x_i2c_of_match,
	},
	.probe = am335x_i2c_probe,
	.remove = am335x_i2c_remove,
};
module_platform_driver(am335x_i2c_driver);


MODULE_DEVICE_TABLE(of, am335x_i2c_of_match);
MODULE_ALIAS("platform:" DRIVE_NAME);
MODULE_AUTHOR("zhengrong.liu");
MODULE_DESCRIPTION("TI AM335X I2C driver");
MODULE_LICENSE("GPL v2");


