/*
 * sdhc-at91sam9-mci.c
 *
 *  Created on: Nov 28, 2017
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>

#define DRIVER_NAME "at91sam9-mci"

#define HSMCI_CR	0x00
#define HSMCI_MR	0x04
#define HSMCI_DTOR	0x08
#define HSMCI_SDCR	0x0c
#define HSMCI_ARGR	0x10
#define HSMCI_CMDR	0x14
#define HSMCI_BLKR	0x18
#define HSMCI_CSTOR	0x1c
#define HSMCI_RSPR	0x20
#define HSMCI_RSPR2	0x24
#define HSMCI_RSPR3	0x28
#define HSMCI_RSPR4	0x2c
#define HSMCI_RDR	0x30
#define HSMCI_TDR	0x34
#define HSMCI_SR	0x40
#define HSMCI_IER	0x44
#define HSMCI_IDR	0x48
#define HSMCI_IMR	0x4c
#define HSMCI_DMA	0x50
#define HSMCI_CFG	0x54
#define HSMCI_WPMR	0xe4
#define HSMCI_WPSR	0xe8

/*
 * HSMCI Control Register
 */
#define HSMCI_CR_MCIEN		(1<<0)
#define HSMCI_CR_MCIDIS		(1<<1)
#define HSMCI_CR_PWSEN		(1<<2)
#define HSMCI_CR_PWSDIS		(1<<3)
#define HSMCI_CR_SWRST		(1<<7)

/*
 * HSMCI Mode Register
 */
#define HSMCI_MR_RDPROOF	(1<<11)
#define HSMCI_MR_WRPROOF	(1<<12)
#define HSMCI_MR_FBYTE		(1<<13)
#define HSMCI_MR_PADV		(1<<14)
#define HSMCI_MR_CLKODD		(1<<16)

/*
 * HSMCI Data Timeout Register
 */
#define HSMCI_DTOR_DTOCYC_SHIFT		0
#define HSMCI_DTOR_DTOCYC_WIDTH		4
#define HSMCI_DTOR_DTOMUL_SHIFT		4
#define HSMCI_DTOR_DTOMUL_WIDTH		3

/*
 * HSMCI SDCard/SDIO Register
 */
#define HSMIC_SDCR_SDCSEL_SHIFT		0
#define HSMIC_SDCR_SDCSEL_WIDTH		2
#define HSMCI_SDCR_SDCSEL_SLOTA		(0<<HSMIC_SDCR_SDCSEL_SHIFT)
#define HSMCI_SDCR_SDCSEL_SLOTB		(1<<HSMIC_SDCR_SDCSEL_SHIFT)
#define HSMCI_SDCR_SDCSEL_SLOTC		(2<<HSMIC_SDCR_SDCSEL_SHIFT)
#define HSMCI_SDCR_SDCSEL_SLOTD		(3<<HSMIC_SDCR_SDCSEL_SHIFT)
#define HSMIC_SDCR_SDCBUS_SHIFT		6
#define HSMIC_SDCR_SDCBUS_WIDTH		2
#define HSMCI_SDCR_SDCBUS_1BIT		(0<<HSMIC_SDCR_SDCBUS_SHIFT)
#define HSMCI_SDCR_SDCBUS_4BIT		(2<<HSMIC_SDCR_SDCBUS_SHIFT)
#define HSMCI_SDCR_SDCBUS_8BIT		(3<<HSMIC_SDCR_SDCBUS_SHIFT)


/*
 * HSMCI Command Register
 */
#define HSMCI_CMDR_CMDNB_SHIFT		0
#define HSMCI_CMDR_CMDNB_WIDTH		6
#define HSMCI_CMDR_CMDNB_MASK		((1<<HSMCI_CMDR_CMDNB_WIDTH)-1)
#define HSMCI_CMDR_RSPTYP_SHIFT		6
#define HSMCI_CMDR_RSPTYP_WIDTH		2
#define HSMCI_CMDR_RSPTYP_NORESP	0
#define HSMCI_CMDR_RSPTYP_48BIT		1
#define HSMCI_CMDR_RSPTYP_136BIT	2
#define HSMCI_CMDR_RSPTYP_R1B		3
#define HSMCI_CMDR_RSPTYP_MASK		((1<<HSMCI_CMDR_RSPTYP_WIDTH)-1)
#define HSMCI_CMDR_SPCMD_SHIFT		8
#define HSMCI_CMDR_SPCMD_WIDTH		3
#define HSMCI_CMDR_SPCMD_STD		0
#define HSMCI_CMDR_SPCMD_INIT		1
#define HSMCI_CMDR_SPCMD_SYNC		2
#define HSMCI_CMDR_SPCMD_CE_ATA		3
#define HSMCI_CMDR_SPCMD_IT_CMD		4
#define HSMCI_CMDR_SPCMD_IT_RESP	5
#define HSMCI_CMDR_SPCMD_BOR		6
#define HSMCI_CMDR_SPCMD_EBO		7
#define HSMCI_CMDR_SPCMD_MASK		((1<<HSMCI_CMDR_SPCMD_WIDTH)-1)
#define HSMCI_CMDR_OPDCMD_SHIFT		11
#define HSMCI_CMDR_OPDCMD_WIDTH		1
#define HSMCI_CMDR_OPDCMD_PUSHPULL	0
#define HSMCI_CMDR_OPDCMD_OPENDRAIN	1
#define HSMCI_CMDR_OPDCMD_MASK		((1<<HSMCI_CMDR_OPCMD_WIDTH)-1)
#define HSMCI_CMDR_MAXLAT_SHIFT		12
#define HSMCI_CMDR_MAXLAT_WIDTH		1
#define HSMCI_CMDR_MAXLAT_5CYCLE	0
#define HSMCI_CMDR_MAXLAT_64CYCLE	1
#define HSMCI_CMDR_TRCMD_SHIFT		16
#define HSMCI_CMDR_TRCMD_WIDTH		2
#define HSMCI_CMDR_TRCMD_NO_DATA	0
#define HSMCI_CMDR_TRCMD_START_DATA	1
#define HSMCI_CMDR_TRCMD_STOP_DATA	2
#define HSMCI_CMDR_TRDIR_SHIFT		18
#define HSMCI_CMDR_TRDIR_WIDTH		1
#define HSMCI_CMDR_TRDIR_WRITE		0
#define HSMCI_CMDR_TRDIR_READ		1
#define HSMCI_CMDR_TRTYP_SHIFT		19
#define HSMCI_CMDR_TRTYP_WIDTH		3
#define HSMCI_CMDR_TRTYP_SINGLE		0
#define HSMCI_CMDR_TRTYP_MULTIPLE	1
#define HSMCI_CMDR_TRTYP_STREAM		2
#define HSMCI_CMDR_TRTYP_BYTE		4
#define HSMCI_CMDR_TRTYP_BLOCK		5
#define HSMCI_CMDR_IOSPCMD_SHIFT	24
#define HSMCI_CMDR_IOSPCMD_WIDTH	3
#define HSMCI_CMDR_IOSPCMD_STD		0
#define HSMCI_CMDR_IOSPCMD_SUSPEND	1
#define HSMCI_CMDR_IOSPCMD_RESUME	2
#define HSMCI_CMDR_ATACS_SHIFT		26
#define HSMCI_CMDR_ATACS_WIDTH		1
#define HSMCI_CMDR_BOOT_ACK_SHIFT	27
#define HSMCI_CMDR_BOOT_ACK_WIDTH	1


/*
 * HSMCI Block Register
 */
#define HSMCI_BLKR_BCNT_SHIFT		0
#define HSMCI_BLKR_BCNT_WIDTH		16
#define HSMCI_BLKR_BLKLEN_SHIFT		16
#define HSMCI_BLKR_BLKLEN_WIDTH		16

/*
 * HSMCI Completion Signal Timeout Register
 */
#define HSMCI_CSTOR_CSTOCYC_SHIFT	0
#define HSMCI_CSTOR_CSTOCYC_WIDTH	4
#define HSMCI_CSTOR_CSTOMUL_SHIFT	4
#define HSMCI_CSTOR_CSTOMUL_WIDTH	3

/*
 * HSMCI Status Register
 */
#define HSMCI_EVENT_CMDRDY		(1<<0)
#define HSMCI_EVENT_RXRDY		(1<<1)
#define HSMCI_EVENT_TXRDY		(1<<2)
#define HSMCI_EVENT_BLKE		(1<<3)
#define HSMCI_EVENT_DTIP		(1<<4)
#define HSMCI_EVENT_NOTBUSY		(1<<5)
#define HSMCI_EVENT_SDIOIRQA		(1<<8)
#define HSMCI_EVENT_SDIOWAIT		(1<<12)
#define HSMCI_EVENT_CSRCV		(1<<13)
#define HSMCI_EVENT_RINDE		(1<<16)
#define HSMCI_EVENT_RDIRE		(1<<17)
#define HSMCI_EVENT_RCRCE		(1<<18)
#define HSMCI_EVENT_RENDE		(1<<19)
#define HSMCI_EVENT_RTOE		(1<<20)
#define HSMCI_EVENT_DCRCE		(1<<21)
#define HSMCI_EVENT_DTOE		(1<<22)
#define HSMCI_EVENT_CSTOE		(1<<23)
#define HSMCI_EVENT_BLKOVRE		(1<<24)
#define HSMCI_EVENT_DMADONE		(1<<25)
#define HSMCI_EVENT_FIFOEMPTY		(1<<26)
#define HSMCI_EVENT_XFRDONE		(1<<27)
#define HSMCI_EVENT_ACKRCV		(1<<28)
#define HSMCI_EVENT_ACKRCVE		(1<<29)
#define HSMCI_EVENT_OVRE		(1<<30)
#define HSMCI_EVENT_UNRE		(1<<31)


/*
 * HSMCI DMA Configuration Register
 */
#define HSMCI_DMA_OFFSET_SHIFT		0
#define HSMCI_DMA_OFFSET_WIDTH		2
#define HSMCI_DMA_CHKSIZE_SHIFT		4
#define HSMCI_DMA_CHKSIZE_WIDTH		2
#define HSMCI_DMA_DMAEN_SHIFT		8
#define HSMCI_DMA_DMAEN_WIDTH		1
#define HSMCI_DMA_ROPT_SHIFT		12
#define HSMCI_DMA_ROPT_WIDTH		1


/*
 * HSMCI Configuration Register
 */
#define HSMCI_CFG_FIFOMODE		(1<<0)
#define HSMCI_CFG_FERRCTRL		(1<<1)
#define HSMCI_CFG_HSMODE		(1<<8)
#define HSMCI_CFG_LSYNC			(1<<12)

/*
 * HSMCI Write Protection Mode Register
 */
#define HSMCI_WPMR_WPEN_SHIFT		0
#define HSMCI_WPMR_WPEN_WIDTH		1
#define HSMCI_WPMR_WPKEY_SHIFT		8
#define HSMCI_WPMR_WPKEY_WIDTH		24

/*
 * HSMCI Write Protection Status Register
 */
#define HSMCI_WPSR_WPVS_SHIFT		0
#define HSMCI_WPSR_WPVS_WIDTH		1
#define HSMCI_WPSR_WPVSRC_SHIFT		8
#define HSMCI_WPSR_WPVSRC_WIDTH		16

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


struct mci_mmc_host{
	void __iomem *		base;
	unsigned long 		mapbase;
	struct mmc_host *	mmc;
	struct mmc_request *	mrq;
	struct mmc_command *	cmd;
	struct mmc_data	*	data;
	struct dma_chan *	dma_chan;
	struct dma_async_tx_descriptor *dma_desc;
	uint32_t 		status;
	uint32_t 		event_mask;
	uint32_t		pio_size;
	void *			pio_ptr;
};

static void mci_mmc_start_command(struct mci_mmc_host *host ,struct mmc_command *cmd);

static void mci_dma_complate(void *dma_async_param)
{
	struct mci_mmc_host *host = (struct mci_mmc_host *)dma_async_param;
	struct mmc_data * data = host->data;

	data->bytes_xfered = data->blksz * data->blocks;


	dma_unmap_sg(host->dma_chan->device->dev,
			data->sg,data->sg_len,((data->flags & MMC_DATA_WRITE)
					 ? DMA_TO_DEVICE : DMA_FROM_DEVICE));

	if(!data->stop){
		mmc_request_done(host->mmc, host->mrq);
	} else {
		mci_mmc_start_command(host,data->stop);
	}


}
#if defined(MCI_NO_DMA)
static void mci_mmc_prepare_data(struct mci_mmc_host *host,struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	if(!data){
		return ;
	}

	host->data = data;
	host->pio_size = data->blksz * data->blocks;
	host->pio_ptr = sg_virt(data->sg);

}
#else

static void mci_mmc_prepare_data(struct mci_mmc_host *host,struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	struct dma_chan *chan = host->dma_chan;
	enum dma_data_direction direction;
	enum dma_transfer_direction slave_dir;
	int sglen;

	if(!data){
		return ;
	}

	if(data->flags & MMC_DATA_READ){
		direction = DMA_FROM_DEVICE;
		slave_dir = DMA_DEV_TO_MEM;
	} else {
		direction = DMA_TO_DEVICE;
		slave_dir = DMA_MEM_TO_DEV;
	}

	sglen = dma_map_sg(chan->device->dev,data->sg,
			data->sg_len,direction);

	host->data = data;
	host->dma_desc = dmaengine_prep_slave_sg(host->dma_chan,data->sg,
			sglen,slave_dir,DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	host->dma_desc->callback = mci_dma_complate;
	host->dma_desc->callback_param = host;


}
#endif

static void mci_mmc_start_command(struct mci_mmc_host *host ,struct mmc_command *cmd)
{
	uint32_t cmd_reg = 0;
	uint32_t resptype = 0;
	struct mmc_data * data = cmd->data;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		resptype = HSMCI_CMDR_RSPTYP_NORESP;
		break;
	case MMC_RSP_R1:
	case MMC_RSP_R3:
		resptype = HSMCI_CMDR_RSPTYP_48BIT;
		break;
	case MMC_RSP_R2:
		resptype = HSMCI_CMDR_RSPTYP_136BIT;
		break;
	case MMC_RSP_R1B:
		resptype = HSMCI_CMDR_RSPTYP_R1B;
		break;
	}


	cmd_reg = __FIELD_VALUE_SET(HSMCI_CMDR,CMDNB,cmd->opcode)|
			__FIELD_VALUE_SET(HSMCI_CMDR,RSPTYP,resptype);

	host->mrq = cmd->mrq;
	host->cmd = cmd;

	if(data){
		writel((data->blksz<<HSMCI_BLKR_BLKLEN_SHIFT)|
				(data->blocks<<HSMCI_BLKR_BCNT_SHIFT),
				host->base + HSMCI_BLKR);


		cmd_reg |= (data->flags & MMC_DATA_READ)?
				__FIELD_VALUE_FIXED(HSMCI_CMDR,TRDIR,READ):
				__FIELD_VALUE_FIXED(HSMCI_CMDR,TRDIR,WRITE);
		cmd_reg |= __FIELD_VALUE_FIXED(HSMCI_CMDR,TRTYP,MULTIPLE)|
				__FIELD_VALUE_FIXED(HSMCI_CMDR,TRCMD,START_DATA);
	}

	dev_dbg(mmc_dev(host->mmc), "cmd %d arg %x  len %d\n",
			cmd->opcode, cmd->arg,data ? data->blksz * data->blocks : 0);

	writel(cmd->arg,host->base + HSMCI_ARGR);
	writel(cmd_reg,host->base + HSMCI_CMDR);

	writel(HSMCI_EVENT_CMDRDY,host->base + HSMCI_IER);

}

static void mci_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mci_mmc_host *host = mmc_priv(mmc);


	mci_mmc_prepare_data(host,mrq);
	mci_mmc_start_command(host,mrq->cmd);
}

static void mci_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{

}

static void mci_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{

}

#if defined(MCI_NO_DMA)
static void mci_mmc_command_done(struct mci_mmc_host *host ,struct mmc_command *cmd)
{
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[0] = readl(host->base + HSMCI_RSPR);
			cmd->resp[1] = readl(host->base + HSMCI_RSPR + 0x04);
			cmd->resp[2] = readl(host->base + HSMCI_RSPR + 0x08);
			cmd->resp[3] = readl(host->base + HSMCI_RSPR + 0x0c);
		} else {
			cmd->resp[0] = readl(host->base + HSMCI_RSPR);
		}

	}

	host->cmd = NULL;
	if (cmd->data == NULL || cmd->error) {
		mmc_request_done(host->mmc, host->mrq);
	}

}
#else

static void mci_mmc_command_done(struct mci_mmc_host *host ,struct mmc_command *cmd)
{
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[0] = readl(host->base + HSMCI_RSPR);
			cmd->resp[1] = readl(host->base + HSMCI_RSPR + 0x04);
			cmd->resp[2] = readl(host->base + HSMCI_RSPR + 0x08);
			cmd->resp[3] = readl(host->base + HSMCI_RSPR + 0x0c);
		} else {
			cmd->resp[0] = readl(host->base + HSMCI_RSPR);
		}

	}

	host->cmd = NULL;
	if (cmd->data == NULL || cmd->error) {
		mmc_request_done(host->mmc, host->mrq);
	} else if (cmd->data) {
		dmaengine_submit(host->dma_desc);
		dma_async_issue_pending(host->dma_chan);
	}

}
#endif

static void mci_mmc_receive_data(struct mci_mmc_host *host)
{
	struct mmc_data *data = host->data;
	uint32_t status;
	uint32_t value;
	int len;


	status = readl(host->base + HSMCI_SR);
	while((status & HSMCI_EVENT_RXRDY) && host->pio_size != 0){
		len = 4;

		if(host->pio_size < len){
			len = host->pio_size;
		}
		value = readl(host->base + HSMCI_RDR);

		while(len--){
			*(uint8_t*)host->pio_ptr = value &0xff;
			value >>= 8;
			host->pio_ptr++;
			host->pio_size--;
		}

		status = readl(host->base + HSMCI_SR);
	}

	data->bytes_xfered = data->blksz * data->blocks - host->pio_size;

	if(host->pio_size == 0){

		if(!data->stop){
			mmc_request_done(host->mmc, host->mrq);
		} else {
			mci_mmc_start_command(host,data->stop);
		}
	}

}

static void mci_mmc_send_data(struct mci_mmc_host *host)
{
	struct mmc_data *data = host->data;
	uint32_t status;
	uint32_t value;
	int len;
	int i = 0;

	status = readl(host->base + HSMCI_SR);
	while((status & HSMCI_EVENT_TXRDY) && host->pio_size != 0){
		len = 4;

		if(host->pio_size < len){
			len = host->pio_size;
		}

		value = 0;
		i = 0;
		while(i < len){
			value |= (*(uint8_t*)host->pio_ptr)<<i*8;
			host->pio_ptr++;
			host->pio_size--;
			i++;
		}

		writel(value,host->base + HSMCI_TDR);


		status = readl(host->base + HSMCI_SR);
	}

	data->bytes_xfered = data->blksz * data->blocks - host->pio_size;

	if(host->pio_size == 0){
		if(!data->stop){
			mmc_request_done(host->mmc, host->mrq);
		} else {
			mci_mmc_start_command(host,data->stop);
		}
	}

}


static irqreturn_t mci_mmc_irq_thread(int irq, void *dev)
{
	struct mci_mmc_host *host  = dev;
	uint32_t status;

	status = readl(host->base + HSMCI_SR);

	if(!status){
		return IRQ_NONE;
	}

	if((status & HSMCI_EVENT_CMDRDY) && host->cmd){
		mci_mmc_command_done(host,host->cmd);
		writel(HSMCI_EVENT_CMDRDY,host->base + HSMCI_IER);
	}


	if((status & HSMCI_EVENT_RXRDY) && host->pio_size){
		mci_mmc_receive_data(host);
		writel(HSMCI_EVENT_RXRDY,host->base + HSMCI_IER);
	}

	if((status & HSMCI_EVENT_TXRDY) && host->pio_size){
		mci_mmc_send_data(host);
		writel(HSMCI_EVENT_TXRDY,host->base + HSMCI_IER);
	}


	return IRQ_HANDLED;
}

static irqreturn_t mci_mmc_irq(int irq, void *dev)
{
	struct mci_mmc_host *host  = dev;
	uint32_t status;

	status = readl(host->base + HSMCI_SR);
	status &= host->event_mask;

	if(status){
		writel_relaxed(status,host->base + HSMCI_IDR);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static struct mmc_host_ops mci_mmc_ops = {
	.request	= mci_mmc_request,
	.set_ios	= mci_mmc_set_ios,
	.get_ro		= mmc_gpio_get_ro,
	.get_cd		= mmc_gpio_get_cd,
	.enable_sdio_irq = mci_mmc_enable_sdio_irq,
};



static void mci_hw_init(struct mci_mmc_host *host)
{
#if defined(MCI_NO_DMA)
	host->event_mask = HSMCI_EVENT_CMDRDY|HSMCI_EVENT_RXRDY|
			HSMCI_EVENT_TXRDY;
#else
	host->event_mask = HSMCI_EVENT_CMDRDY;
#endif
	writel(0xffffffff,host->base + HSMCI_IDR);
	writel(host->event_mask,host->base + HSMCI_IER);
}

static int at91sam9_mci_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct mci_mmc_host *host;
	struct dma_slave_config dma_config;
	struct resource *res;
	int ret;
	int irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0)
		return -ENXIO;

	mmc = mmc_alloc_host(sizeof(struct mci_mmc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;


	host->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		goto failed_ioremap;
	}

	host->mapbase = res->start;

	host->dma_chan = dma_request_chan(&pdev->dev,"rxtx");
	if(IS_ERR(host->dma_chan)){
		ret = PTR_ERR(host->dma_chan);
		goto failed_request_dma;
	}

	dma_config.src_addr = host->mapbase + HSMCI_RDR;
	dma_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_config.src_maxburst = 1;
	dma_config.dst_addr = host->mapbase + HSMCI_TDR;
	dma_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_config.dst_maxburst = 1;

	ret = dmaengine_slave_config(host->dma_chan,&dma_config);
	if(ret)
		goto failed_config_dma;


	ret = devm_request_threaded_irq(&pdev->dev, irq, mci_mmc_irq,
			mci_mmc_irq_thread, 0, DRIVER_NAME, host);
	if(ret)
		goto failed_request_irq;

	mmc->max_blk_count = 511;
	mmc->max_blk_size = 65535;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	ret = mmc_of_parse(mmc);
	if(ret)
		goto failed_mmc_parse;

	mmc->ops = &mci_mmc_ops;

	platform_set_drvdata(pdev, mmc);

	mci_hw_init(host);

	ret = mmc_add_host(mmc);
	if(ret)
		goto failed_add_host;

	return 0;

failed_add_host:
failed_mmc_parse:
failed_config_dma:
failed_request_irq:
	dma_release_channel(host->dma_chan);
failed_request_dma:
failed_ioremap:
	mmc_free_host(mmc);
	return ret;
}

static int at91sam9_mci_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);

	mmc_remove_host(mmc);

	mmc_free_host(mmc);
	return 0;
}

static const struct of_device_id at91sam9_mci_of_match[] = {
	{ .compatible = "atmel,at91sam9-hsmci",},
};

static struct platform_driver at91sam9_mci_driver = {
	.driver = {
		.name   = DRIVER_NAME,
		.of_match_table = of_match_ptr(at91sam9_mci_of_match),
	},
	.probe          = at91sam9_mci_probe,
	.remove         = at91sam9_mci_remove,
};
module_platform_driver(at91sam9_mci_driver);


MODULE_DEVICE_TABLE(of, at91sam9_mci_of_match);
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("Atmel HSMCI driver");
MODULE_LICENSE("GPL v2");
