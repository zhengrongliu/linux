/*
 * snd-am335x-mcasp.c
 *
 *  Created on: Mar 27, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#define DRIVE_NAME "am335x-mcasp"
/* global reg */
#define MCASP_REV		0x0
#define MCASP_SYSCONFIG		0x4
#define MCASP_PFUNC		0x10
#define MCASP_PDIR		0x14
#define MCASP_PDOUT		0x18
#define MCASP_PDIN		0x1c
#define MCASP_PDCLR		0x20
#define MCASP_GBLCTL		0x44
#define MCASP_AMUTE		0x48
#define MCASP_DLBCTL		0x4c
#define MCASP_DITCTL		0x50
/* receive reg */
#define MCASP_RGBLCTL		0x60
#define MCASP_RMASK		0x64
#define MCASP_RFMT		0x68
#define MCASP_AFSRCTL		0x6c
#define MCASP_ACLKRCTL		0x70
#define MCASP_AHCLKRCTL		0x74
#define MCASP_RTDM		0x78
#define MCASP_PINTCTL		0x7c
#define MCASP_RSTAT		0x80
#define MCASP_RSLOT		0x84
#define MCASP_RCLKCHK		0x88
#define MCASP_REVTCTL		0x8c
/* xfer reg */
#define MCASP_XGBLCTL		0xa0
#define MCASP_XMASK		0xa4
#define MCASP_XFMT		0xa8
#define MCASP_AFSXCTL		0xac
#define MCASP_ACLKXCTL		0xb0
#define MCASP_AHCLKXCTL		0xb4
#define MCASP_XTDM		0xb8
#define MCASP_XINTCTL		0xbc
#define MCASP_XSTAT		0xc0
#define MCASP_XSLOT		0xc4
#define MCASP_XCLKCHK		0xc8
#define MCASP_XEVTCTL		0xcc

#define MCASP_DITCSRA(x)	(0x100+(x)*0x4)
#define MCASP_DITCSRB(x)	(0x118+(x)*0x4)
#define MCASP_DITUDRA(x)	(0x130+(x)*0x4)
#define MCASP_DITUDRB(x)	(0x148+(x)*0x4)
#define MCASP_SRCTL(x)		(0x180+(x)*0x4)
#define MCASP_XBUF(x)		(0x200+(x)*0x4)
#define MCASP_RBUF(x)		(0x280+(x)*0x4)

#define MCASP_WFIFOCTL		0x1000
#define MCASP_WFIFOSTS		0x1004
#define MCASP_RFIFOCTL		0x1008
#define MCASP_RFIFOSTS		0x100c

/*
 * bit stream format register
 */
#define MCASP_FMT_ROT_0		(0<<0)
#define MCASP_FMT_ROT_4		(1<<0)
#define MCASP_FMT_ROT_8		(2<<0)
#define MCASP_FMT_ROT_12	(3<<0)
#define MCASP_FMT_ROT_16	(4<<0)
#define MCASP_FMT_ROT_20	(5<<0)
#define MCASP_FMT_ROT_24	(6<<0)
#define MCASP_FMT_ROT_28	(7<<0)

#define MCASP_FMT_BUSEL_DATA	(0<<3)
#define MCASP_FMT_BUSEL_CFG	(1<<3)

#define MCASP_FMT_SSZ_8		(3<<4)
#define MCASP_FMT_SSZ_12	(5<<4)
#define MCASP_FMT_SSZ_16	(7<<4)
#define MCASP_FMT_SSZ_20	(9<<4)
#define MCASP_FMT_SSZ_24	(0xb<<4)
#define MCASP_FMT_SSZ_32	(0xf<<4)

#define MCASP_FMT_PBIT(x)	((x)<<8)

#define MCASP_FMT_PAD_0		(0<<13)
#define MCASP_FMT_PAD_1		(1<<13)
#define MCASP_FMT_PAD_2		(2<<13)
#define MCASP_FMT_RVRS_LSB	(0<<15)
#define MCASP_FMT_RVRS_MSB	(1<<15)

#define MCASP_FMT_DATDLY_0	(0<<16)
#define MCASP_FMT_DATDLY_1	(1<<16)
#define MCASP_FMT_DATDLY_2	(2<<16)

/*
 * frame control register
 */
#define MCASP_AFSCTL_FSP_RISING		(0<<0)
#define MCASP_AFSCTL_FSP_FALLING	(1<<0)

#define MCASP_AFSCTL_FSM_EXTERNAL	(0<<1)
#define MCASP_AFSCTL_FSM_INTERNAL	(1<<1)

#define MCASP_AFSCTL_FWID_BIT		(0<<4)
#define MCASP_AFSCTL_FWID_WORD		(1<<4)

#define MCASP_AFSCTL_XMOD_BUST		(0<<7)
#define MCASP_AFSCTL_XMOD_TDM(x)	((x)<<7)
#define MCASP_AFSCTL_XMOD_384_SLOT	(0x180<<7)

/*
 * clock control register
 */
#define MCASP_ACLKCTL_CLKDIV(x)		(((x)-1)<<0)
#define MCASP_ACLKCTL_CLKM_EXTERNAL	(0<<5)
#define MCASP_ACLKCTL_CLKM_INTERNAL	(1<<5)
#define MCASP_ACLKCTL_RXCLK_SYNC	(0<<6)
#define MCASP_ACLKCTL_RXCLK_ASYNC	(1<<6)
#define MCASP_ACLKCTL_CLKP_RISING	(0<<7)
#define MCASP_ACLKCTL_CLKP_FALLING	(1<<7)

/*
 * hight-frequency master clock register
 */
#define MCASP_AHCLKCTL_HCLKDIV(x)	((x)<<0)
#define MCASP_AHCLKCTL_HCLKP_NORMAL	(0<<14)
#define MCASP_AHCLKCTL_HCLKP_INVERT	(1<<14)
#define MCASP_AHCLKCTL_HCLKM_EXTERNAL	(0<<15)
#define MCASP_AHCLKCTL_HCLKM_INTERNAL	(1<<15)

/*
 * interrupt ctrl register
 */
#define MCASP_INTCTL_UNDRN		(1<<0)
#define MCASP_INTCTL_SYNCERR		(1<<1)
#define MCASP_INTCTL_CKFAIL		(1<<2)
#define MCASP_INTCTL_DMAERR		(1<<3)
#define MCASP_INTCTL_LAST		(1<<4)
#define MCASP_INTCTL_DATA		(1<<5)
#define MCASP_INTCTL_STAFRM		(1<<7)

/*
 * interrupt status register
 */
#define MCASP_INTSTATE_UNDRN		(1<<0)
#define MCASP_INTSTATE_SYNCERR		(1<<1)
#define MCASP_INTSTATE_CKFAIL		(1<<2)
#define MCASP_INTSTATE_TDMSLOT		(1<<3)
#define MCASP_INTSTATE_LAST		(1<<4)
#define MCASP_INTSTATE_DATA		(1<<5)
#define MCASP_INTSTATE_STAFRM		(1<<6)
#define MCASP_INTSTATE_DMAERR		(1<<7)
#define MCASP_INTSTATE_XERR		(1<<8)

struct am335x_mcasp_device{
	struct clk *fclk;
	struct clk *extclk;
	void __iomem *base;
	unsigned int daifmt;
};

struct am335x_mcasp_dai{

};



static struct snd_pcm_hardware am335x_mcasp_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED,
	.periods_max		= 2,
	.periods_min		= 2,
	.period_bytes_min	= 3 * 4,
	.period_bytes_max	= 64 * 4,
	.buffer_bytes_max	= 128 * 4,
};

static int am335x_mcasp_dai_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai * dai)
{
	return 0;
}

static int am335x_mcasp_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai * dai)
{
	struct am335x_mcasp_device *mdai = snd_soc_dai_get_drvdata(dai);
	int slot;
	uint32_t fmt = 0;
	uint32_t mask = 0;
	uint32_t afsctl = 0;
	uint32_t aclkctl = 0;
	uint32_t ahclkctl = 0;
	unsigned long clk_rate;
	unsigned long bclk ;
	unsigned long div;



	switch (params_width(params)) {
	case 8:
		fmt |= MCASP_FMT_SSZ_8;
		mask = 0xff;
		break;
	case 16:
		fmt |= MCASP_FMT_SSZ_16;
		mask = 0xffff;
		break;
	case 24:
		fmt |= MCASP_FMT_SSZ_24;
		mask = 0xffffff;
		break;
	case 32:
		fmt |= MCASP_FMT_SSZ_32;
		mask = 0xffffffff;
		break;
	}

	afsctl |= MCASP_AFSCTL_XMOD_TDM(params_channels(params));

	switch(mdai->daifmt&SND_SOC_DAIFMT_FORMAT_MASK){
	case SND_SOC_DAIFMT_CBM_CFM:
		afsctl |= MCASP_AFSCTL_FSM_INTERNAL;
		aclkctl |= MCASP_ACLKCTL_CLKM_INTERNAL;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		afsctl |= MCASP_AFSCTL_FSM_INTERNAL;
		aclkctl |= MCASP_ACLKCTL_CLKM_EXTERNAL;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		afsctl |= MCASP_AFSCTL_FSM_EXTERNAL;
		aclkctl |= MCASP_ACLKCTL_CLKM_INTERNAL;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		afsctl |= MCASP_AFSCTL_FSM_EXTERNAL;
		aclkctl |= MCASP_ACLKCTL_CLKM_EXTERNAL;
		break;
	}

	switch(mdai->daifmt&SND_SOC_DAIFMT_INV_MASK){
	case SND_SOC_DAIFMT_NB_NF:
		afsctl |= MCASP_AFSCTL_FSP_RISING;
		aclkctl |= MCASP_ACLKCTL_CLKP_RISING;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		afsctl |= MCASP_AFSCTL_FSP_RISING;
		aclkctl |= MCASP_ACLKCTL_CLKP_FALLING;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		afsctl |= MCASP_AFSCTL_FSP_FALLING;
		aclkctl |= MCASP_ACLKCTL_CLKP_RISING;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		afsctl |= MCASP_AFSCTL_FSP_FALLING;
		aclkctl |= MCASP_ACLKCTL_CLKP_FALLING;
		break;

	}

	clk_rate = clk_get_rate(mdai->fclk);

	bclk = params_rate(params);

	div = bclk/clk_rate;

	aclkctl |= MCASP_ACLKCTL_CLKDIV(div);

	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		writel(aclkctl,mdai->base + MCASP_ACLKXCTL);
		writel(afsctl,mdai->base + MCASP_AFSXCTL);
		writel(fmt,mdai->base + MCASP_AHCLKXCTL);
		writel(mask,mdai->base + MCASP_XMASK);
	} else {
		writel(aclkctl,mdai->base + MCASP_ACLKRCTL);
		writel(afsctl,mdai->base + MCASP_AFSRCTL);
		writel(fmt,mdai->base + MCASP_AHCLKRCTL);
		writel(mask,mdai->base + MCASP_RMASK);
	}



	return 0;
}

static int am335x_mcasp_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct am335x_mcasp_device *mdev = snd_soc_dai_get_drvdata(dai);

	mdev->daifmt = fmt;

	return 0;
}



static int am335x_mcasp_dai_xlate_tdm_slot_mask(unsigned int slots,
		unsigned int *tx_mask, unsigned int *rx_mask)
{
	return 0;
}

static int am335x_mcasp_dai_set_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	return 0;
}

static int am335x_mcasp_dai_probe(struct snd_soc_dai *dai)
{
	return 0;
}

static int am335x_mcasp_dai_remove(struct snd_soc_dai *dai)
{
	return 0;
}
static const struct snd_soc_dai_ops am335x_mcasp_dai_ops = {
	.hw_params	= am335x_mcasp_dai_hw_params,
	.trigger	= am335x_mcasp_dai_trigger,
	.set_fmt	= am335x_mcasp_dai_set_fmt,
	.set_tdm_slot	= am335x_mcasp_dai_set_tdm_slot,
	.xlate_tdm_slot_mask	= am335x_mcasp_dai_xlate_tdm_slot_mask,
};
static const struct snd_soc_component_driver am335x_mcasp_component = {
	.name	= "am335x-mcasp",
};
static struct snd_soc_dai_driver am335x_mcasp_dai[] = {
	{
		.name	= "am335x-mcasp",
		.probe	= am335x_mcasp_dai_probe,
		.remove	= am335x_mcasp_dai_remove,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops	= &am335x_mcasp_dai_ops,
	}
};

static int am335x_mcasp_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	ret = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);

	return snd_soc_set_runtime_hwparams(substream,&am335x_mcasp_pcm_hardware);
}


static int am335x_mcasp_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}



static int am335x_mcasp_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	return 0;
}

static snd_pcm_uframes_t am335x_mcasp_pcm_pointer(struct snd_pcm_substream *substream)
{
	return 0;
}


static const struct snd_pcm_ops am335x_mcasp_pcm_ops = {
	.open		= am335x_mcasp_pcm_open,
	.hw_params	= am335x_mcasp_pcm_hw_params,
	.hw_free	= snd_pcm_lib_free_pages,
	.trigger	= am335x_mcasp_pcm_trigger,
	.pointer	= am335x_mcasp_pcm_pointer,
};
#if 0
static int am335x_mcasp_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm *pcm = rtd->pcm;
	size_t prealloc_buffer_size = 512 * 1024;
	size_t max_buffer_size = SIZE_MAX;
	int i;
	int ret;


	for (i = SNDRV_PCM_STREAM_PLAYBACK; i <= SNDRV_PCM_STREAM_CAPTURE; i++) {
		substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;
		ret = snd_pcm_lib_preallocate_pages(substream,
				SNDRV_DMA_TYPE_CONTINUOUS,
				pcm->card->dev,
				prealloc_buffer_size,
				max_buffer_size);
		if (ret){
			dev_err(rtd->platform->dev,"preallocate pcm buffer failed\n");
			return ret;
		}

	}
	return 0;
}

static void am335x_mcasp_pcm_free(struct snd_pcm *pcm)
{

}
static struct snd_soc_platform_driver am335x_mcasp_plat = {
	.ops		= &am335x_mcasp_pcm_ops,
	.pcm_new	= am335x_mcasp_pcm_new,
	.pcm_free	= am335x_mcasp_pcm_free,
};
#endif
static irqreturn_t am335x_mcasp_tx_irq_handler(int irq, void *dev_id)
{
	struct am335x_mcasp_device *mdev = dev_id;
	uint32_t status;

	status = readl(mdev->base + MCASP_XSTAT);

	if(status & MCASP_INTSTATE_DATA){

	}

	return IRQ_HANDLED;
}

static irqreturn_t am335x_mcasp_tx_irq_thread(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static irqreturn_t am335x_mcasp_rx_irq_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static irqreturn_t am335x_mcasp_rx_irq_thread(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static int am335x_mcasp_parse_of(struct am335x_mcasp_device *mdev)
{
	return 0;
}

static void am335x_mcasp_hw_init(struct am335x_mcasp_device *mdev)
{
	writel(0,mdev->base + MCASP_GBLCTL);
}

static int am335x_mcasp_init(struct am335x_mcasp_device *mdev)
{
	int ret;

	ret = am335x_mcasp_parse_of(mdev);
	if(ret)
		return ret;

	am335x_mcasp_hw_init(mdev);

	return 0;
}

static int am335x_mcasp_probe(struct platform_device *pdev)
{
	struct am335x_mcasp_device *mdev;
	struct resource *res;
	int ret;
	int tx_irq,rx_irq;


	mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
	if(!mdev){
		dev_err(&pdev->dev,"alloc memory for soc node failed");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mdev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mdev->base)){
		dev_err(&pdev->dev,"failed to get io memory base");
		return PTR_ERR(mdev->base);
	}

	tx_irq = platform_get_irq_byname(pdev, "tx");
	if(tx_irq < 0){
		dev_err(&pdev->dev,"unable to get tx irq info");
		return tx_irq;
	}

	rx_irq = platform_get_irq_byname(pdev, "rx");
	if(rx_irq < 0){
		dev_err(&pdev->dev,"unable to get rx irq info");
		return rx_irq;
	}


	ret = devm_request_threaded_irq(&pdev->dev,tx_irq,
			am335x_mcasp_tx_irq_handler,am335x_mcasp_tx_irq_thread,
			0,"am335x-mcasp-tx",mdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request mcasp tx irq");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev,rx_irq,
			am335x_mcasp_rx_irq_handler,am335x_mcasp_rx_irq_thread,
			0,"am335x-mcasp-rx",mdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request mcasp rx irq");
		return ret;
	}

	mdev->fclk = devm_clk_get(&pdev->dev, "fclk");
	if (IS_ERR(mdev->fclk)){
		dev_err(&pdev->dev,"failed to get clk of node");
		return PTR_ERR(mdev->fclk);
	}


	ret = am335x_mcasp_init(mdev);
	if(ret){
		dev_err(&pdev->dev,"failed to init mcasp");
		return ret;
	}

	platform_set_drvdata(pdev, mdev);

	ret = devm_snd_soc_register_component(&pdev->dev,
					&am335x_mcasp_component,
					am335x_mcasp_dai, ARRAY_SIZE(am335x_mcasp_dai));
	if(ret){
		dev_err(&pdev->dev,"failed to register sound component with err code %d",-ret);
		clk_disable_unprepare(mdev->fclk);
		return ret;
	}


	ret = devm_snd_dmaengine_pcm_register(&pdev->dev,NULL, 0);

	if(ret){
		dev_err(&pdev->dev,"failed to register sound dma with err code %d",-ret);
		clk_disable_unprepare(mdev->fclk);
		return ret;
	}

#if 0
	ret = devm_snd_soc_register_platform(&pdev->dev,
					      &am335x_mcasp_plat);
	if(ret){
		dev_err(&pdev->dev,"failed to register sound platform with err code %d",-ret);
		clk_disable_unprepare(mdev->fclk);
		return ret;
	}
#endif
	return 0;
}

static int am335x_mcasp_remove(struct platform_device *pdev)
{
	struct am335x_mcasp_device *mdev = platform_get_drvdata(pdev);

	clk_disable_unprepare(mdev->fclk);

	return 0;
}

static const struct of_device_id am335x_mcasp_of_match[] = {
	{ .compatible = "ti,am335x-mcasp", },
	{}
};

static struct platform_driver am335x_mcasp_driver = {
	.driver = {
		.name = DRIVE_NAME,
		.of_match_table = am335x_mcasp_of_match,
	},
	.probe = am335x_mcasp_probe,
	.remove = am335x_mcasp_remove,
};
module_platform_driver(am335x_mcasp_driver);


MODULE_DEVICE_TABLE(of, am335x_mcasp_of_match);
MODULE_ALIAS("platform:" DRIVE_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("TI AM335X MCASP driver");
MODULE_LICENSE("GPL v2");

