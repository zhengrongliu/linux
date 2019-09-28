/*
 * dma-at91sam9.c
 *
 *  Created on: Dec 6, 2017
 *      Author: Zhengrong Liu<towering@126.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/dmapool.h>
#include <linux/dmaengine.h>

#include "virt-dma.h"

#define DRIVER_NAME "at91sam9-dma"

#define DMAC_MAX_CHANS		8
#define DMAC_MAX_LLI_PER_CHAN   8

#define DMAC_GCFG		0x00
#define DMAC_EN			0x04
#define DMAC_SREQ		0x08
#define DMAC_CREQ		0x0c
#define DMAC_LAST		0x10
#define DMAC_EBCIER		0x18
#define DMAC_EBCIDR		0x1c
#define DMAC_EBCIMR		0x20
#define DMAC_EBCISR		0x24
#define DMAC_CHER		0x28
#define DMAC_CHDR		0x2c
#define DMAC_CHSR		0x30
#define DMAC_SADDR(x)		(0x3c + (x)*0x28 + 0x00)
#define DMAC_DADDR(x)		(0x3c + (x)*0x28 + 0x04)
#define DMAC_DSCR(x)		(0x3c + (x)*0x28 + 0x08)
#define DMAC_CTRLA(x)		(0x3c + (x)*0x28 + 0x0c)
#define DMAC_CTRLB(x)		(0x3c + (x)*0x28 + 0x10)
#define DMAC_CFG(x)		(0x3c + (x)*0x28 + 0x14)
#define DMAC_SPIP(x)		(0x3c + (x)*0x28 + 0x18)
#define DMAC_DPIP(x)		(0x3c + (x)*0x28 + 0x1c)
#define DMAC_WPMR		0x1e4
#define DMAC_WPSR		0x1e8

/*
 * DMAC Global Configuration Register
 */
#define DMAC_GCFG_ARB_CFG_FIXED		(0<<4)
#define DMAC_GCFG_ARB_CFG_RR		(1<<4)
#define DMAC_GCFG_ARB_CFG_MASK		(1<<4)

/*
 * DMAC Enable Register
 */
#define DMAC_EN_ENABLE 			(0<<0)
#define DMAC_EN_DISABLE 		(1<<0)

/*
 * DMAC Software Single Request Register
 */
#define DMAC_SREG_SSREG(x)		(1<<((x)*2))
#define DMAC_SREG_DSREG(x)		(1<<((x)*2+1))

/*
 * DMAC Software Chunk Transfer Request Register
 */
#define DMAC_CREG_SCREG(x)		(1<<((x)*2))
#define DMAC_CREG_DCREG(x)		(1<<((x)*2+1))


/*
 * DMAC Software Last Transfer Flag Register
 */
#define DMAC_LAST_SLAST(x)		(1<<((x)*2))
#define DMAC_LAST_DLAST(x)		(1<<((x)*2+1))

/*
 * DMAC Error,Buffer Transfer and Chained Buffer Transfer Event
 */
#define DMAC_EVENT_BTC(x)		(1<<(x))
#define DMAC_EVENT_CBTC(x)		(1<<((x)+8))
#define DMAC_EVENT_ERR(x)		(1<<((x)+16))

/*
 * DMAC Channel Handler Enable Register
 */
#define DMAC_CHER_ENA(x)		(1<<(x))
#define DMAC_CHER_SUSP(x)		(1<<((x)+8))
#define DMAC_CHER_KEEP(x)		(1<<((x)+24))

/*
 * DMAC Channel Handler Disable Register
 */
#define DMAC_CHDR_DIS(x)		(1<<(x))
#define DMAC_CHDR_RES(x)		(1<<((x)+8))

/*
 * DMAC Channel Handler Status Register
 */
#define DMAC_CHSR_ENA(x)		(1<<(x))
#define DMAC_CHSR_SUSP(x)		(1<<((x)+8))
#define DMAC_CHSR_EMPT(x)		(1<<((x)+16))
#define DMAC_CHSR_STAL(x)		(1<<((x)+24))

/*
 * DMAC Descriptor Address Register
 */
#define DMAC_DSCR_AHB_IF0		(0<<0)
#define DMAC_DSCR_AHB_IF1		(1<<0)
#define DMAC_DSCR_DSCR_SHIFT		2
#define DMAC_DSCR_DSCR_WIDTH		30
#define DMAC_DSCR_DSCR_MASK		((1<<DMAC_DSCR_DSCR_WIDTH)-1)

/*
 * DMAC Channel Control A Register
 */
#define DMAC_CTRLA_BTSIZE_SHIFT		0
#define DMAC_CTRLA_BTSIZE_WIDTH		16
#define DMAC_CTRLA_BTSIZE_MASK		((1<<DMAC_CTRLA_BTSIZE_WIDTH)-1)
#define DMAC_CTRLA_SCSIZE_SHIFT		16
#define DMAC_CTRLA_SCSIZE_WIDTH		3
#define DMAC_CTRLA_SCSIZE_MASK		((1<<DMAC_CTRLA_SCSIZE_WIDTH)-1)
#define DMAC_CTRLA_SCSIZE_CHK_1		0
#define DMAC_CTRLA_SCSIZE_CHK_4		1
#define DMAC_CTRLA_SCSIZE_CHK_8		2
#define DMAC_CTRLA_SCSIZE_CHK_16	3
#define DMAC_CTRLA_DCSIZE_SHIFT		20
#define DMAC_CTRLA_DCSIZE_WIDTH		3
#define DMAC_CTRLA_DCSIZE_MASK		((1<<DMAC_CTRLA_DCSIZE_WIDTH)-1)
#define DMAC_CTRLA_DCSIZE_CHK_1		0
#define DMAC_CTRLA_DCSIZE_CHK_4		1
#define DMAC_CTRLA_DCSIZE_CHK_8		2
#define DMAC_CTRLA_DCSIZE_CHK_16	3
#define DMAC_CTRLA_SRC_WIDTH_SHIFT	24
#define DMAC_CTRLA_SRC_WIDTH_WIDTH	2
#define DMAC_CTRLA_SRC_WIDTH_MASK	((1<<DMAC_CTRLA_SRC_WIDTH_WIDTH)-1)
#define DMAC_CTRLA_SRC_WIDTH_BYTE	0
#define DMAC_CTRLA_SRC_WIDTH_HALF_WORD	1
#define DMAC_CTRLA_SRC_WIDTH_WORD	2
#define DMAC_CTRLA_DST_WIDTH_SHIFT	28
#define DMAC_CTRLA_DST_WIDTH_WIDTH	2
#define DMAC_CTRLA_DST_WIDTH_MASK	((1<<DMAC_CTRLA_DST_WIDTH_WIDTH)-1)
#define DMAC_CTRLA_DST_WIDTH_BYTE	0
#define DMAC_CTRLA_DST_WIDTH_HALF_WORD	1
#define DMAC_CTRLA_DST_WIDTH_WORD	2
#define DMAC_CTRLA_DONE_SHIFT		31
#define DMAC_CTRLA_DONE_WIDTH		1
#define DMAC_CTRLA_DONE_MASK		((1<<DMAC_CTRLA_DONE_WIDTH)-1)

/*
 * DMAC Channel Control B Register
 */
#define DMAC_CTRLB_SIF_SHIFT		0
#define DMAC_CTRLB_SIF_WIDTH		2
#define DMAC_CTRLB_SIF_MASK		((1<<DMAC_CTRLB_SIF_WIDTH)-1)
#define DMAC_CTRLB_SIF_AHB_IF0		0
#define DMAC_CTRLB_SIF_AHB_IF1		1
#define DMAC_CTRLB_DIF_SHIFT		4
#define DMAC_CTRLB_DIF_WIDTH		2
#define DMAC_CTRLB_DIF_MASK		((1<<DMAC_CTRLB_DIF_WIDTH)-1)
#define DMAC_CTRLB_DIF_AHB_IF0		0
#define DMAC_CTRLB_DIF_AHB_IF1		1
#define DMAC_CTRLB_SRC_PIP_SHIFT	8
#define DMAC_CTRLB_SRC_PIP_WIDTH	1
#define DMAC_CTRLB_SRC_PIP_MASK		((1<<DMAC_CTRLB_SRC_PIP_WIDTH)-1)
#define DMAC_CTRLB_DST_PIP_SHIFT	12
#define DMAC_CTRLB_DST_PIP_WIDTH	1
#define DMAC_CTRLB_DST_PIP_MASK		((1<<DMAC_CTRLB_DST_PIP_WIDTH)-1)
#define DMAC_CTRLB_SRC_DSCR_SHIFT	16
#define DMAC_CTRLB_SRC_DSCR_WIDTH	1
#define DMAC_CTRLB_SRC_DSCR_MASK	((1<<DMAC_CTRLB_SRC_DSCR_WIDTH)-1)
#define DMAC_CTRLB_DST_DSCR_SHIFT	20
#define DMAC_CTRLB_DST_DSCR_WIDTH	1
#define DMAC_CTRLB_DST_DSCR_MASK	((1<<DMAC_CTRLB_DST_DSCR_WIDTH)-1)
#define DMAC_CTRLB_FC_SHIFT		21
#define DMAC_CTRLB_FC_WIDTH		2
#define DMAC_CTRLB_FC_MASK		((1<<DMAC_CTRLB_FC_WIDTH)-1)
#define DMAC_CTRLB_FC_MEM2MEM		0
#define DMAC_CTRLB_FC_MEM2PER		1
#define DMAC_CTRLB_FC_PER2MEM		2
#define DMAC_CTRLB_FC_PER2PER		3
#define DMAC_CTRLB_SRC_INCR_SHIFT	24
#define DMAC_CTRLB_SRC_INCR_WIDTH	2
#define DMAC_CTRLB_SRC_INCR_MASK	((1<<DMAC_CTRLB_SRC_INCR_WIDTH)-1)
#define DMAC_CTRLB_SRC_INCR_INCREMENT	0
#define DMAC_CTRLB_SRC_INCR_DECREMENT	1
#define DMAC_CTRLB_SRC_INCR_FIXED		2
#define DMAC_CTRLB_DST_INCR_SHIFT	28
#define DMAC_CTRLB_DST_INCR_WIDTH	2
#define DMAC_CTRLB_DST_INCR_MASK	((1<<DMAC_CTRLB_DST_INCR_WIDTH)-1)
#define DMAC_CTRLB_DST_INCR_INCREMENT	0
#define DMAC_CTRLB_DST_INCR_DECREMENT	1
#define DMAC_CTRLB_DST_INCR_FIXED	2
#define DMAC_CTRLB_IEN_SHIFT		30
#define DMAC_CTRLB_IEN_WIDTH		1
#define DMAC_CTRLB_IEN_MASK		((1<<DMAC_CTRLB_IEN_WIDTH)-1)
#define DMAC_CTRLB_AUTO_SHIFT		31
#define DMAC_CTRLB_AUTO_WIDTH		1
#define DMAC_CTRLB_AUTO_MASK		((1<<DMAC_CTRLB_AUTO_WIDTH)-1)

/*
 * DMAC Channel Configuration Register
 */
#define DMAC_CFG_SRC_PER_SHIFT		0
#define DMAC_CFG_SRC_PER_WIDTH		4
#define DMAC_CFG_SRC_PER_MASK		((1<<DMAC_CFG_SRC_PER_WIDTH)-1)
#define DMAC_CFG_DST_PER_SHIFT		4
#define DMAC_CFG_DST_PER_WIDTH		4
#define DMAC_CFG_DST_PER_MASK		((1<<DMAC_CFG_DST_PER_WIDTH)-1)
#define DMAC_CFG_SRC_REP_SHIFT		8
#define DMAC_CFG_SRC_REP_WIDTH		1
#define DMAC_CFG_SRC_REP_MASK		((1<<DMAC_CFG_SRC_REP_WIDTH)-1)
#define DMAC_CFG_SRC_REP_CONTIGUOUS	0
#define DMAC_CFG_SRC_REP_RELOAD		1
#define DMAC_CFG_SRC_H2SEL_SHIFT	9
#define DMAC_CFG_SRC_H2SEL_WIDTH	1
#define DMAC_CFG_SRC_H2SEL_MASK		((1<<DMAC_CFG_SRC_H2SEL_WIDTH)-1)
#define DMAC_CFG_SRC_H2SEL_SW		0
#define DMAC_CFG_SRC_H2SEL_HW		1
#define DMAC_CFG_DST_REP_SHIFT		12
#define DMAC_CFG_DST_REP_WIDTH		1
#define DMAC_CFG_DST_REP_MASK		((1<<DMAC_CFG_DST_REP_WIDTH)-1)
#define DMAC_CFG_DST_REP_CONTIGUOUS	0
#define DMAC_CFG_DST_REP_RELOAD		1
#define DMAC_CFG_DST_H2SEL_SHIFT	13
#define DMAC_CFG_DST_H2SEL_WIDTH	1
#define DMAC_CFG_DST_H2SEL_MASK		((1<<DMAC_CFG_DST_H2SEL_WIDTH)-1)
#define DMAC_CFG_DST_H2SEL_SW		0
#define DMAC_CFG_DST_H2SEL_HW		1
#define DMAC_CFG_SOD_SHIFT		16
#define DMAC_CFG_SOD_WIDTH		1
#define DMAC_CFG_SOD_MASK		((1<<DMAC_CFG_SOD_WIDTH)-1)
#define DMAC_CFG_SOD_DISABLE		0
#define DMAC_CFG_SOD_ENABLE		1
#define DMAC_CFG_LOCK_IF_SHIFT		20
#define DMAC_CFG_LOCK_IF_WIDTH		1
#define DMAC_CFG_LOCK_IF_MASK		((1<<DMAC_CFG_LOCK_IF_WIDTH)-1)
#define DMAC_CFG_LOCK_IF_DISABLE	0
#define DMAC_CFG_LOCK_IF_ENABLE		1
#define DMAC_CFG_LOCK_B_SHIFT		21
#define DMAC_CFG_LOCK_B_WIDTH		1
#define DMAC_CFG_LOCK_B_MASK		((1<<DMAC_CFG_LOCK_B_WIDTH)-1)
#define DMAC_CFG_LOCK_B_DISABLE		0
#define DMAC_CFG_LOCK_B_ENABLE		1
#define DMAC_CFG_LOCK_IF_L_SHIFT	22
#define DMAC_CFG_LOCK_IF_L_WIDTH	1
#define DMAC_CFG_LOCK_IF_L_MASK		((1<<DMAC_CFG_LOCK_IF_L_WIDTH)-1)
#define DMAC_CFG_LOCK_IF_L_DISABLE	0
#define DMAC_CFG_LOCK_IF_L_ENABLE	1
#define DMAC_CFG_AHB_PROT_SHIFT		24
#define DMAC_CFG_AHB_PROT_WIDTH		3
#define DMAC_CFG_AHB_PROT_MASK		((1<<DMAC_CFG_AHB_PROT_WIDTH)-1)
#define DMAC_CFG_FIFOCFG_SHIFT		28
#define DMAC_CFG_FIFOCFG_WIDTH		2
#define DMAC_CFG_FIFOCFG_MASK		((1<<DMAC_CFG_FIFOCFG_WIDTH)-1)
#define DMAC_CFG_FIFOCFG_ALAP_CFG	0
#define DMAC_CFG_FIFOCFG_HALF_CFG	1
#define DMAC_CFG_FIFOCFG_ASAP_CFG	2

/*
 * DMAC Channel Source Picture-in-Picture Configuration Register
 */
#define DMAC_SPIP_SPIP_HOLD_SHIFT	0
#define DMAC_SPIP_SPIP_HOLD_WIDTH	16
#define DMAC_SPIP_SPIP_HOLD_MASK	((1<<DMAC_SPIP_SPIP_HOLD_WIDTH)-1)
#define DMAC_SPIP_SPIP_BOUNDARY_SHIFT	16
#define DMAC_SPIP_SPIP_BOUNDARY_WIDTH	10
#define DMAC_SPIP_SPIP_BOUNDARY_MASK	((1<<DMAC_SPIP_SPIP_BOUNDARY_WIDTH)-1)

/*
 * DMAC Channel Destination Picture-in-Picture Configuration Register
 */
#define DMAC_DPIP_DPIP_HOLD_SHIFT	0
#define DMAC_DPIP_DPIP_HOLD_WIDTH	16
#define DMAC_DPIP_DPIP_HOLD_MASK	((1<<DMAC_DPIP_DPIP_HOLD_WIDTH)-1)
#define DMAC_DPIP_DPIP_BOUNDARY_SHIFT	16
#define DMAC_DPIP_DPIP_BOUNDARY_WDITH	10
#define DMAC_DPIP_DPIP_BOUNDARY_MASK	((1<<DMAC_DPIP_DPIP_BOUNDARY_WIDTH)-1)

/*
 * DMAC Write Protection Mode Register
 */
#define DMAC_WPMR_WPEN_SHIFT		0
#define DMAC_WPMR_WPEN_WIDTH		1
#define DMAC_WPMR_WPEN_MASK		((1<<DMAC_WPMR_WPEN_WIDTH)-1)
#define DMAC_WPMR_WPKEY_SHIFT		8
#define DMAC_WPMR_WPKEY_WIDTH		24
#define DMAC_WPMR_WPKEY_MASK		((1<<DMAC_WPMR_WPKEY_WIDTH)-1)

/*
 * DMAC Write Protection Status Register
 */
#define DMAC_WPSR_WPVS_SHIFT		0
#define DMAC_WPSR_WPVS_WIDTH		1
#define DMAC_WPSR_WPVS_MASK		((1<<DMAC_WPSR_WPVS_WIDTH)-1)
#define DMAC_WPSR_WPVSRC_SHIFT		8
#define DMAC_WPSR_WPVSRC_WIDTH		16
#define DMAC_WPSR_WPVSRC_MASK		((1<<DMAC_WPSR_WPVSRC_WIDTH)-1)

#define __FIELD_MASK(regname,fieldname) \
    (regname##_##fieldname##_MASK<<regname##_##fieldname##_SHIFT)

#define __FIELD_VALUE_FIXED(regname,fieldname,valname) \
    (regname##_##fieldname##_##valname<<regname##_##fieldname##_SHIFT)

#define __FIELD_IS_SET(regname,fieldname,valname,value) \
    (((value) &(regname##_##fieldname##_MASK<<regname##_##fieldname##_SHIFT)) == \
    (regname##_##fieldname##_##valname<<regname##_##fieldname##_SHIFT))

#define __FIELD_VALUE_SET(regname,fieldname,value) \
    (((value)&regname##_##fieldname##_MASK)<<regname##_##fieldname##_SHIFT)

#define __FIELD_VALUE_GET(regname,fieldname,value) \
    (((value)>>regname##_##fieldname##_SHIFT)&regname##_##fieldname##_MASK)




struct at91sam9_dma_desc;
struct at91sam9_dma_chan;

struct at91sam9_dma_device {
	struct dma_device ddev;
	void __iomem *base;
	struct dma_pool *desc_pool;
	struct at91sam9_dma_chan * chans[DMAC_MAX_CHANS];
	uint32_t event_mask;
	volatile uint32_t event_status;
};


struct at91sam9_dma_lli{
	uint32_t saddr;
	uint32_t daddr;
	uint32_t ctrla;
	uint32_t ctrlb;
	uint32_t dscr;
};

struct at91sam9_dma_hw_desc{
	struct at91sam9_dma_lli lli;
	dma_addr_t phys_addr;
};

struct at91sam9_dma_desc{
	struct virt_dma_desc vdesc;
	struct at91sam9_dma_device *dmadev;
	int hdesc_count;
	struct at91sam9_dma_hw_desc * hdesc[];
};

struct at91sam9_dma_chan {
	struct virt_dma_chan vchan;
	struct at91sam9_dma_device *dmadev;
	struct dma_slave_config dma_sconfig;
	struct scatterlist *sg;
	struct at91sam9_dma_desc *desc;
	int periph_id;
	int chan_id;
};



static struct at91sam9_dma_chan *to_at91sam9_chan(struct virt_dma_chan *vchan)
{
	return container_of(vchan, struct at91sam9_dma_chan, vchan);
}

static struct at91sam9_dma_desc * to_at91sam9_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct at91sam9_dma_desc, vdesc);
}



static int dmac_alloc_chan_resources(struct dma_chan *chan)
{

	return 0;
}

static void dmac_free_chan_resources(struct dma_chan *chan)
{

}

static int to_chunk_size(int burst_size)
{
	switch(burst_size){
	case 1:
		return DMAC_CTRLA_SCSIZE_CHK_1;
	case 4:
		return DMAC_CTRLA_SCSIZE_CHK_4;
	case 8:
		return DMAC_CTRLA_SCSIZE_CHK_8;
	case 16:
		return DMAC_CTRLA_SCSIZE_CHK_16;

	}
	return DMAC_CTRLA_SCSIZE_CHK_1;
}

static int to_xfer_size(int bus_width)
{
	switch(bus_width){
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		return DMAC_CTRLA_SRC_WIDTH_BYTE;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return DMAC_CTRLA_SRC_WIDTH_HALF_WORD;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return DMAC_CTRLA_SRC_WIDTH_WORD;
	}
	return  DMAC_CTRLA_SRC_WIDTH_BYTE;
}

static struct dma_async_tx_descriptor *dmac_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct at91sam9_dma_chan *ac = to_at91sam9_chan(to_virt_chan(chan));
	struct at91sam9_dma_device *dmadev = ac->dmadev;
	struct at91sam9_dma_desc *ad;
	struct at91sam9_dma_hw_desc * hw_desc;
	struct at91sam9_dma_lli *lli,*prev=NULL;
	struct scatterlist *sg;
	dma_addr_t addr;
	uint32_t reg_width;
	int i;

	ad = kzalloc(sizeof(*ad) + sizeof(hw_desc)*sg_len,GFP_KERNEL);
	if(ad == NULL){
		return NULL;
	}

	if(!(dmadev->ddev.directions & BIT(direction))){
		return NULL;
	}

	ad->hdesc_count = sg_len;
	ad->dmadev = dmadev;

	for_each_sg(sgl, sg, sg_len, i) {
		hw_desc = dma_pool_zalloc(dmadev->desc_pool,GFP_KERNEL,&addr);
		lli = &hw_desc->lli;


		switch(direction){
		case DMA_MEM_TO_DEV:
			reg_width = to_xfer_size(ac->dma_sconfig.dst_addr_width);

			lli->saddr = sg_dma_address(sg);
			lli->daddr = ac->dma_sconfig.dst_addr;

			lli->ctrla = __FIELD_VALUE_SET(DMAC_CTRLA,BTSIZE,sg_dma_len(sg) >> reg_width)|
					__FIELD_VALUE_SET(DMAC_CTRLA,DST_WIDTH,reg_width)|
					__FIELD_VALUE_SET(DMAC_CTRLA,DCSIZE,to_chunk_size(ac->dma_sconfig.dst_maxburst))|
					__FIELD_VALUE_SET(DMAC_CTRLA,SCSIZE,to_chunk_size(ac->dma_sconfig.src_maxburst))|
					__FIELD_VALUE_FIXED(DMAC_CTRLA,SRC_WIDTH,WORD);

			lli->ctrlb = __FIELD_VALUE_FIXED(DMAC_CTRLB,FC,MEM2PER)|
					__FIELD_VALUE_FIXED(DMAC_CTRLB,SRC_INCR,INCREMENT)|
					__FIELD_VALUE_FIXED(DMAC_CTRLB,DST_INCR,FIXED);
			break;
		case DMA_DEV_TO_MEM:
			reg_width = to_xfer_size(ac->dma_sconfig.src_addr_width);

			lli->saddr = ac->dma_sconfig.src_addr;
			lli->daddr = sg_dma_address(sg);

			lli->ctrla = __FIELD_VALUE_SET(DMAC_CTRLA,BTSIZE,sg_dma_len(sg) >> reg_width)|
					__FIELD_VALUE_SET(DMAC_CTRLA,SRC_WIDTH,reg_width)|
					__FIELD_VALUE_SET(DMAC_CTRLA,SCSIZE,to_chunk_size(ac->dma_sconfig.src_maxburst))|
					__FIELD_VALUE_SET(DMAC_CTRLA,DCSIZE,to_chunk_size(ac->dma_sconfig.dst_maxburst))|
					__FIELD_VALUE_FIXED(DMAC_CTRLA,DST_WIDTH,WORD);

			lli->ctrlb = __FIELD_VALUE_FIXED(DMAC_CTRLB,FC,PER2MEM)|
					__FIELD_VALUE_FIXED(DMAC_CTRLB,SRC_INCR,FIXED)|
					__FIELD_VALUE_FIXED(DMAC_CTRLB,DST_INCR,INCREMENT);

			break;
		default:
			break;
		}

		if(prev != NULL){
			prev->dscr = addr;
		}

		hw_desc->phys_addr = addr;
		ad->hdesc[i] = hw_desc;

		prev = lli;
	}

	return vchan_tx_prep(&ac->vchan,&ad->vdesc,flags);
}

static void dmac_desc_free(struct virt_dma_desc *vdesc)
{
	struct at91sam9_dma_desc *ad = to_at91sam9_desc(vdesc);
	int i;

	for(i = 0;i < ad->hdesc_count;i++){
		dma_pool_free(ad->dmadev->desc_pool,ad->hdesc[i],ad->hdesc[i]->phys_addr);
	}


	kfree(ad);
}



static struct dma_async_tx_descriptor *dmac_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	return NULL;
}

static enum dma_status dmac_tx_status(struct dma_chan *chan,
					    dma_cookie_t cookie,
					    struct dma_tx_state *txstate)
{
	return 0;
}

static void dmac_start_desc(struct at91sam9_dma_chan *ac)
{
	struct at91sam9_dma_device *dmadev = ac->dmadev;
	struct at91sam9_dma_desc *ad = to_at91sam9_desc(vchan_next_desc(&ac->vchan));
	uint32_t cfg;

	if(!ad) {
		ac->desc = NULL;
		return ;
	}

	list_del(&ad->vdesc.node);

	ac->desc = ad;

	cfg = __FIELD_VALUE_FIXED(DMAC_CFG,FIFOCFG,ASAP_CFG)|
			__FIELD_VALUE_SET(DMAC_CFG,DST_PER,ac->periph_id)|
			__FIELD_VALUE_SET(DMAC_CFG,SRC_PER,ac->periph_id)|
			__FIELD_VALUE_FIXED(DMAC_CFG,DST_H2SEL,SW)|
			__FIELD_VALUE_FIXED(DMAC_CFG,SRC_H2SEL,SW);


	writel(cfg,dmadev->base + DMAC_CFG(ac->chan_id));
	writel(ad->hdesc[0]->phys_addr,dmadev->base + DMAC_DSCR(ac->chan_id));

	writel(DMAC_CHER_ENA(ac->chan_id),dmadev->base + DMAC_CHER);


}

static void dmac_issue_pending(struct dma_chan *chan)
{
	struct at91sam9_dma_chan *ac = to_at91sam9_chan(to_virt_chan(chan));
	unsigned long flags;


	spin_lock_irqsave(&ac->vchan.lock, flags);
	if (vchan_issue_pending(&ac->vchan) && !ac->desc)
		dmac_start_desc(ac);

	spin_unlock_irqrestore(&ac->vchan.lock, flags);

}

static int dmac_chan_config(struct dma_chan *chan,
			     struct dma_slave_config *config)
{
	struct at91sam9_dma_chan *ac = to_at91sam9_chan(to_virt_chan(chan));

	ac->dma_sconfig = *config;


	return 0;
}

static struct dma_chan *dmac_of_xlate(struct of_phandle_args *dma_spec,
                                           struct of_dma *ofdma)
{
	struct at91sam9_dma_device *dmadev = ofdma->of_dma_data;
	struct at91sam9_dma_chan *ac;
	struct dma_chan *chan;

	chan = dma_get_any_slave_channel(&dmadev->ddev);
	if(!chan){
		return NULL;
	}

	ac = to_at91sam9_chan(to_virt_chan(chan));

	ac->periph_id = dma_spec->args[0];

	return chan;
}

static void dmac_chan_init(struct at91sam9_dma_device *dmadev,int chan_id)
{
	struct at91sam9_dma_chan *c;


	c = devm_kzalloc(dmadev->ddev.dev,sizeof(*c),GFP_KERNEL);

	c->vchan.desc_free = dmac_desc_free;
	c->chan_id = chan_id;
	c->dmadev = dmadev;

	dmadev->chans[chan_id] = c;

	vchan_init(&c->vchan,&dmadev->ddev);

}


static irqreturn_t dmac_irq_thread(int irq, void *dev)
{
	struct at91sam9_dma_device *dmadev = dev;
	struct at91sam9_dma_chan *chan;
	int bitno;


	while(dmadev->event_status){
		unsigned long value = dmadev->event_status;
		bitno = find_first_bit(&value,BITS_PER_LONG);

		if(bitno < 8){



		} else if(bitno < 16){
			chan = dmadev->chans[bitno - 8];

			dmadev->event_status &= ~(1<<bitno);

			vchan_cookie_complete(&chan->desc->vdesc);
			dmac_start_desc(chan);
		} else if(bitno < 24){

		}

	}

	writel(dmadev->event_mask,dmadev->base + DMAC_EBCIER);

	return IRQ_HANDLED;
}

static irqreturn_t dmac_irq_handle(int irq, void *dev)
{
	struct at91sam9_dma_device *dmadev  = dev;
	uint32_t status;

	status = readl(dmadev->base + DMAC_EBCISR);
	status &= dmadev->event_mask;

	if(status){
		dmadev->event_status |= status;
		writel(status,dmadev->base + DMAC_EBCIDR);
		return IRQ_WAKE_THREAD;
	}


	return IRQ_NONE;
}

static void dmac_hw_init(struct at91sam9_dma_device *dmadev)
{
	dmadev->event_mask = 0xff00;

	readl(dmadev->base + DMAC_EBCISR);
	readl(dmadev->base + DMAC_SADDR(1));

	writel(dmadev->event_mask,dmadev->base + DMAC_EBCIER);
}

static int at91sam9_dma_probe(struct platform_device *pdev)
{
	struct at91sam9_dma_device *dmadev;
	struct dma_device *dd;
	struct resource *res;
	int irq;
	int ret = 0;
	int i = 0;

        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        irq = platform_get_irq(pdev, 0);
        if (!res || irq < 0)
                return -ENXIO;


	dmadev = devm_kzalloc(&pdev->dev, sizeof(*dmadev), GFP_KERNEL);
	if(!dmadev)
		return -ENOMEM;

	dmadev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dmadev->base)) {
		return PTR_ERR(dmadev->base);
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, dmac_irq_handle,
			dmac_irq_thread, 0, DRIVER_NAME, dmadev);
	if(ret)
		return ret;

	dmadev->desc_pool = dma_pool_create("at91sam9_desc_pool",
			&pdev->dev, sizeof(struct at91sam9_dma_hw_desc),4,0);
	if(dmadev->desc_pool == NULL)
		return -ENOMEM;


	dd = &dmadev->ddev;

	dma_cap_set(DMA_SLAVE,dd->cap_mask);
	dma_cap_set(DMA_PRIVATE,dd->cap_mask);
	dma_cap_set(DMA_CYCLIC,dd->cap_mask);

	INIT_LIST_HEAD(&dd->channels);

	dd->device_alloc_chan_resources = dmac_alloc_chan_resources;
	dd->device_free_chan_resources = dmac_free_chan_resources;
	dd->device_prep_slave_sg = dmac_prep_slave_sg;
	dd->device_prep_dma_cyclic = dmac_prep_dma_cyclic;
	dd->device_config = dmac_chan_config;
	dd->device_issue_pending = dmac_issue_pending;
	dd->device_tx_status = dmac_tx_status;

	dd->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE)|
			BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dd->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE)|
			BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dd->max_burst = 16;

	dd->directions = BIT(DMA_DEV_TO_MEM)|BIT(DMA_MEM_TO_DEV);

	dd->dev = &pdev->dev;

	for(i = 0;i < DMAC_MAX_CHANS;i++){
		dmac_chan_init(dmadev,i);
	}

	dmac_hw_init(dmadev);

	platform_set_drvdata(pdev,dmadev);

	ret = of_dma_controller_register(pdev->dev.of_node,dmac_of_xlate,dmadev);
	if(ret)
		return ret;

	return 0;
failed_ioremap:

	return ret;
}

static int at91sam9_dma_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id at91sam9_dma_of_match[] = {
	{ .compatible = "atmel,at91sam9-dmac",},
};

static struct platform_driver at91sam9_dma_driver = {
	.driver = {
		.name   = DRIVER_NAME,
		.of_match_table = of_match_ptr(at91sam9_dma_of_match),
	},
	.probe          = at91sam9_dma_probe,
	.remove         = at91sam9_dma_remove,
};
module_platform_driver(at91sam9_dma_driver);


MODULE_DEVICE_TABLE(of, at91sam9_dma_of_match);
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("Atmel DMAC driver");
MODULE_LICENSE("GPL v2");



