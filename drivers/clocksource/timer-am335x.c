/*
 * timer-am335x.c
 *
 *  Created on: Feb 6, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>

#define TIMER_TIDR		0x0
#define TIMER_TIOCP_CFG		0x10
#define TIMER_IRQ_EOI		0x20
#define TIMER_IRQSTATUS_RAW	0x24
#define TIMER_IRQSTATUS		0x28
#define TIMER_IRQENABLE_SET	0x2c
#define TIMER_IRQENABLE_CLR	0x30
#define TIMER_IRQWAKEEN		0x34
#define TIMER_TCLR		0x38
#define TIMER_TCRR		0x3c
#define TIMER_TLDR		0x40
#define TIMER_TTGR		0x44
#define TIMER_TWPS		0x48
#define TIMER_TMAR		0x4c
#define TIMER_TCAR1		0x50
#define TIMER_TSICR		0x54
#define TIMER_TCAR2		0x58

/*
 * TIMER_TIDR register
 */
#define TIMER_TIDR_Y_MINOR_OFFSET	0
#define TIMER_TIDR_Y_MINOR_WIDTH	6

/*
 * TIMER_TIOCP_CFG register
 */
#define TIMER_TIOCP_CFG_SOFTRESET	BIT(0)
#define TIMER_TIOCP_CFG_EMUFREE		BIT(1)
#define TIMER_TIOCP_CFG_IDLEMODE_OFFSET 2

/*
 * TIMER_IRQ_EOI register
 */
#define TIMER_IRQ_EOI_DMAEVENT_ACK	BIT(0)

/*
 * TIMER_IRQSTATUS register
 */
#define TIMER_IRQSTATUS_MAT		BIT(0)
#define TIMER_IRQSTATUS_OVF		BIT(1)
#define TIMER_IRQSTATUS_TCAR		BIT(2)

/*
 * TIMER_TCLR register
 */
#define TIMER_TCLR_START		BIT(0)
#define TIMER_TCLR_AUTORELOAD		BIT(1)
#define TIMER_TCLR_ONESHOT		0
#define TIMER_TCLR_PTV			BIT(2)
#define TIMER_TCLR_PRE			BIT(3)
#define TIMER_TCLR_CE			BIT(4)
#define TIMER_TCLR_SCPWM		BIT(5)
#define TIMER_TCLR_TCM_OFFSET		8
#define TIMER_TCLR_TCM_NOCAP		(0<<TIMER_TCLR_TCM_OFFSET)
#define TIMER_TCLR_TCM_RISING_EDGE	(1<<TIMER_TCLR_TCM_OFFSET)
#define TIMER_TCLR_TCM_FALLING_EDGE	(2<<TIMER_TCLR_TCM_OFFSET)
#define TIMER_TCLR_TCM_BOTH_EDGE	(3<<TIMER_TCLR_TCM_OFFSET)
#define TIMER_TCLR_TRG_OFFSET		10
#define TIMER_TCLR_TRG_NOTRG		(0<<TIMER_TCLR_TRG_OFFSET)
#define TIMER_TCLR_TRG_OVF		(1<<TIMER_TCLR_TRG_OFFSET)
#define TIMER_TCLR_TRG_OVF_MAT		(2<<TIMER_TCLR_TRG_OFFSET)
#define TIMER_TCLR_PT			BIT(12)
#define TIMER_TCLR_CAPT_MODE		BIT(13)
#define TIMER_TCLR_GPO_CFG		BIT(14)

/*
 * TIMER_TSICR register
 */
#define TIMER_TSICR_SET			BIT(1)
#define TIMER_TSICR_POSTED		BIT(2)


#define to_am335x_timer(dev) container_of(dev,struct am335x_timer,clkevt)

struct am335x_timer{
	struct clock_event_device clkevt;
	struct clk *fclk;
	unsigned long cycle;
	void __iomem * base;
};

static irqreturn_t am335x_timer_irqhandler(int irq, void *dev_id)
{
	struct am335x_timer *timer = dev_id;
	uint32_t value;

	value = readl_relaxed(timer->base + TIMER_IRQSTATUS);
	writel_relaxed(value,timer->base + TIMER_IRQSTATUS);
	if (clockevent_state_periodic(&timer->clkevt) &&
			(value&TIMER_IRQSTATUS_OVF)){

		timer->clkevt.event_handler(&timer->clkevt);

		return IRQ_HANDLED;

	}
	return IRQ_NONE;
}

static int am335x_timer_set_next_event(unsigned long evt, struct clock_event_device *clkevt)
{
	return 0;
}

static int am335x_timer_set_state_shutdown(struct clock_event_device *clkevt)
{
	struct am335x_timer *timer = to_am335x_timer(clkevt);
	uint32_t value ;

	value = readl_relaxed(timer->base + TIMER_TCLR);
	value &= ~TIMER_TCLR_START;
	writel_relaxed(value,timer->base + TIMER_TCLR);

	return 0;
}

static int am335x_timer_set_state_oneshot(struct clock_event_device *clkevt)
{
	return 0;
}

static int am335x_timer_set_state_oneshot_stopped(struct clock_event_device *clkevt)
{
	return 0;
}

static int am335x_timer_set_state_periodic(struct clock_event_device *clkevt)
{
	struct am335x_timer *timer = to_am335x_timer(clkevt);
	uint32_t value ;

	value = ~0 - timer->cycle;

	writel(value,timer->base + TIMER_TLDR);

	writel(value,timer->base + TIMER_TCRR);

	writel(TIMER_TCLR_START|TIMER_TCLR_AUTORELOAD,timer->base + TIMER_TCLR);

	return 0;
}

static unsigned long  am335x_timer_hw_init(struct am335x_timer *timer,unsigned long freq)
{

	writel(TIMER_TIOCP_CFG_SOFTRESET,timer->base + TIMER_TIOCP_CFG);

	while(readl(timer->base + TIMER_TIOCP_CFG) & TIMER_TIOCP_CFG_SOFTRESET);

	writel(~0,timer->base + TIMER_IRQENABLE_CLR);
	writel(TIMER_IRQSTATUS_OVF,timer->base + TIMER_IRQENABLE_SET);

	return freq;
}

static int __init am335x_timer_of_init(struct device_node *node)
{
	struct am335x_timer *timer;

	unsigned long freq  = 0;
	unsigned long min_delta,max_delta;
	int irq;
	int ret = 0;

	timer = kzalloc(sizeof(*timer),GFP_KERNEL);
	if(!timer){
		pr_err("Could not alloc timer data");
		return -ENOMEM;
	}

	timer->base = of_iomap(node,0);
	if(!timer->base){
		pr_err("Could not alloc timer data");
		ret = -ENOMEM;
		goto failed_iomap;
	}

	timer->fclk = of_clk_get_by_name(node,"fclk");
	if(IS_ERR(timer->fclk)){
		pr_err("Could not get timer clock domain");
		ret = PTR_ERR(timer->fclk);
		goto failed_clock;
	}

	ret = clk_prepare_enable(timer->fclk);
	if(ret < 0){
		pr_err("Cound not enable timer function clock");
		goto failed_clock_enable;
	}

	freq = clk_get_rate(timer->fclk);

	irq = irq_of_parse_and_map(node,0);
	if(!irq){
		pr_err("Could not parse irq");
		ret = -EINVAL;
		goto failed_irq;

	}


	ret = request_irq(irq, am335x_timer_irqhandler,
			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
			  "am335x_tick", timer);
	if(ret){
		pr_err("Could not request irq");
		goto failed_irq;
	}


	/*
	 *  register clock event device
	 */
	timer->clkevt.name = "am335x timer";
	timer->clkevt.features = CLOCK_EVT_FEAT_ONESHOT|CLOCK_EVT_FEAT_PERIODIC;
	timer->clkevt.rating = HZ;
	timer->clkevt.set_next_event = am335x_timer_set_next_event;
	timer->clkevt.set_state_shutdown = am335x_timer_set_state_shutdown;
	timer->clkevt.set_state_oneshot = am335x_timer_set_state_oneshot;
	timer->clkevt.set_state_oneshot_stopped = am335x_timer_set_state_oneshot_stopped;
	timer->clkevt.set_state_periodic = am335x_timer_set_state_periodic;


	freq = am335x_timer_hw_init(timer,freq);


	timer->cycle = freq/timer->clkevt.rating;

	min_delta = 1;
	max_delta = 0xfffffffe;

	clockevents_config_and_register(&timer->clkevt,freq,min_delta,max_delta);

	return 0;
failed_irq:
failed_clock_enable:
	clk_put(timer->fclk);
failed_clock:
	iounmap(timer->base);
failed_iomap:
	kfree(timer);
	return ret;
}
CLOCKSOURCE_OF_DECLARE(am553x_timer, "ti,am335x-timer",am335x_timer_of_init);
