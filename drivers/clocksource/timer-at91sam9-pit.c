/*
 * timer-at91sam9-pit.c
 *
 *  Created on: Jul 9, 2017
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

#define PIT_MR          0x0
#define PIT_SR          0x4
#define PIT_PIVR        0x8
#define PIT_PIIR        0xc


#define PIT_MR_PIV_SHIFT         0
#define PIT_MR_PIV_MASK          0xfffff
#define PIT_MR_PITEN_SHIFT       24
#define PIT_MR_PITEN_ENABLE      1
#define PIT_MR_PITEN_DISABLE     0
#define PIT_MR_PITEN_MASK        1
#define PIT_MR_PITIEN_SHIFT      25
#define PIT_MR_PITIEN_ENABLE     1
#define PIT_MR_PITIEN_DISABLE    0
#define PIT_MR_PITIEN_MASK       1

#define PIT_SR_PITS              BIT(0)

#define PIT_PIVR_CPIV_SHIFT      0
#define PIT_PIVR_CPIV_MASK       0xfffff
#define PIT_PIVR_PICNT_SHIFT     20
#define PIT_PIVR_PICNT_MASK      0xfff

#define PIT_PIIR_CPIV_SHIFT      0
#define PIT_PIIR_CPIV_MASK       0xfffff
#define PIT_PIIR_PICNT_SHIFT     20
#define PIT_PIIR_PICNT_MASK      0xfff


struct pit_data{
	struct clock_event_device	clkevt;
	struct clk *clk;
	int irq;
	int cycle;
	void __iomem *base;
};
#define to_pit_data(dev) container_of(dev,struct pit_data,clkevt)


static irqreturn_t pit_irqhandler(int irq, void *dev_id)
{
	struct pit_data *data = dev_id;

	if (clockevent_state_periodic(&data->clkevt) &&
			(readl_relaxed(data->base + PIT_SR)&PIT_SR_PITS)){

		readl_relaxed(data->base + PIT_PIVR);
		data->clkevt.event_handler(&data->clkevt);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;

}
static int pit_clkevt_set_state_periodic(struct clock_event_device *dev)
{
	struct pit_data *data = to_pit_data(dev);
	uint32_t value ;

	value = PIT_MR_PITIEN_ENABLE << PIT_MR_PITIEN_SHIFT|
			PIT_MR_PITEN_ENABLE << PIT_MR_PITEN_SHIFT|
			(data->cycle-1) << PIT_MR_PIV_SHIFT;

	writel(value,data->base + PIT_MR);
	return 0;
}

static int pit_clkevt_set_state_shutdown(struct clock_event_device *dev)
{
	struct pit_data *data = to_pit_data(dev);
	uint32_t value ;

	value = readl(data->base + PIT_MR);

	value &= ~(PIT_MR_PITIEN_MASK<<PIT_MR_PITIEN_SHIFT|
			PIT_MR_PIV_MASK<<PIT_MR_PIV_SHIFT);
	value |= PIT_MR_PITIEN_DISABLE << PIT_MR_PITIEN_SHIFT|
			(data->cycle-1) << PIT_MR_PIV_SHIFT;

	writel(value,data->base + PIT_MR);
	return 0;
}


static int __init pit_of_init(struct device_node *node)
{
	struct pit_data *data;
	int ret;
	unsigned long rate;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->base = of_iomap(node, 0);
	if (!data->base) {
		pr_err("Could not map PIT address\n");
		ret =  -ENOMEM;
		goto failed_io_map;
	}

	data->clk = of_clk_get(node,0);
	if(IS_ERR(data->clk)){
		pr_err("Could not map PIT clk source\n");
		ret =  PTR_ERR(data->clk);
		goto failed_get_clk;
	}

	clk_prepare_enable(data->clk);

	rate = clk_get_rate(data->clk)/16;
	data->cycle = DIV_ROUND_CLOSEST(rate, HZ);


	data->irq = irq_of_parse_and_map(node, 0);
	if (!data->irq) {
		pr_err("Unable to get IRQ from DT\n");
		ret = -EINVAL;
		goto failed_irq;
	}

	ret = request_irq(data->irq, pit_irqhandler,
			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
			  "at91_tick", data);
	if(ret < 0){
		pr_err("Unable to request irq\n");
		goto failed_irq_req;
	}


	data->clkevt.name = "pit";
	data->clkevt.features = CLOCK_EVT_FEAT_PERIODIC;
	data->clkevt.rating = 100;
	data->clkevt.set_state_periodic = pit_clkevt_set_state_periodic;
	data->clkevt.set_state_shutdown = pit_clkevt_set_state_shutdown;

	clockevents_register_device(&data->clkevt);



	return 0;
failed_irq_req:
failed_irq:
	clk_put(data->clk);
failed_get_clk:
	iounmap(data->base);
failed_io_map:
	kfree(data);
	return ret;
}


CLOCKSOURCE_OF_DECLARE(at91sam9_pit, "atmel,at91sam9-pit",pit_of_init);
