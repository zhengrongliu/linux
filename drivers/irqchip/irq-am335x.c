/*
 * irq-am335x.c
 *
 *  Created on: Feb 6, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/exception.h>

#define INTC_NR_IRQS		128

#define INTC_REVISION 		0x0
#define INTC_SYSCONFIG		0x10
#define INTC_SYSSTATUS		0x14
#define INTC_SIR_IRQ		0x40
#define INTC_SIR_FIQ		0x44
#define INTC_CONTROL		0x48
#define INTC_PROTECTION		0x4c
#define INTC_IDLE		0x50
#define INTC_IRQ_PRIORITY	0x60
#define INTC_FIQ_PRIORITY	0x64
#define INTC_THRESHOLD		0x68
#define INTC_ITR(x)		(0x80+0x20*(x))
#define INTC_MIR(x)		(0x84+0x20*(x))
#define INTC_MIR_CLEAR(x)	(0x88+0x20*(x))
#define INTC_MIR_SET(x)		(0x8c+0x20*(x))
#define INTC_ISR_SET(x)		(0x90+0x20*(x))
#define INTC_ISR_CLEAR(x)	(0x94+0x20*(x))
#define INTC_PENDING_IRQ(x)	(0x98+0x20*(x))
#define INTC_PENDING_FIQ(x)	(0x9c+0x20*(x))
#define INTC_ILR(x)		(0x100 + 4*(x))

/*
 * INTC_REVISION register
 */
#define INTC_REVISION_REV_OFFSET	0x0
#define INTC_REVISION_REV_WIDTH		8
/*
 * INTC_SYSCONFIG register
 */
#define INTC_SYSCONFIG_AUTOIDLE		BIT(0)
#define INTC_SYSCONFIG_SOFTRESET	BIT(1)
/*
 * INTC_SYSSTATUS register
 */
#define INTC_SYSSTATUS_RESETDONE	BIT(0)
/*
 * INTC_SIR_IRQ register
 */
#define INTC_SIR_IRQ_ACTIVEIRQ_OFFSET	0
#define INTC_SIR_IRQ_ACTIVEIRQ_WIDTH	7
#define INTC_SIR_IRQ_ACTIVEIRQ_MASK	GENMASK(INTC_SIR_IRQ_ACTIVEIRQ_WIDTH-1,0)
#define INTC_SIR_IRQ_SPU_OFFSET 	7
#define INTC_SIR_IRQ_SPU_WIDTH		25
#define INTC_SIR_IRQ_SPU_MASK		GENMASK(INTC_SIR_IRQ_SPU_WIDTH-1,0)
/*
 * INTC_SIR_FIQ register
 */
#define INTC_SIR_FIQ_ACTIVEFIQ_OFFSET	0x0
#define INTC_SIR_FIQ_ACTIVEFIQ_WIDTH	7
#define INTC_SIR_FIQ_SPU_OFFSET 	0x7
#define INTC_SIR_FIQ_SPU_WIDTH		25
/*
 * INTC_CONTROL register
 */
#define INTC_CONTROL_NEWIRQAGR		BIT(0)
#define INTC_CONTROL_NEWFIQAGR		BIT(1)
/*
 * INTC_PROTECTION register
 */
#define INTC_PROTECTION_PROTECTION	BIT(0)
/*
 * INTC_IDLE register
 */
#define INTC_IDLE_FUNCIDLE		BIT(0)
#define INTC_IDLE_TURBO			BIT(1)
/*
 * INTC_IRQ_PRIORITY register
 */
#define INTC_IRQ_PRIORITY_PRIO_OFFSET	0
#define INTC_IRQ_PRIORITY_PRIO_WIDTH	7
#define INTC_IRQ_PRIORITY_SPU_OFFSET	7
#define INTC_IRQ_PRIORITY_SPU_WIDTH	9
/*
 * INTC_FIQ_PRIORITY register
 */
#define INTC_FIQ_PRIORITY_PRIO_OFFSET	0
#define INTC_FIQ_PRIORITY_PRIO_WIDTH	7
#define INTC_FIQ_PRIORITY_SPU_OFFSET	7
#define INTC_FIQ_PRIORITY_SPU_WIDTH	9
/*
 * INTC_THRESHOLD register
 */
#define INTC_THRESHOLD_PTH_OFFSET	0
#define INTC_THRESHOLD_PTH_WIDTH	8
/*
 * INTC_ILR register
 */
#define INTC_ILR_FIQnIRQ_OFFSET		0
#define INTC_ILR_FIQnIRQ_WIDTH		1
#define INTC_ILR_FIQ			1
#define INTC_ILR_IRQ			0
#define INTC_ILR_PRIORITY_OFFSET	2
#define INTC_ILR_PRIORITY_WIDTH		6

#define INTC_IRQ_PRIORITY_MIN		0
#define INTC_IRQ_PRIORITY_MAX		GENMASK(INTC_IRQ_PRIORITY_PRIO_WIDTH-1,0)

#define INTC_GIC_IRQS_PER_CHIP		32


struct am335x_intc_irqchip{
	void __iomem * base;
	struct irq_domain *domain;
};

static struct am335x_intc_irqchip am335x_intc;

static void intc_irq_eoi(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);

	writel(INTC_CONTROL_NEWIRQAGR,gc->reg_base + INTC_CONTROL);
	wmb();
}

static void __exception_irq_entry intc_handle_irq(struct pt_regs *regs)
{
	int irqnr;
	uint32_t value;

	value = readl(am335x_intc.base + INTC_SIR_IRQ);

	if(((value >> INTC_SIR_IRQ_SPU_OFFSET)&INTC_SIR_IRQ_SPU_MASK) != 0){
		return;
	}

	irqnr = value & INTC_SIR_IRQ_ACTIVEIRQ_MASK;

	handle_domain_irq(am335x_intc.domain,irqnr,regs);
}


static int intc_irq_domain_xlate(struct irq_domain *d, struct device_node *node,
		     const u32 *intspec, unsigned int intsize,
		     unsigned long *out_hwirq, unsigned int *out_type)
{
	uint32_t prior;

	if (WARN_ON(intsize < 2))
		return -EINVAL;

	if (WARN_ON((intspec[0] >= INTC_NR_IRQS)))
		return -EINVAL;

	if (WARN_ON((intspec[1] < INTC_IRQ_PRIORITY_MIN) ||
		    (intspec[1] > INTC_IRQ_PRIORITY_MAX)))
		return -EINVAL;

	*out_hwirq = intspec[0];
	*out_type = IRQ_TYPE_LEVEL_HIGH;

	prior = intspec[1];

	writel(prior<<INTC_ILR_PRIORITY_OFFSET|INTC_ILR_IRQ,
			am335x_intc.base + INTC_ILR(*out_hwirq));
	wmb();

	return 0;
}

static const struct irq_domain_ops intc_irq_ops = {
	.map	= irq_map_generic_chip,
	.xlate	= intc_irq_domain_xlate,
};

static void intc_hw_init(struct am335x_intc_irqchip * intc)
{
	writel(INTC_SYSCONFIG_SOFTRESET,INTC_SYSCONFIG+intc->base);

	writel(0xFFFFFFFF,INTC_MIR_SET(0)+intc->base);
	writel(0xFFFFFFFF,INTC_MIR_SET(1)+intc->base);
	writel(0xFFFFFFFF,INTC_MIR_SET(2)+intc->base);
	writel(0xFFFFFFFF,INTC_MIR_SET(3)+intc->base);
}

static int __init am335x_intc_of_init(struct device_node *node,
			       struct device_node *parent)
{
	struct irq_chip_generic *gc;
	int ret;
	int nchips;
	int i;

	nchips = DIV_ROUND_UP(INTC_NR_IRQS, 32);

	/*
	 *  linear mapping is suitable for am335x intc
	 */
	am335x_intc.domain = irq_domain_add_linear(node, INTC_NR_IRQS, &intc_irq_ops, NULL);
	if(am335x_intc.domain == NULL){
		return -ENOMEM;
	}


	/*
	 * alloc generic irq chips,32 irq per chip and only one type of irqchip.
	 * the irq handle flow is fast eoi
	 */
	ret = irq_alloc_domain_generic_chips(am335x_intc.domain, 32, 1, "am335x-intc",
					     handle_fasteoi_irq,
					     IRQ_NOREQUEST | IRQ_NOPROBE |
					     IRQ_NOAUTOEN, 0, 0);
	if(ret)
		goto failed_alloc_generic_chips;


	am335x_intc.base = of_iomap(node, 0);
	if(!am335x_intc.base){
		ret = -ENOMEM;
		goto failed_iomap;
	}

	/*
	 * initialize generic irq chips
	 */
	for(i = 0; i < nchips;i++){
		gc = irq_get_domain_generic_chip(am335x_intc.domain, i * 32);

		gc->reg_base = am335x_intc.base;


		/*
		 * eoi func is quirk
		 */
		gc->chip_types[0].type = IRQ_TYPE_SENSE_MASK;
		gc->chip_types[0].chip.irq_eoi = intc_irq_eoi;
		gc->chip_types[0].chip.irq_mask = irq_gc_mask_disable_reg;
		gc->chip_types[0].chip.irq_unmask = irq_gc_unmask_enable_reg;
		gc->chip_types[0].regs.enable = INTC_MIR_CLEAR(i);
		gc->chip_types[0].regs.disable = INTC_MIR_SET(i);

	}


	intc_hw_init(&am335x_intc);

	set_handle_irq(intc_handle_irq);

	return 0;
failed_iomap:
failed_alloc_generic_chips:
	irq_domain_remove(am335x_intc.domain);
	return ret;
}


IRQCHIP_DECLARE(am335x_irq, "ti,am335x-intc", am335x_intc_of_init);
