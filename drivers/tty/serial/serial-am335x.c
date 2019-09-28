/*
 * serial-am335x.c
 *
 *  Created on: Mar 17, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/console.h>
#include <linux/clk.h>
#include <linux/tty_flip.h>

#define DRIVE_NAME	"am335x-uart"
#define DEVICE_NAME	"ttyS"
#define MAX_PORTS	6


#define UART_THR	0x0
#define UART_RHR	0x0
#define UART_IER	0x4
#define UART_EFR	0x8
#define UART_IIR	0x8
#define UART_LCR	0xc
#define UART_LSR	0x14


/*
 * UART_LSR_UART register
 */
#define UART_LSR_RXFIFOE	BIT(0)
#define UART_LSR_RXOE		BIT(1)
#define UART_LSR_RXPE		BIT(2)
#define UART_LSR_RXFE		BIT(3)
#define UART_LSR_RXBI		BIT(4)
#define UART_LSR_TXFIFOE	BIT(5)
#define UART_LSR_TXSRE		BIT(6)
#define UART_LSR_RXFIFOSTS	BIT(7)

/*
 * UART_IER_UART register
 */
#define UART_IER_RHRIT		BIT(0)
#define UART_IER_THRIT		BIT(1)
#define UART_IER_LINESTSIT	BIT(2)
#define UART_IER_MODEMSTSIT	BIT(3)

/*
 * UART_IIR_UART register
 */
#define UART_IIR_IT_PENDING		BIT(0)
#define UART_IIR_IT_TYPE_SHIFT		1
#define UART_IIR_IT_TYPE_WIDTH		5
#define UART_IIR_IT_TYPE_MODEM		0
#define UART_IIR_IT_TYPE_THR		1
#define UART_IIR_IT_TYPE_RHR		2
#define UART_IIR_IT_TYPE_RXTIMEOUT	6

/*
 * UART_EFR register
 */
#define UART_EFR_AUTOCTSEN	BIT(7)
#define UART_EFR_AUTORTSEN	BIT(6)
#define UART_EFR_SPECIAL	BIT(5)
#define UART_EFR_ENHANCEDEN	BIT(4)

/*
 * UART_LCR register
 */
#define UART_LCR_CONF_MODE_A	0x80
#define UART_LCR_CONF_MODE_B	0xbf


struct am335x_uart {
	struct uart_port port;
	struct platform_device *pdev;
	uint32_t event_mask;
	uint32_t status_mask;
};

static struct am335x_uart am335x_uarts[MAX_PORTS];
static struct uart_driver am335x_uart_driver;

static void am335x_uart_stop_tx(struct uart_port *port);

static void am335x_uart_tx(struct am335x_uart *uart)
{

	struct uart_port *port = &uart->port;
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		writeb_relaxed(port->x_char,port->membase + UART_THR);
		port->x_char = 0;
		port->icount.tx++;

		return ;
	}

	if (uart_tx_stopped(port)) {
		am335x_uart_stop_tx(port);
		return;
	}

	if (uart_circ_empty(xmit)) {
		am335x_uart_stop_tx(port);
		return;
	}

	writeb_relaxed(xmit->buf[xmit->tail], port->membase + UART_THR);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	port->icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS){
		uart_write_wakeup(port);
	}

	if (uart_circ_empty(xmit)){
		am335x_uart_stop_tx(port);
	}



}

static void am335x_uart_rx(struct am335x_uart *uart,uint32_t status,unsigned long *irqflags)
{

	struct uart_port *port = &uart->port;
	uint8_t ch;
	unsigned int flag;


	while(status  & (UART_LSR_RXFIFOE|UART_LSR_RXFIFOSTS)){
		ch = readb_relaxed(port->membase + UART_RHR);
		port->icount.rx++;
		flag = TTY_NORMAL;

		if(unlikely(status & UART_LSR_RXFIFOSTS)){
			if(status & UART_LSR_RXBI){
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if(status & UART_LSR_RXOE){
				port->icount.overrun++;
			} else if(status & UART_LSR_RXFE){
				port->icount.frame++;
			} else if(status & UART_LSR_RXPE){
				port->icount.parity++;
			}

			status &= port->read_status_mask;

			if(status & UART_LSR_RXBI){
				flag = TTY_BREAK;
			} else if(status & UART_LSR_RXFE){
				flag = TTY_FRAME;
			} else if(status & UART_LSR_RXPE){
				flag = TTY_PARITY;
			}
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;

		uart_insert_char(port, status, UART_LSR_RXOE, ch, flag);

		status = readb_relaxed(port->membase + UART_LSR);
	}


	spin_unlock_irqrestore(&port->lock,*irqflags);
	tty_flip_buffer_push(&port->state->port);
	spin_lock_irqsave(&port->lock,*irqflags);


}

static irqreturn_t am335x_uart_irq_handler(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	uint32_t value;
	uint32_t ier;
	uint32_t mask;

	spin_lock(&port->lock);
	value = readb(port->membase + UART_IIR);
	ier = readb(port->membase + UART_IER);

	if(!(value & UART_IIR_IT_PENDING)){
		uint32_t type;
		type = (value >> UART_IIR_IT_TYPE_SHIFT)&
				((1<<UART_IIR_IT_TYPE_WIDTH)-1);

		mask = readl(port->membase + UART_IER);

		if(type == UART_IIR_IT_TYPE_THR){
			mask &= ~UART_IER_THRIT;
		} else if(type == UART_IIR_IT_TYPE_RHR || type == UART_IIR_IT_TYPE_RXTIMEOUT){
			mask &= ~UART_IER_RHRIT;
		}

		writel(mask,port->membase + UART_IER);
	}
	spin_unlock(&port->lock);

	if(!(value & UART_IIR_IT_PENDING)){
		return IRQ_WAKE_THREAD;
	}
	return IRQ_NONE;
}

static irqreturn_t am335x_uart_irq_thread(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct am335x_uart *uart = port->private_data;
	uint32_t value;
	unsigned long flags;


	spin_lock_irqsave(&port->lock,flags);
	value = readl(port->membase + UART_LSR);
	value &= uart->status_mask;


	while(value&(UART_LSR_RXFIFOE|UART_LSR_TXFIFOE)){
		if(value & UART_LSR_RXFIFOE){
			am335x_uart_rx(uart,value,&flags);
		}

		if(value & UART_LSR_TXFIFOE){
			am335x_uart_tx(uart);
		}

		value = readl_relaxed(port->membase + UART_LSR);
		value &= uart->status_mask;
	}
	writel(uart->event_mask,port->membase + UART_IER);

	spin_unlock_irqrestore(&port->lock,flags);

	return IRQ_HANDLED;
}

static int am335x_uart_startup(struct uart_port *port)
{
	int ret;


	ret = request_threaded_irq(port->irq, am335x_uart_irq_handler,
				   am335x_uart_irq_thread,
				   0, "am335x-uart", port);

	if(ret < 0){
		return ret;
	}

	writeb(3,port->membase + UART_IER);

	return 0;
}
static void am335x_uart_shutdown(struct uart_port *port)
{

	writeb(0,port->membase + UART_IER);
	free_irq(port->irq,port);
}
static void am335x_uart_set_termios(struct uart_port *port, struct ktermios *new,
				       struct ktermios *old)
{
}
static void am335x_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}
static void am335x_uart_start_tx(struct uart_port *port)
{
	struct am335x_uart *uart = port->private_data;
	uint32_t value;

	uart->status_mask |= UART_LSR_TXFIFOE;
	uart->event_mask |= UART_IER_THRIT;



	value = readb_relaxed(port->membase + UART_IER);
	value |= UART_IER_THRIT;
	writeb_relaxed(value,port->membase + UART_IER);

	value = readb_relaxed(port->membase + UART_IER);

}
static void am335x_uart_stop_tx(struct uart_port *port)
{
	struct am335x_uart *uart = port->private_data;
	uint32_t value;

	uart->status_mask &= ~UART_LSR_TXFIFOE;
	uart->event_mask &= ~UART_IER_THRIT;

	value = readb_relaxed(port->membase + UART_IER);
	value &= ~UART_IER_THRIT;
	writeb_relaxed(value,port->membase + UART_IER);

	value = readb_relaxed(port->membase + UART_IER);

}
static void am335x_uart_stop_rx(struct uart_port *port)
{
	struct am335x_uart *uart = port->private_data;
	uint32_t value;

	uart->status_mask &= ~UART_LSR_RXFIFOE;
	uart->event_mask &= ~UART_IER_RHRIT;

	value = readl_relaxed(port->membase + UART_IER);
	value &= ~UART_IER_RHRIT;
	writel_relaxed(value,port->membase + UART_IER);

	value = readb_relaxed(port->membase + UART_IER);

}
static const char*am335x_uart_type(struct uart_port *port)
{
	return "am335x-uart";
}



static const struct uart_ops am335x_uart_ops = {
	.startup 	= am335x_uart_startup,
	.shutdown	= am335x_uart_shutdown,
	.start_tx	= am335x_uart_start_tx,
	.stop_tx	= am335x_uart_stop_tx,
	.stop_rx	= am335x_uart_stop_rx,
	.set_termios	= am335x_uart_set_termios,
	.set_mctrl 	= am335x_uart_set_mctrl,
	.type 		= am335x_uart_type,
};


#ifdef CONFIG_SERIAL_AM335X_CONSOLE
static void am335x_console_putchar(struct uart_port *port, int ch)
{
	while (!(readl(port->membase + UART_LSR) & UART_LSR_TXFIFOE))
		cpu_relax();

	writeb(ch,port->membase + UART_THR);

}
static void am335x_console_write(struct console *co, const char *s, unsigned count)
{
	struct uart_port *port = &am335x_uarts[co->index].port;
	struct am335x_uart *uart = port->private_data;
	uint32_t value;
	uint32_t restore;
	unsigned long flags;

	spin_lock_irqsave(&port->lock,flags);

	restore = readl(port->membase + UART_IER);
	value = restore & ~uart->event_mask;
	writel(value,port->membase + UART_IER);

	uart_console_write(port, s, count, am335x_console_putchar);

	while (!(readl(port->membase + UART_LSR)& UART_LSR_TXFIFOE));

	writel(restore,port->membase + UART_IER);

	spin_unlock_irqrestore(&port->lock,flags);
}

static int am335x_console_setup(struct console *co, char *options)
{
	return 0;
}

static struct console am335x_console = {
	.name		= DEVICE_NAME,
	.device		= uart_console_device,
	.write		= am335x_console_write,
	.setup		= am335x_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &am335x_uart_driver,
};

#define AM335X_SERIAL_CONSOLE (&am335x_console)
#else
#define AM335X_SERIAL_CONSOLE NULL
#endif

static void am335x_uart_hw_init(struct am335x_uart *uart)
{
	uint8_t lcr;

	lcr = readb_relaxed(uart->port.membase + UART_LCR);
	writeb_relaxed(UART_LCR_CONF_MODE_B,uart->port.membase + UART_LCR);
	writeb_relaxed(UART_EFR_ENHANCEDEN,uart->port.membase + UART_EFR);
	writeb_relaxed(0,uart->port.membase + UART_IER);
	writeb_relaxed(lcr,uart->port.membase + UART_LCR);
}

static int am335x_uart_device_init(struct am335x_uart *uart,int id)
{
	struct uart_port *port = &uart->port;
	struct platform_device *pdev = uart->pdev;
	struct resource *res;
	struct clk *clk;


	port->irq = platform_get_irq(pdev, 0);
	if(port->irq < 0){
		dev_err(&pdev->dev,"unable to get irq info");
		return port->irq;
	}


	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->membase = devm_ioremap_resource(&pdev->dev, res);
	if(IS_ERR(port->membase)){
		dev_err(&pdev->dev,"unable to get mem map ");
		return PTR_ERR(port->membase);
	}
	port->mapbase = res->start;

	clk = devm_clk_get(&pdev->dev,"fclk");
	if(IS_ERR(clk)){
		dev_err(&pdev->dev,"unable to get clk src");
		return PTR_ERR(clk);
	}
	clk_prepare_enable(clk);

	port->type 	= PORT_8250;
	port->iotype	= UPIO_MEM;
	port->dev	= &pdev->dev;
	port->line 	= id;
	port->ops	= &am335x_uart_ops;


	spin_lock_init(&port->lock);

	uart->event_mask = UART_IER_RHRIT|UART_IER_THRIT;
	uart->status_mask = UART_LSR_RXFIFOE|UART_LSR_TXFIFOE;

	port->private_data = uart;
	am335x_uart_hw_init(uart);

	return 0;
}


static int am335x_uart_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct am335x_uart *uart;
	int id;
	int ret;

	id = of_alias_get_id(np, "serial");
	if(id < 0){
		return id;
	}
	if(id >= MAX_PORTS){
		return -EINVAL;
	}

	uart = &am335x_uarts[id];
	uart->pdev = pdev;

	ret = am335x_uart_device_init(uart,id);
	if(ret){
		return ret;
	}


	ret = uart_add_one_port(&am335x_uart_driver, &uart->port);
	if(ret){
		return ret;
	}

	platform_set_drvdata(pdev,&uart->port);
	return 0;
}

static int am335x_uart_remove(struct platform_device *pdev)
{
	return 0;
}

static struct uart_driver am335x_uart_driver = {
	.driver_name 	= DRIVE_NAME,
	.dev_name 	= DEVICE_NAME,
	.major 		= 0,
	.minor 		= 0,
	.nr 		= MAX_PORTS,
	.cons 		= AM335X_SERIAL_CONSOLE,
};

static const struct of_device_id am335x_uart_of_match[] = {
	{ .compatible = "ti,am335x-uart"},
	{},
};

static struct platform_driver am335x_uart_platform_driver = {
	.probe 		= am335x_uart_probe,
	.remove 	= am335x_uart_remove,
	.driver 	= {
		.owner		= THIS_MODULE,
		.name 		= DRIVE_NAME,
		.of_match_table = of_match_ptr(am335x_uart_of_match),
	},
};

static int __init am335x_uart_init(void)
{
	static char banner[] __initdata = "AM335X UART driver initialized";
	int ret;

	pr_info("%s\n", banner);

	ret = uart_register_driver(&am335x_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&am335x_uart_platform_driver);
	if (ret)
		uart_unregister_driver(&am335x_uart_driver);

	return ret;
}

static void __exit am335x_uart_exit(void)
{
	platform_driver_unregister(&am335x_uart_platform_driver);
	uart_unregister_driver(&am335x_uart_driver);
}

module_init(am335x_uart_init);
module_exit(am335x_uart_exit);

MODULE_DEVICE_TABLE(of, am335x_uart_of_match);
MODULE_ALIAS("platform:" DRIVE_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("TI AM335X uart driver");
MODULE_LICENSE("GPL v2");



