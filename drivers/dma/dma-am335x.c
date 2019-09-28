/*
 * dma-am335x.c
 *
 *  Created on: Apr 15, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/dmapool.h>
#include <linux/dmaengine.h>

#include "virt-dma.h"

#define DRIVE_NAME "am335x-dma"


/*
 * dma channel controller
 */
#define EDMA_CC_PID		0x0
#define EDMA_CC_CCCFG		0x4
#define EDMA_CC_SYSCONFIG	0x10
#define EDMA_CC_DCHMAP(x)	(0x100 + 0x4*(x))
#define EDMA_CC_QCHMAP(x)	(0x200 + 0x4*(x))
#define EDMA_CC_DMAQNUM(x)	(0x240 + 0x4*(x))
#define EDMA_CC_QDMAQNUM	0x260
#define EDMA_CC_QUEPRI		0x284
#define EDMA_CC_EMR		0x300
#define EDMA_CC_EMRH		0x304
#define EDMA_CC_EMCR		0x308
#define EDMA_CC_EMCRH		0x30c
#define EDMA_CC_QEMR		0x310
#define EDMA_CC_QEMCR		0x314
#define EDMA_CC_CCERR		0x318
#define EDMA_CC_CCERRCLR	0x31c
#define EDMA_CC_EEVAL		0x320
#define EDMA_CC_DRAE0		0x340
#define EDMA_CC_DRAEH0		0x344
#define EDMA_CC_DRAE1		0x348
#define EDMA_CC_DRAEH1		0x34c
#define EDMA_CC_DRAE2		0x350
#define EDMA_CC_DRAEH2		0x354
#define EDMA_CC_DRAE3		0x358
#define EDMA_CC_DRAEH3		0x35c
#define EDMA_CC_DRAE4		0x360
#define EDMA_CC_DRAEH4		0x364
#define EDMA_CC_DRAE5		0x368
#define EDMA_CC_DRAEH5		0x36c
#define EDMA_CC_DRAE6		0x370
#define EDMA_CC_DRAEH6		0x374
#define EDMA_CC_DRAE7		0x378
#define EDMA_CC_DRAEH7		0x37c
#define EDMA_CC_QRAE(x)		(0x380 + 0x4*(x))
#define EDMA_CC_EQE(x,y)	(0x400 + (16*(x)+(y))*0x4)
#define EDMA_CC_QSTAT(x)	(0x600 + 0x4*(x))
#define EDMA_CC_QWMTHRA		0x620
#define EDMA_CC_CCSTAT		0x640
#define EDMA_CC_MPFAR		0x800
#define EDMA_CC_MPFSR		0x804
#define EDMA_CC_MPFCR		0x808
#define EDMA_CC_MPPAG		0x80c
#define EDMA_CC_MPPA(x)		(0x810 + 0x4*(x))
#define EDMA_CC_ER		0x1000
#define EDMA_CC_ERH		0x1004
#define EDMA_CC_ECR		0x1008
#define EDMA_CC_ECRH		0x100c
#define EDMA_CC_ESR		0x1010
#define EDMA_CC_ESRH		0x1014
#define EDMA_CC_CER		0x1018
#define EDMA_CC_CERH		0x101c
#define EDMA_CC_EER		0x1020
#define EDMA_CC_EERH		0x1024
#define EDMA_CC_EECR		0x1028
#define EDMA_CC_EECRH		0x102c
#define EDMA_CC_EESR		0x1030
#define EDMA_CC_EESRH		0x1034
#define EDMA_CC_SER		0x1038
#define EDMA_CC_SERH		0x103c
#define EDMA_CC_SECR		0x1040
#define EDMA_CC_SECRH		0x1044
#define EDMA_CC_IER		0x1050
#define EDMA_CC_IERH		0x1054
#define EDMA_CC_IECR		0x1058
#define EDMA_CC_IECRH		0x105c
#define EDMA_CC_IESR		0x1060
#define EDMA_CC_IESRH		0x1064
#define EDMA_CC_IPR		0x1068
#define EDMA_CC_IPRH		0x106c
#define EDMA_CC_ICR		0x1070
#define EDMA_CC_ICRH		0x1074
#define EDMA_CC_IEVAL		0x1078
#define EDMA_CC_QER		0x1080
#define EDMA_CC_QEER		0x1084
#define EDMA_CC_QEECR		0x1088
#define EDMA_CC_QEESR		0x108c
#define EDMA_CC_QSER		0x1090
#define EDMA_CC_QSECR		0x1094


#define EDMA_CC_Q0E0		0x400
#define EDMA_CC_Q0E1		0x404
#define EDMA_CC_Q0E2		0x408
#define EDMA_CC_Q0E3		0x40c
#define EDMA_CC_Q0E4		0x410
#define EDMA_CC_Q0E5		0x414
#define EDMA_CC_Q0E6		0x418
#define EDMA_CC_Q0E7		0x41c
#define EDMA_CC_Q0E8		0x420
#define EDMA_CC_Q0E9		0x424
#define EDMA_CC_Q0E10		0x428
#define EDMA_CC_Q0E11		0x42c
#define EDMA_CC_Q0E12		0x430
#define EDMA_CC_Q0E13		0x434
#define EDMA_CC_Q0E14		0x438
#define EDMA_CC_Q0E15		0x43c


/*
 * dma transfer controller
 */
#define EDMA_TC_PID		0x0
#define EDMA_TC_TCCFG		0x4
#define EDMA_TC_SYSCONFIG	0x10
#define EDMA_TC_TCSTAT		0x100
#define EDMA_TC_ERRSTAT		0x120
#define EDMA_TC_ERREN		0x124
#define EDMA_TC_ERRCLR		0x128
#define EDMA_TC_ERRDET		0x12c
#define EDMA_TC_ERRCMD		0x130
#define EDMA_TC_RDRATE		0x140
#define EDMA_TC_SAOPT		0x240
#define EDMA_TC_SASRC		0x244
#define EDMA_TC_SACNT		0x248
#define EDMA_TC_SADST		0x24c
#define EDMA_TC_SABIDX		0x250
#define EDMA_TC_SAMPPRXY	0x254
#define EDMA_TC_SACNTRLD	0x258
#define EDMA_TC_SASRCBREF	0x25c
#define EDMA_TC_SADSTBREF	0x260
#define EDMA_TC_DFCNTRLD	0x280
#define EDMA_TC_DFSRCBREF	0x284
#define EDMA_TC_DFDSTBREF	0x288
#define EDMA_TC_DFOPT0		0x300
#define EDMA_TC_DFSRC0		0x304
#define EDMA_TC_DFCNT0		0x308
#define EDMA_TC_DFDST0		0x30c
#define EDMA_TC_DFBIDX0		0x310
#define EDMA_TC_DFMPPRXY0	0x314
#define EDMA_TC_DFOPT1		0x340
#define EDMA_TC_DFSRC1		0x344
#define EDMA_TC_DFCNT1		0x348
#define EDMA_TC_DFDST1		0x34c
#define EDMA_TC_DFBIDX1		0x350
#define EDMA_TC_DFMPPRXY1	0x354
#define EDMA_TC_DFOPT2		0x380
#define EDMA_TC_DFSRC2		0x384
#define EDMA_TC_DFCNT2		0x388
#define EDMA_TC_DFDST2		0x38c
#define EDMA_TC_DFBIDX2		0x390
#define EDMA_TC_DFMPPRXY2	0x394
#define EDMA_TC_DFOPT3		0x3c0
#define EDMA_TC_DFSRC3		0x3c4
#define EDMA_TC_DFCNT3		0x3c8
#define EDMA_TC_DFDST3		0x3cc
#define EDMA_TC_DFBIDX3		0x3d0
#define EDMA_TC_DFMPPRXY3	0x3d4



/*
 * dma channel map register
 */
#define EDMA_CC_DCHMAP_PAENTRY_SHIFT	5
#define EDMA_CC_DCHMAP_PAENTRY_WIDTH	8

/*
 * dma q channel map register
 */
#define EDMA_CC_QCHMAP_PAENTRY_SHIFT	5
#define EDMA_CC_QCHMAP_PAENTRY_WIDTH	8
#define EDMA_CC_QCHMAP_TRWORD_SHIFT	2
#define EDMA_CC_QCHMAP_TRWORD_WIDTH	3

/*
 * dma channel queue number register
 */
#define EDMA_CC_DMAQNUM_E0_SHIFT		0
#define EDMA_CC_DMAQNUM_E0_WIDTH		3
#define EDMA_CC_DMAQNUM_E1_SHIFT		4
#define EDMA_CC_DMAQNUM_E1_WIDTH		3
#define EDMA_CC_DMAQNUM_E2_SHIFT		8
#define EDMA_CC_DMAQNUM_E2_WIDTH		3
#define EDMA_CC_DMAQNUM_E3_SHIFT		12
#define EDMA_CC_DMAQNUM_E3_WIDTH		3
#define EDMA_CC_DMAQNUM_E4_SHIFT		16
#define EDMA_CC_DMAQNUM_E4_WIDTH		3
#define EDMA_CC_DMAQNUM_E5_SHIFT		20
#define EDMA_CC_DMAQNUM_E5_WIDTH		3
#define EDMA_CC_DMAQNUM_E6_SHIFT		24
#define EDMA_CC_DMAQNUM_E6_WIDTH		3
#define EDMA_CC_DMAQNUM_E7_SHIFT		28
#define EDMA_CC_DMAQNUM_E7_WIDTH		3

/*
 * dma queue priority register
 */
#define EDMA_CC_QUEPRI_PRIQ0_SHIFT		0
#define EDMA_CC_QUEPRI_PRIQ0_WIDTH		3
#define EDMA_CC_QUEPRI_PRIQ1_SHIFT		4
#define EDMA_CC_QUEPRI_PRIQ1_WIDTH		3
#define EDMA_CC_QUEPRI_PRIQ2_SHIFT		8
#define EDMA_CC_QUEPRI_PRIQ2_WIDTH		3

/*
 * dma error register
 */
#define EDMA_CC_CCERR_QTHRXCD0		BIT(0)
#define EDMA_CC_CCERR_QTHRXCD1		BIT(1)
#define EDMA_CC_CCERR_QTHRXCD2		BIT(2)
#define EDMA_CC_CCERR_TCCERR		BIT(16)

/*
 * dma eeval register
 */
#define EDMA_CC_EEVAL_EVAL		BIT(0)

/*
 * dma event queue entry register
 */
#define EDMA_CC_QXEY_ENUM_SHIFT		0
#define EDMA_CC_QXEY_ENUM_WIDTH		6
#define EDMA_CC_QXEY_ETYPE_SHIFT	6
#define EDMA_CC_QXEY_ETYPE_WIDTH	2
#define EDMA_CC_QXEY_ETYPE_EVT_TRG	0
#define EDMA_CC_QXEY_ETYPE_AUTO_TRG	1

/*
 * dma queue status register
 */
#define EDMA_CC_QSTAT_STRTPTR_SHIFT	0
#define EDMA_CC_QSTAT_STRTPTR_WIDTH	4
#define EDMA_CC_QSTAT_NUMVAL_SHIFT	8
#define EDMA_CC_QSTAT_NUMVAL_WIDTH	5
#define EDMA_CC_QSTAT_WM_SHIFT		16
#define EDMA_CC_QSTAT_WM_WIDTH		5
#define EDMA_CC_QSTAT_THRXCD_SHIFT	24
#define EDMA_CC_QSTAT_THRXCD_WIDTH	1

/*
 * dma queue watermark threshold A register
 */
#define EDMA_CC_QWMTHRA_Q0_SHIFT		0
#define EDMA_CC_QWMTHRA_Q0_WIDTH		5
#define EDMA_CC_QWMTHRA_Q1_SHIFT		8
#define EDMA_CC_QWMTHRA_Q1_WIDTH		5
#define EDMA_CC_QWMTHRA_Q2_SHIFT		16
#define EDMA_CC_QWMTHRA_Q2_WIDTH		5

/*
 * dma status register
 */
#define EDMA_CC_CCSTAT_EVTACTV		BIT(0)
#define EDMA_CC_CCSTAT_QEVTACTV		BIT(1)
#define EDMA_CC_CCSTAT_TRACTV		BIT(2)
#define EDMA_CC_CCSTAT_ACTV		BIT(3)
#define EDMA_CC_CCSTAT_COMPACTV_SHIFT	8
#define EDMA_CC_CCSTAT_COMPACTV_WIDTH	6
#define EDMA_CC_CCSTAT_QUEACTV0		BIT(16)
#define EDMA_CC_CCSTAT_QUEACTV1		BIT(17)
#define EDMA_CC_CCSTAT_QUEACTV2		BIT(18)

/*
 * dma memory protection fault status register
 */
#define EDMA_CC_MPFSR_UXE			BIT(0)
#define EDMA_CC_MPFSR_UWE			BIT(1)
#define EDMA_CC_MPFSR_URE			BIT(2)
#define EDMA_CC_MPFSR_SXE			BIT(3)
#define EDMA_CC_MPFSR_SWE			BIT(4)
#define EDMA_CC_MPFSR_SRE			BIT(5)
#define EDMA_CC_MPFSR_FID_SHIFT		9
#define EDMA_CC_MPFSR_FID_WIDTH		4

/*
 * dma memory protection fault clear register
 */
#define EDMA_CC_MPFCR_MPFCLR		BIT(0)

/*
 * dma memory protection allow register
 */
#define EDMA_CC_MPPA_UX			BIT(0)
#define EDMA_CC_MPPA_UW			BIT(1)
#define EDMA_CC_MPPA_UR			BIT(2)
#define EDMA_CC_MPPA_SX			BIT(3)
#define EDMA_CC_MPPA_SW			BIT(4)
#define EDMA_CC_MPPA_SR			BIT(5)
#define EDMA_CC_MPPA_EXT		BIT(9)
#define EDMA_CC_MPPA_AID_SHIFT		10
#define EDMA_CC_MPPA_AID_WIDTH		6

/*
 * dma tc state register
 */
#define EDMA_TC_TCSTAT_DFSTRTPTR_SHIFT	12
#define EDMA_TC_TCSTAT_DFSTRTPTR_WIDTH	2
#define EDMA_TC_TCSTAT_DSTACTV_SHIFT	4
#define EDMA_TC_TCSTAT_DSTACTV_WIDTH	3
#define EDMA_TC_TCSTAT_WSACTV		BIT(2)
#define EDMA_TC_TCSTAT_SRCACTV		BIT(1)
#define EDMA_TC_TCSTAT_PROGBUSY		BIT(0)

/*
 * dma tc error register
 */
#define EDMA_TC_ERR_MMRAERR		BIT(3)
#define EDMA_TC_ERR_TRERR		BIT(2)
#define EDMA_TC_ERR_BUSERR		BIT(0)


/*
 * dma tc errdet register
 */
#define EDMA_TC_ERRDET_TCCHEN		BIT(17)
#define EDMA_TC_ERRDET_TCINTEN		BIT(16)
#define EDMA_TC_ERRDET_TCC_SHIFT	8
#define EDMA_TC_ERRDET_TCC_WIDTH	6
#define EDMA_TC_ERRDET_STAT_SHIFT	0
#define EDMA_TC_ERRDET_STAT_WDITH	4

/*
 * dma tc command register
 */
#define EDMA_TC_ERRCMD_EVAL		BIT(0)

/*
 * dma tc read rate register
 */
#define EDMA_TC_RDRATE_ASAP		0
#define EDMA_TC_RDRATE_CYCLES_4		1
#define EDMA_TC_RDRATE_CYCLES_8		2
#define EDMA_TC_RDRATE_CYCLES_16	3
#define EDMA_TC_RDRATE_CYCLES_32	4

/*
 * dma tc source active options register
 */
#define EDMA_TC_SAOPT_TCCHEN		BIT(22)
#define EDMA_TC_SAOPT_TCINTEN		BIT(20)
#define EDMA_TC_SAOPT_TCC_SHIFT		12
#define EDMA_TC_SAOPT_TCC_WIDTH		6
#define EDMA_TC_SAOPT_FWID_SHIFT	8
#define EDMA_TC_SAOPT_FWID_WIDTH	3
#define EDMA_TC_SAOPT_PRI_SHIFT		4
#define EDMA_TC_SAOPT_PRI_WIDTH		3
#define EDMA_TC_SAOPT_DAM		BIT(1)
#define EDMA_TC_SAOPT_SAM		BIT(0)


/*
 * edma params set entry options
 */
#define EDMA_PARAM_SET_OPT_SAM_INCR	0
#define EDMA_PARAM_SET_OPT_SAM_CONST	BIT(0)
#define EDMA_PARAM_SET_OPT_DAM_INCR	0
#define EDMA_PARAM_SET_OPT_DAM_CONST	BIT(1)
#define EDMA_PARAM_SET_OPT_A_SYNC	0
#define EDMA_PARAM_SET_OPT_AB_SYNC	BIT(2)
#define EDMA_PARAM_SET_OPT_STATIC	BIT(3)
#define EDMA_PARAM_SET_OPT_FIFO_8	(0<<8)
#define EDMA_PARAM_SET_OPT_FIFO_16	(1<<8)
#define EDMA_PARAM_SET_OPT_FIFO_32	(2<<8)
#define EDMA_PARAM_SET_OPT_FIFO_64	(3<<8)
#define EDMA_PARAM_SET_OPT_FIFO_128	(4<<8)
#define EDMA_PARAM_SET_OPT_FIFO_256	(5<<8)
#define EDMA_PARAM_SET_OPT_TCCMODE_NORMAL	0
#define EDMA_PARAM_SET_OPT_TCCMODE_EARLY	BIT(11)
#define EDMA_PARAM_SET_OPT_TCC(x)	((x)<<12)
#define EDMA_PARAM_SET_OPT_TCINTEN	BIT(20)
#define EDMA_PARAM_SET_OPT_ITCINTEN	BIT(21)
#define EDMA_PARAM_SET_OPT_TCCHEN	BIT(22)
#define EDMA_PARAM_SET_OPT_ITCCHEN	BIT(23)
#define EDMA_PARAM_SET_OPT_PRIVID_SHIFT	24
#define EDMA_PARAM_SET_OPT_PRIVID_WIDTH	4
#define EDMA_PARAM_SET_OPT_PRIV		BIT(31)


#define _F_MASK(regname,fieldname) \
    GENMASK(regname##_##fieldname##_SHIFT+regname##_##fieldname##_WIDTH - 1,regname##_##fieldname##_SHIFT)

#define _F_MAX(regname,fieldname) \
    GENMASK(regname##_##fieldname##_SHIFT+regname##_##fieldname##_WIDTH - 1,0)

#define _F_CONST(regname,fieldname,constname) \
    (regname##_##fieldname##_##constname<<regname##_##fieldname##_SHIFT)

#define _F_CHECK(regname,fieldname,valname,value) \
    (((value) &GENMASK(regname##_##fieldname##_SHIFT+regname##_##fieldname##_WIDTH - 1,regname##_##fieldname##_SHIFT)) == \
    (regname##_##fieldname##_##valname<<regname##_##fieldname##_SHIFT))

#define _F_VAR(regname,fieldname,value) \
    (((value)&GENMASK(regname##_##fieldname##_WIDTH - 1,0))<<regname##_##fieldname##_SHIFT)

#define _F_GET(regname,fieldname,value) \
    (((value)>>regname##_##fieldname##_SHIFT)&GENMASK(regname##_##fieldname##_WIDTH - 1,0))



#define EDMA_PARAM_SET_BASE		0x4000

#define EDMA_CHANNEL_MAX		64
#define EDMA_PARAM_SET_MAX		256

struct edma_param_set{
	uint32_t opt;
	uint32_t src;
	uint16_t bcnt;
	uint16_t acnt;
	uint32_t dst;
	uint16_t dstbidx;
	uint16_t srcbidx;
	uint16_t bcntrld;
	uint16_t link;
	uint16_t dstcidx;
	uint16_t srccidx;
	uint16_t reserved;
	uint16_t ccnt;
}__packed;



struct edma_desc{
	struct virt_dma_desc vdesc;
	int hw_prmset_index;
	int prmset_count;
	struct edma_param_set prmset[0];
};

struct edma_chan {
	struct virt_dma_chan vchan;
	struct edma_device *dmadev;
	struct dma_slave_config dma_sconfig;
	struct edma_desc *curr_desc;
	struct edma_param_set *curr_prmset;
	int chan_id;
};

struct edma_device {
	struct dma_device ddev;
	void __iomem *base;
	struct clk *fclk;
	struct edma_chan chans[EDMA_CHANNEL_MAX];
	spinlock_t lock;
	volatile uint64_t event_mask;
	DECLARE_BITMAP(prmset_bitmap,EDMA_PARAM_SET_MAX);
};




static inline struct edma_chan *to_edma_chan(struct virt_dma_chan *vchan)
{
	return container_of(vchan, struct edma_chan, vchan);
}

static inline struct edma_desc * to_edma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct edma_desc, vdesc);
}

static unsigned int edma_fifo_width(unsigned int bytes)
{
	switch(bytes){
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		return EDMA_PARAM_SET_OPT_FIFO_8;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return EDMA_PARAM_SET_OPT_FIFO_16;
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return EDMA_PARAM_SET_OPT_FIFO_32;
	case DMA_SLAVE_BUSWIDTH_16_BYTES:
		return EDMA_PARAM_SET_OPT_FIFO_64;
	case DMA_SLAVE_BUSWIDTH_32_BYTES:
		return EDMA_PARAM_SET_OPT_FIFO_128;
	case DMA_SLAVE_BUSWIDTH_64_BYTES:
		return EDMA_PARAM_SET_OPT_FIFO_256;
	}

	return EDMA_PARAM_SET_OPT_FIFO_8;
}

static void edma_prmset_write(struct edma_device *edma,int index,struct edma_param_set *ps)
{
	memcpy(edma->base + EDMA_PARAM_SET_BASE + index *sizeof(*ps),ps,sizeof(*ps));
}

static void edma_prmset_read(struct edma_device *edma,int index,struct edma_param_set *ps)
{
	memcpy(ps,edma->base + EDMA_PARAM_SET_BASE + index *sizeof(*ps),sizeof(*ps));
}

static void edma_write64(struct edma_device *edma,int offset,uint64_t value)
{
	writel(value&0xffffffff,edma->base + offset);
	writel((value>>32)&0xffffffff,edma->base + offset + 4);
}

static void edma_read64(struct edma_device *edma,int offset,uint64_t *value)
{
	*value = readl(edma->base + offset+4);
	*value <<=32;
	*value |= readl(edma->base + offset);
}

static void edma_write(struct edma_device *edma,int offset,uint32_t value)
{
	writel(value,edma->base + offset);
}

static void edma_read(struct edma_device *edma,int offset,uint32_t *value)
{
	*value = readl(edma->base + offset);
}




static void edma_chan_xfer_start(struct edma_chan *echan)
{
	struct edma_desc *edesc = to_edma_desc(vchan_next_desc(&echan->vchan));
	struct edma_device *dmadev = echan->dmadev;
	struct edma_param_set *prmset;
	int index;

	if(edesc == NULL){
		return ;
	}


	spin_lock(&dmadev->lock);
	index = find_first_zero_bit(dmadev->prmset_bitmap,EDMA_PARAM_SET_MAX);
	set_bit(dmadev->prmset_bitmap,index);
	spin_unlock(&dmadev->lock);

	edesc->hw_prmset_index = index;
	echan->curr_desc = edesc;

	prmset = &edesc->prmset[0];

	edma_prmset_write(dmadev,index,prmset);

	edma_write(dmadev,EDMA_CC_DCHMAP(echan->chan_id),
			_F_VAR(EDMA_CC_DCHMAP,PAENTRY,index));


}

static void edma_chan_xfer_complate(struct edma_chan *echan)
{
	struct edma_param_set prmset;
	struct edma_desc *edesc = echan->curr_desc;
	struct edma_device *dmadev = echan->dmadev;


	edma_prmset_read(dmadev,edesc->hw_prmset_index,&prmset);
	if(prmset.ccnt != 0){
		goto out;
	}


	edma_chan_xfer_start(echan);


out:
	vchan_cookie_complete(&echan->curr_desc->vdesc);
}




static struct dma_async_tx_descriptor *edma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct edma_chan *echan = to_edma_chan(to_virt_chan(chan));
	struct edma_desc *edesc;

	return vchan_tx_prep(&echan->vchan,&edesc->vdesc,flags);
}


static struct dma_async_tx_descriptor *edma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct edma_chan *echan = to_edma_chan(to_virt_chan(chan));
	struct edma_desc *edesc;
	struct edma_param_set *param_set;
	struct dma_slave_config *sconfig = &echan->dma_sconfig;
	int prmset_count = 1;

	edesc = kzalloc(sizeof(*edesc)+
			sizeof(*param_set) * prmset_count,GFP_KERNEL);

	edesc->prmset_count = prmset_count;

	param_set = &(edesc->prmset[0]);


	switch(sconfig->direction){
	case DMA_MEM_TO_DEV:
		param_set->src		= buf_addr;
		param_set->dst		= sconfig->dst_addr;

		param_set->acnt		= sconfig->dst_addr_width;
		param_set->bcnt		= period_len/sconfig->dst_addr_width;
		param_set->ccnt		= buf_len/period_len;

		param_set->srcbidx	= param_set->acnt;
		param_set->dstbidx	= 0;

		param_set->srccidx	= param_set->acnt * param_set->bcnt;
		param_set->dstcidx	= 0;

		param_set->opt		= edma_fifo_width(sconfig->dst_addr_width)|
						EDMA_PARAM_SET_OPT_SAM_INCR|EDMA_PARAM_SET_OPT_DAM_CONST|
						EDMA_PARAM_SET_OPT_AB_SYNC|EDMA_PARAM_SET_OPT_TCCMODE_NORMAL|
						EDMA_PARAM_SET_OPT_TCINTEN|EDMA_PARAM_SET_OPT_ITCINTEN|
						EDMA_PARAM_SET_OPT_TCC(echan->chan_id);
		break;
	case DMA_DEV_TO_MEM:
		param_set->src		= sconfig->src_addr;
		param_set->dst		= buf_addr;

		param_set->acnt		= sconfig->src_addr_width;
		param_set->bcnt		= period_len/sconfig->src_addr_width;
		param_set->ccnt		= buf_len/period_len;

		param_set->srcbidx	= 0;
		param_set->dstbidx	= param_set->acnt;

		param_set->srccidx	= 0;
		param_set->dstcidx	= param_set->acnt * param_set->bcnt;

		param_set->opt		= edma_fifo_width(sconfig->src_addr_width)|
						EDMA_PARAM_SET_OPT_SAM_CONST|EDMA_PARAM_SET_OPT_DAM_INCR|
						EDMA_PARAM_SET_OPT_AB_SYNC|EDMA_PARAM_SET_OPT_TCCMODE_NORMAL|
						EDMA_PARAM_SET_OPT_TCINTEN|EDMA_PARAM_SET_OPT_ITCINTEN|
						EDMA_PARAM_SET_OPT_TCC(echan->chan_id);
		break;
	case DMA_DEV_TO_DEV:
		break;
	case DMA_MEM_TO_MEM:
		break;
	default:
		break;
	}


	return vchan_tx_prep(&echan->vchan,&edesc->vdesc,flags);
}


static enum dma_status edma_tx_status(struct dma_chan *chan,
					    dma_cookie_t cookie,
					    struct dma_tx_state *txstate)
{
	return 0;
}

static void edma_issue_pending(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(to_virt_chan(chan));
	unsigned long flags;

	spin_lock_irqsave(&echan->vchan.lock, flags);
	if (vchan_issue_pending(&echan->vchan) && !echan->curr_desc){
		edma_chan_xfer_start(echan);
	}
	spin_unlock_irqrestore(&echan->vchan.lock, flags);
}

static int edma_chan_config(struct dma_chan *chan,
			     struct dma_slave_config *config)
{
	struct edma_chan *echan = to_edma_chan(to_virt_chan(chan));

	echan->dma_sconfig = *config;

	return 0;
}

static int edma_alloc_chan_resources(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(to_virt_chan(chan));
	struct edma_device *dmadev = echan->dmadev;
	uint64_t value;

	value = 1<<(uint64_t)echan->chan_id;

	spin_lock(&dmadev->lock);
	dmadev->event_mask |= value;
	spin_unlock(&dmadev->lock);

	edma_write64(dmadev,EDMA_CC_EESR,value);
	edma_write64(dmadev,EDMA_CC_IESR,value);

	return 0;
}

static void edma_free_chan_resources(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(to_virt_chan(chan));
	struct edma_device *dmadev = echan->dmadev;
	uint64_t value;

	value = 1<<(uint64_t)echan->chan_id;

	spin_lock(&dmadev->lock);
	dmadev->event_mask &= ~value;
	spin_unlock(&dmadev->lock);

	edma_write64(dmadev,EDMA_CC_EECR,value);
	edma_write64(dmadev,EDMA_CC_IECR,value);

}



static irqreturn_t edma_ccint_irq_handler(int irq, void *dev_id)
{
	struct edma_device *dmadev = dev_id;
	uint64_t value;

	edma_read64(dmadev,EDMA_CC_IPR,&value);

	if(value){
		edma_write64(dmadev,EDMA_CC_IECR,value);
		return IRQ_WAKE_THREAD;

	}

	return IRQ_NONE;
}


static irqreturn_t edma_ccint_irq_thread(int irq, void *dev_id)
{
	struct edma_device *dmadev = dev_id;
	uint64_t pending;
	unsigned int chan_id;

	edma_read64(dmadev,EDMA_CC_IPR,&pending);

	while(pending){

		chan_id = find_first_bit((unsigned long *)&pending,sizeof(pending));

		edma_chan_xfer_complate(&dmadev->chans[chan_id]);

		edma_write64(dmadev,EDMA_CC_ICR,1<<(uint64_t)chan_id);

		edma_read64(dmadev,EDMA_CC_IPR,&pending);
	}




	edma_write64(dmadev,EDMA_CC_IESR,dmadev->event_mask);
	return IRQ_HANDLED;
}


static irqreturn_t edma_mpint_irq_handler(int irq, void *dev_id)
{
	return IRQ_NONE;
}

static irqreturn_t edma_mpint_irq_thread(int irq, void *dev_id)
{
	return IRQ_NONE;
}


static irqreturn_t edma_errint_irq_handler(int irq, void *dev_id)
{
	return IRQ_NONE;
}

static irqreturn_t edma_errint_irq_thread(int irq, void *dev_id)
{
	return IRQ_NONE;
}

static struct dma_chan *edma_of_xlate(struct of_phandle_args *dma_spec,
                                           struct of_dma *ofdma)
{
	struct edma_device *dmadev = ofdma->of_dma_data;
	struct edma_chan *echan;
	unsigned int index ;
	int err;

	if(dma_spec->args_count < 1){
		return NULL;
	}

	index = dma_spec->args[0];

	if(index >= EDMA_CHANNEL_MAX){
		return NULL;
	}

	echan = &dmadev->chans[index];

	return dma_get_slave_channel(&echan->vchan.chan);
}

static void edma_desc_free(struct virt_dma_desc *vdesc)
{
	struct edma_desc *edesc = to_edma_desc(vdesc);

	kfree(edesc);
}

static void edma_chan_init(struct edma_device *edma,int chan_id)
{
	struct edma_chan *c;

	c = &edma->chans[chan_id];

	c->vchan.desc_free = edma_desc_free;
	c->chan_id = chan_id;
	c->dmadev = edma;

	vchan_init(&c->vchan,&edma->ddev);


}

static int edma_hw_init(struct edma_device *edma)
{
	return 0;
}

static int edma_init(struct edma_device *edma)
{
	struct dma_device *dd;
	int ret;
	int i;

	dd = &edma->ddev;

	dma_cap_set(DMA_PRIVATE,dd->cap_mask);
	dma_cap_set(DMA_SLAVE,dd->cap_mask);
	dma_cap_set(DMA_CYCLIC,dd->cap_mask);

	dd->device_alloc_chan_resources = edma_alloc_chan_resources;
	dd->device_free_chan_resources = edma_free_chan_resources;
	dd->device_prep_slave_sg = edma_prep_slave_sg;
	dd->device_prep_dma_cyclic = edma_prep_dma_cyclic;
	dd->device_config = edma_chan_config;
	dd->device_issue_pending = edma_issue_pending;
	dd->device_tx_status = edma_tx_status;

	dd->chancnt = EDMA_CHANNEL_MAX;

	for(i = 0;i < EDMA_CHANNEL_MAX;i++){
		edma_chan_init(edma,i);
	}

	ret = edma_hw_init(edma);
	if(ret)
		return ret;
	return 0;
}

static int am335x_dma_probe(struct platform_device *pdev)
{
	struct edma_device *edma;
	struct dma_device *dd;
	struct resource *res;
	int cc_irq,mp_irq,err_irq;
	int ret;

	edma = devm_kzalloc(&pdev->dev, sizeof(*edma), GFP_KERNEL);
	if(!edma){
		dev_err(&pdev->dev,"alloc memory for soc node failed");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	edma->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(edma->base)){
		dev_err(&pdev->dev,"failed to get io memory base");
		return PTR_ERR(edma->base);
	}

	cc_irq = platform_get_irq_byname(pdev, "ccint");
	if(cc_irq < 0){
		dev_err(&pdev->dev,"unable to get complate irq info");
		return cc_irq;
	}

	mp_irq = platform_get_irq_byname(pdev, "mpint");
	if(mp_irq < 0){
		dev_err(&pdev->dev,"unable to get memory protect irq info");
		return mp_irq;
	}

	err_irq = platform_get_irq_byname(pdev, "errint");
	if(err_irq < 0){
		dev_err(&pdev->dev,"unable to get error irq info");
		return err_irq;
	}


	ret = devm_request_threaded_irq(&pdev->dev,cc_irq,
			edma_ccint_irq_handler,edma_ccint_irq_thread,
			0,"am335x-dma-ccint",edma);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request dma cc irq");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev,mp_irq,
			edma_mpint_irq_handler,edma_mpint_irq_thread,
			0,"am335x-dma-mpint",edma);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request dma mp irq");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev,err_irq,
			edma_errint_irq_handler,edma_errint_irq_thread,
			0,"am335x-dma-errint",edma);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request dma err irq");
		return ret;
	}

	edma->fclk = devm_clk_get(&pdev->dev, "fclk");
	if (IS_ERR(edma->fclk)){
		dev_err(&pdev->dev,"failed to get clk of node");
		return PTR_ERR(edma->fclk);
	}

	clk_prepare_enable(edma->fclk);

	INIT_LIST_HEAD(&edma->ddev.channels);
	ret = edma_init(edma);
	if(ret){
		dev_err(&pdev->dev,"failed to init edma");
		clk_disable_unprepare(edma->fclk);
		return ret;
	}

	spin_lock_init(&edma->lock);

	edma->ddev.dev = &pdev->dev;

	platform_set_drvdata(pdev,edma);

	ret = of_dma_controller_register(pdev->dev.of_node,edma_of_xlate,edma);
	if(ret) {
		dev_err(&pdev->dev,"failed to register dma controller");
		clk_disable_unprepare(edma->fclk);
		return ret;
	}


	return ret;
}

static int am335x_dma_remove(struct platform_device *pdev)
{
	struct edma_device *edma = platform_get_drvdata(pdev);

	clk_disable_unprepare(edma->fclk);

	return 0;
}

static const struct of_device_id am335x_dma_of_match[] = {
	{ .compatible = "ti,am335x-edma",},
};

static struct platform_driver am335x_dma_driver = {
	.driver = {
		.name   = DRIVE_NAME,
		.of_match_table = of_match_ptr(am335x_dma_of_match),
	},
	.probe          = am335x_dma_probe,
	.remove         = am335x_dma_remove,
};
module_platform_driver(am335x_dma_driver);


MODULE_DEVICE_TABLE(of, am335x_dma_of_match);
MODULE_ALIAS("platform:" DRIVE_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("TI EDMA driver");
MODULE_LICENSE("GPL v2");





