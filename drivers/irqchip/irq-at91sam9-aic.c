/*
 * irq-at91sam9-aic.c
 *
 *  Created on: Jul 9, 2017
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

#include <asm/exception.h>

#define AIC_IRQS 32

#define AIC_SMR0      0x0
#define AIC_SMR31     0x7c
#define AIC_SMR(x)    (AIC_SMR0+4*(x))
#define AIC_SVR0      0x80
#define AIC_SVR31     0xfc
#define AIC_SVR(x)    (AIC_SVR0+4*(x))
#define AIC_IVR       0x100
#define AIC_FVR       0x104
#define AIC_ISR       0x108
#define AIC_IPR       0x10c
#define AIC_IMR       0x110
#define AIC_CISR      0x114
#define AIC_IECR      0x120
#define AIC_IDCR      0x124
#define AIC_ICCR      0x128
#define AIC_ISCR      0x12c
#define AIC_EOICR     0x130
#define AIC_SPU       0x134
#define AIC_DCR       0x138
#define AIC_FFER      0x140
#define AIC_FFDR      0x144
#define AIC_FFSR      0x148


#define AIC_SMR_PRIOR_SHIFT                0x0
#define AIC_SMR_PRIOR_MASK                 0x7
#define AIC_SMR_PRIOR_MIN                  0x0
#define AIC_SMC_PRIOR_MAX                  0x7
#define AIC_SMR_SRCTYPE_SHIFT              0x5
#define AIC_SMR_SRCTYPE_INT_LVL_HIGH       0x0
#define AIC_SMR_SRCTYPE_INT_EDGE_POSITIVE  0x1
#define AIC_SMR_SRCTYPE_EXT_LVL_LOW        0x0
#define AIC_SMR_SRCTYPE_EXT_EDGE_NEGATIVE  0x1
#define AIC_SMR_SRCTYPE_EXT_LVL_HIGH       0x2
#define AIC_SMR_SRCTYPE_EXT_EDGE_POSITIVE  0x3
#define AIC_SMR_SRCTYPE_MASK      0x3

#define AIC_DCR_PROT_SHIFT        0x0
#define AIC_DCR_GMSK_SHIFT        0x1


static struct irq_domain *aic_domain;
static void __iomem *aic_base;

static void __exception_irq_entry aic_handle_irq(struct pt_regs *regs)
{
	struct irq_domain_chip_generic *dgc = aic_domain->gc;
	struct irq_chip_generic *gc = dgc->gc[0];
	uint32_t irqnr;
	uint32_t irqstate;


	irqnr = irq_reg_readl(gc, AIC_IVR);
	irqstate = irq_reg_readl(gc, AIC_ISR);

	if(!irqstate){
		irq_reg_writel(gc,0,AIC_EOICR);
	} else {
		handle_domain_irq(aic_domain,irqnr,regs);
	}
}

static int aic_irq_domain_xlate(struct irq_domain *domain,
				struct device_node *ctrlr,
				const u32 *intspec, unsigned int intsize,
				irq_hw_number_t *out_hwirq,
				unsigned int *out_type)
{
	struct irq_chip_generic *gc;
	uint32_t smr;

	if (WARN_ON(intsize < 3))
		return -EINVAL;

	if (WARN_ON((intspec[2] < (AIC_SMR_PRIOR_MIN<<AIC_SMR_PRIOR_SHIFT)) ||
		    (intspec[2] > (AIC_SMC_PRIOR_MAX<<AIC_SMR_PRIOR_SHIFT))))
		return -EINVAL;

	if (WARN_ON((intspec[0] >= AIC_IRQS)))
		return -EINVAL;


	*out_hwirq = intspec[0];
	*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;

	gc = irq_get_domain_generic_chip(domain, *out_hwirq);

	smr = irq_reg_readl(gc, AIC_SMR(*out_hwirq));
	smr &= ~(AIC_SMR_PRIOR_MASK << AIC_SMR_PRIOR_SHIFT);
	smr |= (intspec[2] << AIC_SMR_PRIOR_SHIFT);
	irq_reg_writel(gc,smr,AIC_SMR(*out_hwirq));


	return 0;
}

static const struct irq_domain_ops aic_irq_ops = {
	.map	= irq_map_generic_chip,
	.xlate	= aic_irq_domain_xlate,
};

static void aic_hw_init(struct irq_domain *domain)
{
	struct irq_chip_generic *gc  = irq_get_domain_generic_chip(domain, 0);
	int i = 0;

	for(i = 0;i < AIC_IRQS;i++){
		irq_reg_writel(gc,i,AIC_SVR(i));
	}

	irq_reg_writel(gc,0xffffffff,AIC_SPU);
	irq_reg_writel(gc,0xffffffff,AIC_IDCR);

}

static int __init aic_of_init(struct device_node *node,
			       struct device_node *parent)
{
	struct irq_chip_generic *gc;
	int ret;
	int nchips;
	int i;


	nchips = DIV_ROUND_UP(AIC_IRQS, 32);

	if(aic_domain != NULL){
		return -EEXIST;
	}

	aic_domain = irq_domain_add_linear(node, AIC_IRQS, &aic_irq_ops, NULL);
	if(!aic_domain){
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(aic_domain, 32, 1, "at91sam9-aic",
					     handle_fasteoi_irq,
					     IRQ_NOREQUEST | IRQ_NOPROBE |
					     IRQ_NOAUTOEN, 0, 0);
	if(ret){
		goto failed_alloc_gc;
	}

	aic_base = of_iomap(node, 0);
	if (!aic_base){
		ret = -ENOMEM;
		goto failed_map_iobase;
	}


	for(i = 0;i < nchips;i++){
		gc = irq_get_domain_generic_chip(aic_domain, i * 32);

		gc->reg_base = aic_base;

		gc->chip_types[0].type = IRQ_TYPE_SENSE_MASK;
		gc->chip_types[0].chip.irq_eoi = irq_gc_eoi;
		gc->chip_types[0].chip.irq_mask = irq_gc_mask_disable_reg;
		gc->chip_types[0].chip.irq_unmask = irq_gc_unmask_enable_reg;
		gc->chip_types[0].regs.eoi = AIC_EOICR;
		gc->chip_types[0].regs.enable = AIC_IECR;
		gc->chip_types[0].regs.disable = AIC_IDCR;

	}

	aic_hw_init(aic_domain);

	set_handle_irq(aic_handle_irq);

	return 0;

failed_map_iobase:
failed_alloc_gc:
	irq_domain_remove(aic_domain);

	return ret;
}

IRQCHIP_DECLARE(at91sam9_aic, "atmel,at91sam9-aic", aic_of_init);
