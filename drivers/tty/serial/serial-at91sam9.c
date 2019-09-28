/*
 * serial-at91sam9.c
 *
 *  Created on: Jul 9, 2017
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
#include <linux/tty_flip.h>

#define DRIVER_NAME	"at91sam9-uart"
#define DEVICE_NAME	"ttyS"
#define MAX_PORTS	1

/*
 * Uart registers offset
 */
#define UART_CR          0x00
#define UART_MR          0x04
#define UART_IER         0x08
#define UART_IDR         0x0c
#define UART_IMR         0x10
#define UART_SR          0x14
#define UART_RHR         0x18
#define UART_THR         0x1c
#define UART_BRGR        0x20


/*
 * Uart Control register
 */
#define UART_CR_RSTRX    (1<<2)
#define UART_CR_RSTTX    (1<<3)
#define UART_CR_RXEN     (1<<4)
#define UART_CR_RXDIS    (1<<5)
#define UART_CR_TXEN     (1<<6)
#define UART_CR_TXDIS    (1<<7)
#define UART_CR_RSTSTA   (1<<8)

/*
 * Uart Mode register
 */
#define UART_MR_PAR_SHIFT         9
#define UART_MR_PAR_MASK          0x7
#define UART_MR_PAR_EVEN          0x0
#define UART_MR_PAR_ODD           0x1
#define UART_MR_PAR_SPACE         0x2
#define UART_MR_PAR_MARK          0x3
#define UART_MR_PAR_NONE          0x4
#define UART_MR_CHMODE_SHIFT      14
#define UART_MR_CHMODE_MASK       0x3
#define UART_MR_CHMODE_NORM       0x0
#define UART_MR_CHMODE_AUTO       0x1
#define UART_MR_CHMODE_LOCLOOP    0x2
#define UART_MR_CHMODE_REMLOOP    0x3

/*
 * Uart EVENT bit
 */
#define UART_EVENT_RXRDY_SHIFT   0
#define UART_EVENT_RXRDY         (1<<0)
#define UART_EVENT_TXRDY_SHIFT   1
#define UART_EVENT_TXRDY         (1<<1)
#define UART_EVENT_OVRE          (1<<5)
#define UART_EVENT_FRAME         (1<<6)
#define UART_EVENT_PARE          (1<<7)
#define UART_EVENT_RXERR           (UART_EVENT_OVRE|UART_EVENT_FRAME|UART_EVENT_PARE)
#define UART_EVENT_TXEMPTY       (1<<9)
#define UART_EVENT_COMMTX        (1<<30)
#define UART_EVENT_COMMRX        (1<<31)


/*
 * Uart Baud Rate Generator register
 */
#define UART_BRGR_CD_SHIFT     0
#define UART_BRGR_CD_MASK      0xffff


struct at91sam9_uart {
	struct uart_port port;
	struct platform_device *pdev;
	uint32_t event_mask;
};
#define to_at91sam9_uart(up) container_of(up,struct at91sam9_uart,port)


static void at91sam9_uart_stop_tx(struct uart_port *port);

static const struct uart_ops at91sam9_uart_ops;
static struct at91sam9_uart at91sam9_uarts[MAX_PORTS];
static struct uart_driver at91sam9_uart_driver;

static void at91sam9_uart_tx_handler(struct at91sam9_uart *uart)
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
		at91sam9_uart_stop_tx(port);
		return;
	}

	if (uart_circ_empty(xmit)) {
		at91sam9_uart_stop_tx(port);
		return;
	}

	writel_relaxed(xmit->buf[xmit->tail], port->membase + UART_THR);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	port->icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS){
		uart_write_wakeup(port);
	}

	if (uart_circ_empty(xmit)){
		at91sam9_uart_stop_tx(port);
	}


}

static void at91sam9_uart_rx_handler(struct at91sam9_uart *uart)
{
	struct uart_port *port = &uart->port;
	uint32_t status;
	uint8_t ch;
	unsigned int flag;

	ch = readb_relaxed(port->membase + UART_RHR);
	status = readl_relaxed(port->membase + UART_SR);
	flag = TTY_NORMAL;

	port->icount.rx++;
	if(status & UART_EVENT_RXERR){

		writel_relaxed(UART_CR_RSTSTA,port->membase + UART_CR);

		if(status & UART_EVENT_OVRE){
			port->icount.overrun++;
		}
		if(status & UART_EVENT_FRAME){
			port->icount.frame++;
		}
		if(status & UART_EVENT_PARE){
			port->icount.parity++;
		}

		status &= port->read_status_mask;

		if(status & UART_EVENT_FRAME){
			flag = TTY_FRAME;
		}
		if(status & UART_EVENT_PARE){
			flag = TTY_PARITY;
		}
	}

	uart_handle_sysrq_char(port, ch);

	uart_insert_char(port, status, UART_EVENT_OVRE, ch, flag);

	spin_unlock(&port->lock);
	tty_flip_buffer_push(&port->state->port);
	spin_lock(&port->lock);
}


static irqreturn_t at91sam9_uart_irq_handler(int irq, void *dev_id)
{
	struct at91sam9_uart *uart = dev_id;
	struct uart_port *port = &uart->port;
	uint32_t status;

	status = readl_relaxed(port->membase + UART_SR);
	status &= uart->event_mask;

	if(status){
		writel_relaxed(status,port->membase + UART_IDR);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}
static irqreturn_t at91sam9_uart_irq_thread_handler(int irq, void *dev_id)
{
	struct at91sam9_uart *uart = dev_id;
	struct uart_port *port = &uart->port;
	uint32_t status;

	status = readl_relaxed(port->membase + UART_SR) & uart->event_mask;

	while(status){
		if(status & UART_EVENT_TXRDY){
			at91sam9_uart_tx_handler(uart);

		}

		if(status & UART_EVENT_RXRDY){
			at91sam9_uart_rx_handler(uart);
		}

		status = readl_relaxed(port->membase + UART_SR) & uart->event_mask;

	}

	writel_relaxed(uart->event_mask,port->membase + UART_IER);

	return IRQ_HANDLED;
}

static int at91sam9_uart_startup(struct uart_port *port)
{
	struct at91sam9_uart *uart = to_at91sam9_uart(port);
	int ret;

	ret = request_threaded_irq(port->irq, at91sam9_uart_irq_handler,
				   at91sam9_uart_irq_thread_handler,
				   IRQF_SHARED, "at91sam9-uart", uart);

	if (ret)
		return ret;

	uart->event_mask = UART_EVENT_TXRDY|UART_EVENT_RXRDY;

	writel(UART_CR_RSTRX|UART_CR_RSTTX|UART_CR_RSTSTA,port->membase + UART_CR);
	writel(UART_CR_TXEN|UART_CR_RXEN,port->membase + UART_CR);
	writel(uart->event_mask,port->membase + UART_IER);

	return 0;
}
static void at91sam9_uart_shutdown(struct uart_port *port)
{
	struct at91sam9_uart *uart = to_at91sam9_uart(port);

	writel(UART_CR_RSTRX|UART_CR_RSTTX|UART_CR_RSTSTA,port->membase + UART_CR);
	free_irq(port->irq, uart);
}

static void at91sam9_uart_set_termios(struct uart_port *port, struct ktermios *new,
				       struct ktermios *old)
{

}
static void at91sam9_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	return;
}
static void at91sam9_uart_start_tx(struct uart_port *port)
{
	struct at91sam9_uart *uart = to_at91sam9_uart(port);
	struct circ_buf *xmit = &port->state->xmit;

	if (uart_circ_empty(xmit))
		return;

	writel(UART_EVENT_TXRDY,port->membase + UART_IER);
	uart->event_mask |= UART_EVENT_TXRDY;

	at91sam9_uart_tx_handler(uart);
}
static void at91sam9_uart_stop_tx(struct uart_port *port)
{
	struct at91sam9_uart *uart = to_at91sam9_uart(port);

	writel(UART_EVENT_TXRDY,port->membase + UART_IDR);
	uart->event_mask &= ~UART_EVENT_TXRDY;
}
static void at91sam9_uart_stop_rx(struct uart_port *port)
{
	struct at91sam9_uart *uart = to_at91sam9_uart(port);

	writel(UART_EVENT_RXRDY,port->membase + UART_IDR);
	uart->event_mask &= ~UART_EVENT_RXRDY;

}
static const char*at91sam9_uart_type(struct uart_port *port)
{
	return "at91sam9-uart";
}


static int at91sam9_uart_device_init(struct at91sam9_uart *uart,int id)
{
	struct uart_port *port = &uart->port;
	struct platform_device *pdev = uart->pdev;
	struct resource *res;

	port->type 	= PORT_ATMEL;
	port->iotype	= UPIO_MEM;
	port->dev	= &pdev->dev;
	port->line 	= id;
	port->ops	= &at91sam9_uart_ops;
	port->irq	= platform_get_irq(pdev, 0);
	if(port->irq < 0){
		return port->irq;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);
	port->mapbase = res->start;

	spin_lock_init(&port->lock);
	return 0;
}

static void at91sam9_uart_hw_init(struct at91sam9_uart *uart)
{
	struct uart_port *port = &uart->port;

	writel(0xffffffff,port->membase + UART_IDR);
}

#ifdef CONFIG_SERIAL_AT91SAM9_CONSOLE
static void at91sam9_console_putchar(struct uart_port *port, int ch)
{
	while (!(readl(port->membase + UART_SR) & UART_EVENT_TXRDY))
		cpu_relax();

	writeb(ch,port->membase + UART_THR);
}
static void at91sam9_console_write(struct console *co, const char *s, unsigned count)
{
	struct uart_port *port = &at91sam9_uarts[co->index].port;
	struct at91sam9_uart *uart = to_at91sam9_uart(port);
	uint32_t status;

	spin_lock(&port->lock);
	/*
	 * First, save IMR and then disable interrupts
	 */
	writel(uart->event_mask,port->membase + UART_IDR);

	writel(UART_CR_TXEN,port->membase + UART_CR);

	uart_console_write(port, s, count, at91sam9_console_putchar);

	/*
	 * Finally, wait for transmitter to become empty
	 * and restore IMR
	 */
	do {
		status = readl(port->membase + UART_SR);
	} while (!(status & UART_EVENT_TXRDY));


	writel(uart->event_mask,port->membase + UART_IER);

	spin_unlock(&port->lock);

}

static int at91sam9_console_setup(struct console *co, char *options)
{
	return 0;
}

static struct console at91sam9_console = {
	.name		= DEVICE_NAME,
	.device		= uart_console_device,
	.write		= at91sam9_console_write,
	.setup		= at91sam9_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &at91sam9_uart_driver,
};

#define AT91SAM9_SERIAL_CONSOLE (&at91sam9_console)

#else
#define AT91SAM9_SERIAL_CONSOLE NULL
#endif /* CONFIG_SERIAL_AT91SAM9_CONSOLE */

static int at91sam9_serial_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct at91sam9_uart *uart;
	int id;
	int ret;

	id = of_alias_get_id(np, "serial");
	if(id < 0){
		return id;
	}
	if(id >= MAX_PORTS){
		return -EINVAL;
	}

	uart = &at91sam9_uarts[id];
	uart->pdev = pdev;

	ret = at91sam9_uart_device_init(uart,id);
	if(ret){
		return ret;
	}

	at91sam9_uart_hw_init(uart);

	ret = uart_add_one_port(&at91sam9_uart_driver, &uart->port);
	if(ret){
		return ret;
	}

	platform_set_drvdata(pdev,&uart->port);

	return 0;
}

static int at91sam9_serial_remove(struct platform_device *pdev)
{
	return 0;
}
static const struct uart_ops at91sam9_uart_ops = {
	.startup 	= at91sam9_uart_startup,
	.shutdown	= at91sam9_uart_shutdown,
	.start_tx	= at91sam9_uart_start_tx,
	.stop_tx	= at91sam9_uart_stop_tx,
	.stop_rx	= at91sam9_uart_stop_rx,
	.set_termios	= at91sam9_uart_set_termios,
	.set_mctrl 	= at91sam9_uart_set_mctrl,
	.type 		= at91sam9_uart_type,
};

static const struct of_device_id at91sam9_match[] = {
	{ .compatible = "atmel,at91sam9-uart"},
	{},
};


static struct uart_driver at91sam9_uart_driver = {
	.driver_name 	= DRIVER_NAME,
	.dev_name 	= DEVICE_NAME,
	.major 		= 0,
	.minor 		= 0,
	.nr 		= MAX_PORTS,
	.cons 		= AT91SAM9_SERIAL_CONSOLE,
};

static struct platform_driver at91sam9_serial_driver = {
	.probe 		= at91sam9_serial_probe,
	.remove 	= at91sam9_serial_remove,
	.driver 	= {
		.name 		= DRIVER_NAME,
		.of_match_table = of_match_ptr(at91sam9_match),
	},
};

static int __init at91sam9_uart_init(void)
{
	static char banner[] __initdata = "AT91SAM9 UART driver initialized";
	int ret;

	pr_info("%s\n", banner);

	ret = uart_register_driver(&at91sam9_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&at91sam9_serial_driver);
	if (ret)
		uart_unregister_driver(&at91sam9_uart_driver);

	return ret;
}

static void __exit at91sam9_uart_exit(void)
{
	platform_driver_unregister(&at91sam9_serial_driver);
	uart_unregister_driver(&at91sam9_uart_driver);
}

module_init(at91sam9_uart_init);
module_exit(at91sam9_uart_exit);

MODULE_DEVICE_TABLE(of, at91sam9_match);
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("ATMEL AT91SAM9 serial port driver");
MODULE_LICENSE("GPL v2");
