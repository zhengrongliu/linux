/*
 * eth-at91sam9-emac.c
 *
 *  Created on: Jan 10, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/etherdevice.h>

#define DRIVER_NAME "at91sam9-emac"


#define EMAC_NCR	0x0
#define EMAC_NCFGR	0x4
#define EMAC_NSR	0x8
#define EMAC_TSR	0x14
#define EMAC_RBQP	0x18
#define EMAC_TBQP	0x1c
#define EMAC_RSR	0x20
#define EMAC_ISR	0x24
#define EMAC_IER	0x28
#define EMAC_IDR	0x2c
#define EMAC_IMR	0x30
#define EMAC_MAN	0x34
#define EMAC_PTR	0x38
#define EMAC_PFR	0x3c
#define EMAC_FTO	0x40
#define EMAC_SCF	0x44
#define EMAC_MCF	0x48
#define EMAC_FRO	0x4c
#define EMAC_FCSE	0x50
#define EMAC_ALE	0x54
#define EMAC_DTF	0x58
#define EMAC_LCOL	0x5c
#define EMAC_ECOL	0x60
#define EMAC_TUND	0x64
#define EMAC_CSE	0x68
#define EMAC_RRE	0x6c
#define EMAC_ROV	0x70
#define EMAC_RSE	0x74
#define EMAC_ELE	0x78
#define EMAC_RJA	0x7c
#define EMAC_USF	0x80
#define EMAC_STE	0x84
#define EMAC_RLE	0x88
#define EMAC_HRB	0x90
#define EMAC_HRT	0x94
#define EMAC_SA1B	0x98
#define EMAC_SA1T	0x9c
#define EMAC_SA2B	0xa0
#define EMAC_SA2T	0xa4
#define EMAC_SA3B	0xa8
#define EMAC_SA3T	0xac
#define EMAC_SA4B	0xb0
#define EMAC_SA4T	0xb4
#define EMAC_TID	0xb8
#define EMAC_USRIO	0xc0

/*
 * EMAC_NCR Network Control Register
 */
#define EMAC_NCR_LB		(1 << 0)
#define EMAC_NCR_LLB		(1 << 1)
#define EMAC_NCR_RE		(1 << 2)
#define EMAC_NCR_TE		(1 << 3)
#define EMAC_NCR_MPE		(1 << 4)
#define EMAC_NCR_CLRSTAT	(1 << 5)
#define EMAC_NCR_INCSTAT	(1 << 6)
#define EMAC_NCR_WESTAT		(1 << 7)
#define EMAC_NCR_BP		(1 << 8)
#define EMAC_NCR_TSTART		(1 << 9)
#define EMAC_NCR_THALT		(1 << 10)

/*
 * EMAC_NCFGR Network Configuration Register
 */
#define EMAC_NCFGR_SPD_SHIFT	0
#define EMAC_NCFGR_FD_SHIFT	1
#define EMAC_NCFGR_JFRAME_SHIFT	2
#define EMAC_NCFGR_CAF_SHIFT	3
#define EMAC_NCFGR_NBC_SHIFT	4
#define EMAC_NCFGR_MTI_SHIFT	5
#define EMAC_NCFGR_UNI_SHIFT	6
#define EMAC_NCFGR_BIG_SHIFT	7
#define EMAC_NCFGR_CLK_SHIFT	10
#define EMAC_NCFGR_CLK_MASK	0x3
#define EMAC_NCFGR_RTY_SHIFT	12
#define EMAC_NCFGR_PAE_SHIFT	13
#define EMAC_NCFGR_RBOF_SHIFT	14
#define EMAC_NCFGR_RBOF_MASK	0x3
#define EMAC_NCFGR_RLCE_SHIFT	16
#define EMAC_NCFGR_DRFCS_SHIFT	17
#define EMAC_NCFGR_EFRHD_SHIFT	18
#define EMAC_NCFGR_IRXFCS_SHIFT	19

#define EMAC_NCFGR_SPD_10M		(0 << EMAC_NCFGR_SPD_SHIFT)
#define EMAC_NCFGR_SPD_100M		(1 << EMAC_NCFGR_SPD_SHIFT)
#define EMAC_NCFGR_FD_FULL		(1 << EMAC_NCFGR_FD_SHIFT)
#define EMAC_NCFGR_FD_HALT		(0 << EMAC_NCFGR_FD_SHIFT)
#define EMAC_NCFGR_JFRAME_ENABLE	(1 << EMAC_NCFGR_JFRAME_SHIFT)
#define EMAC_NCFGR_JFRAME_DISABLE	(0 << EMAC_NCFGR_JFRAME_SHIFT)
#define EMAC_NCFGR_CAF_ENABLE		(1 << EMAC_NCFGR_CAF_SHIFT)
#define EMAC_NCFGR_CAF_DISABLE		(0 << EMAC_NCFGR_CAF_SHIFT)
#define EMAC_NCFGR_NBC_ENABLE		(1 << EMAC_NCFGR_NBC_SHIFT)
#define EMAC_NCFGR_NBC_DISABLE		(0 << EMAC_NCFGR_NBC_SHIFT)
#define EMAC_NCFGR_MTI_ENABLE		(1 << EMAC_NCFGR_MTI_SHIFT)
#define EMAC_NCFGR_MTI_DISABLE		(0 << EMAC_NCFGR_MTI_SHIFT)
#define EMAC_NCFGR_UNI_ENABLE		(1 << EMAC_NCFGR_UNI_SHIFT)
#define EMAC_NCFGR_UNI_DISABLE		(0 << EMAC_NCFGR_UNI_SHIFT)
#define EMAC_NCFGR_BIG_ENABLE		(1 << EMAC_NCFGR_BIG_SHIFT)
#define EMAC_NCFGR_BIG_DISABLE		(0 << EMAC_NCFGR_BIG_SHIFT)
#define EMAC_NCFGR_CLK(x)		((x) << EMAC_NCFGR_CLK_SHIFT)
#define EMAC_NCFGR_RTY_ENABLE		(1 << EMAC_NCFGR_RTY_SHIFT)
#define EMAC_NCFGR_RTY_DISABLE		(0 << EMAC_NCFGR_RTY_SHIFT)
#define EMAC_NCFGR_PAE_ENABLE		(1 << EMAC_NCFGR_PAE_SHIFT)
#define EMAC_NCFGR_PAE_DISABLE		(0 << EMAC_NCFGR_PAE_SHIFT)
#define EMAC_NCFGR_RBOF(x)		((x) << EMAC_NCFGR_RBOF_SHIFT)
#define EMAC_NCFGR_RLCE_ENABLE		(1 << EMAC_NCFGR_RLCE_SHIFT)
#define EMAC_NCFGR_RLCE_DISABLE		(0 << EMAC_NCFGR_RLCE_SHIFT)
#define EMAC_NCFGR_DRFCS_ENABLE		(1 << EMAC_NCFGR_DRFCS_SHIFT)
#define EMAC_NCFGR_DRFCS_DISABLE	(0 << EMAC_NCFGR_DRFCS_SHIFT)
#define EMAC_NCFGR_EFRHD_ENABLE		(1 << EMAC_NCFGR_EFRHD_SHIFT)
#define EMAC_NCFGR_EFRHD_DISABLE	(0 << EMAC_NCFGR_EFRHD_SHIFT)
#define EMAC_NCFGR_IRXFCS_ENABLE	(1 << EMAC_NCFGR_IRXFCS_SHIFT)
#define EMAC_NCFGR_IRXFCS_DISABLE	(0 << EMAC_NCFGR_IRXFCS_SHIFT)

/*
 * EMAC_NSR Network Status Register
 */
#define EMAC_NSR_MDIO	(1 << 1)
#define EMAC_NSR_IDLE	(1 << 2)

/*
 * EMAC_TSR Transmit Status Register
 */
#define EMAC_TSR_UBR	(1 << 0)
#define EMAC_TSR_COL	(1 << 1)
#define EMAC_TSR_RLES	(1 << 2)
#define EMAC_TSR_TGO	(1 << 3)
#define EMAC_TSR_BEX	(1 << 4)
#define EMAC_TSR_COMP	(1 << 5)
#define EMAC_TSR_UND	(1 << 6)

/*
 * EMAC_RSR Receive Status Register
 */
#define EMAC_RSR_BNA	(1 << 0)
#define EMAC_RSR_REC	(1 << 1)
#define EMAC_RSR_OVR	(1 << 2)

/*
 * EMAC Interrupt event
 */
#define EMAC_INT_EV_MFD_SHIFT	0
#define EMAC_INT_EV_MFD		(1 << 0)
#define EMAC_INT_EV_RCOMP_SHIFT	1
#define EMAC_INT_EV_RCOMP	(1 << 1)
#define EMAC_INT_EV_RXUBR_SHIFT	2
#define EMAC_INT_EV_RXUBR	(1 << 2)
#define EMAC_INT_EV_TXUBR_SHIFT	3
#define EMAC_INT_EV_TXUBR	(1 << 3)
#define EMAC_INT_EV_TUND_SHIFT	4
#define EMAC_INT_EV_TUND	(1 << 4)
#define EMAC_INT_EV_RLEX_SHIFT	5
#define EMAC_INT_EV_RLEX	(1 << 5)
#define EMAC_INT_EV_TXERR_SHIFT	6
#define EMAC_INT_EV_TXERR	(1 << 6)
#define EMAC_INT_EV_TCOMP_SHIFT	7
#define EMAC_INT_EV_TCOMP	(1 << 7)
#define EMAC_INT_EV_ROVR_SHIFT	10
#define EMAC_INT_EV_ROVR	(1 << 10)
#define EMAC_INT_EV_HRESP_SHIFT	11
#define EMAC_INT_EV_HRESP	(1 << 11)
#define EMAC_INT_EV_PFRE_SHIFT	12
#define EMAC_INT_EV_PFRE	(1 << 12)
#define EMAC_INT_EV_PTZ_SHIFT	13
#define EMAC_INT_EV_PTZ		(1 << 13)

/*
 * EMAC_MAN PHY Maintenance Register
 */
#define EMAC_MAN_DATA_SHIFT 	0
#define EMAC_MAN_DATA_MASK 	0xffff
#define EMAC_MAN_CODE_SHIFT 	16
#define EMAC_MAN_CODE_MASK 	0x3
#define EMAC_MAN_REGA_SHIFT	18
#define EMAC_MAN_REGA_MASK	0x1f
#define EMAC_MAN_PHYA_SHIFT	23
#define EMAC_MAN_PHYA_MASK	0x1f
#define EMAC_MAN_RW_SHIFT	28
#define EMAC_MAN_RW_MASK	0x3
#define EMAC_MAN_SOF_SHIFT	30
#define EMAC_MAN_SOF_MASK	0x3

/*
 * EMAC_PTR Pause Time Register
 */
#define EMAC_PTR_PTIME_SHIFT 	0
#define EMAC_PTR_PTIME_MASK 	0xffff

/*
 * EMAC_TID Type ID Checking Register
 */
#define EMAC_TID_SHIFT 		0
#define EMAC_TID_MASK 		0xffff

/*
 * EMAC_USRIO User Input/Output Register
 */
#define EMAC_USRIO_RMII 	(1 << 0)
#define EMAC_USRIO_CLKEN	(1 << 1)

/*
 * transmit buffer descriptor
 */
#define EMAC_TX_DESC_LENGTH_MASK 	0x7ff
#define EMAC_TX_DESC_LENGTH_SHIFT	0
#define EMAC_TX_DESC_LENGTH(x)  \
	((EMAC_TX_DESC_LENGTH_MASK & (x)) << EMAC_TX_DESC_LENGTH_SHIFT)
#define EMAC_TX_DESC_LAST_BUFFER	(1 << 15)
#define EMAC_TX_DESC_NO_CRC		(1 << 16)
#define EMAC_TX_DESC_BUF_EXHST		(1 << 27)
#define EMAC_TX_DESC_UNDERRUN		(1 << 28)
#define EMAC_TX_DESC_ERROR		(1 << 29)
#define EMAC_TX_DESC_WRAP		(1 << 30)
#define EMAC_TX_DESC_USED		(1 << 31)

/*
 * receive buffer descriptor
 */
#define EMAC_RX_DESC_LENGTH_MASK	0xfff
#define EMAC_RX_DESC_LENGTH_SHIFT	0
#define EMAC_RX_DESC_LENGTH(x)  \
	((EMAC_RX_DESC_LENGTH_MASK & (x)) << EMAC_RX_DESC_LENGTH_SHIFT)
#define EMAC_RX_DESC_RX_OFFSET(x)	((0x3 & (x)) << 12)
#define EMAC_RX_DESC_SOF		(1 << 14)
#define EMAC_RX_DESC_EOF		(1 << 15)
#define EMAC_RX_DESC_CFI		(1 << 16)
#define EMAC_RX_DESC_VLAN_PRIOR(x)	((0x7 & (x)) << 17)
#define EMAC_RX_DESC_PRIOR_DETECT	(1 << 20)
#define EMAC_RX_DESC_VLAN_DETECT	(1 << 21)
#define EMAC_RX_DESC_TYPE_ID_MATCH	(1 << 22)
#define EMAC_RX_DESC_SA4_MATCH		(1 << 23)
#define EMAC_RX_DESC_SA3_MATCH		(1 << 24)
#define EMAC_RX_DESC_SA2_MATCH		(1 << 25)
#define EMAC_RX_DESC_SA1_MATCH		(1 << 26)
#define EMAC_RX_DESC_EXT_ADDR_MATCH	(1 << 28)
#define EMAC_RX_DESC_UNI_HASH_MATCH	(1 << 29)
#define EMAC_RX_DESC_MUL_HASH_MATCH	(1 << 30)
#define EMAC_RX_DESC_BROADCAST_MATCH	(1 << 31)

#define EMAC_RX_DESC_OWNER_SOFT		(1 << 0)
#define EMAC_RX_DESC_OWNER_HARD		(0 << 0)
#define EMAC_RX_DESC_OWNER_MASK		(1 << 0)
#define EMAC_RX_DESC_WRAP		(1 << 1)
#define EMAC_RX_DESC_ADDR_MASK		0xfffffffc

#define EMAC_RX_BUF_SIZE	128
#define EMAC_TX_BUF_SIZE	1536

#define EMAC_RX_DESC_NR		32
#define EMAC_TX_DESC_NR		8

struct emac_buffer_desc {
	uint32_t addr;
	uint32_t ctrl;
};


struct emac_net_device {
	struct net_device *ndev;
	void __iomem *base;
	spinlock_t lock;

	int buffer_size;
	dma_addr_t phys_buffer_base;
	void * virt_buffer_base;

	struct emac_buffer_desc *rx_desc;
	struct emac_buffer_desc *tx_desc;
	dma_addr_t phys_rx_desc;
	dma_addr_t phys_tx_desc;
	int rx_desc_cur;
	int tx_desc_start;
	int tx_desc_end;

	struct sk_buff *tx_skb[EMAC_TX_DESC_NR];
	void * rx_buf_base;

	uint32_t event_mask;
	volatile unsigned long event_status;
};



static int emac_open(struct net_device *ndev)
{
	struct emac_net_device *emac = netdev_priv(ndev);

	netif_start_queue(ndev);

	writel(EMAC_NCR_RE|EMAC_NCR_TE,emac->base + EMAC_NCR);
	return 0;
}

static int emac_stop(struct net_device *ndev)
{
	netif_stop_queue(ndev);
	return 0;
}

static int emac_tx_avail(struct emac_net_device *emac)
{
	return (emac->tx_desc_end - emac->tx_desc_start
			+ EMAC_TX_DESC_NR)%EMAC_TX_DESC_NR != EMAC_TX_DESC_NR -1;
}

static int emac_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct emac_net_device *emac = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct emac_buffer_desc *desc;
	dma_addr_t buf;
	uint32_t value;


	if(!emac_tx_avail(emac)){
		netif_stop_queue(ndev);
		netdev_err(ndev,"tx queue full when queue awake");
		return NETDEV_TX_BUSY;
	}

	buf = dma_map_single(&ndev->dev,skb->data,
			skb->len,DMA_TO_DEVICE);
	if(unlikely(dma_mapping_error(&ndev->dev,buf))){
		netdev_err(ndev,"sk_buff dma mapping failed ,sk_buff will be dropped");
		stats->tx_dropped++;
		stats->tx_errors++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	desc = emac->tx_desc + emac->tx_desc_end;
	emac->tx_skb[emac->tx_desc_end] = skb;
	emac->tx_desc_end++;
	if(emac->tx_desc_end >= EMAC_TX_DESC_NR){
		emac->tx_desc_end = 0;
	}


	desc->addr = buf;
	desc->ctrl &= EMAC_TX_DESC_WRAP;
	desc->ctrl |= EMAC_TX_DESC_LAST_BUFFER|EMAC_TX_DESC_LENGTH(skb->len);

	skb_tx_timestamp(skb);

	value = readl(emac->base + EMAC_NCR);
	writel(value|EMAC_NCR_TSTART,emac->base + EMAC_NCR);

	return NETDEV_TX_OK;
}

static void emac_timeout(struct net_device *ndev)
{

}

static int emac_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{
	return 0;
}

static int emac_set_mac_address(struct net_device *ndev, void *p)
{
	struct emac_net_device *emac = netdev_priv(ndev);
	uint8_t *addr = p;

	writel(addr[0]|addr[1]<<8|addr[2]<<16|addr[3]<<24,
			emac->base + EMAC_SA1B);
	writel(addr[4]|addr[5]<<8,
			emac->base + EMAC_SA1T);


	return 0;
}

static void emac_tx_complate(struct emac_net_device *emac)
{
	struct emac_buffer_desc *desc;
	struct net_device_stats *stats = &emac->ndev->stats;
	dma_addr_t addr;
	uint32_t tx_status;
	int len;

	desc = emac->tx_desc + emac->tx_desc_start;
	addr = desc->addr;
	len  = (desc->ctrl>>EMAC_RX_DESC_LENGTH_SHIFT)&EMAC_RX_DESC_LENGTH_MASK;

	tx_status = readl(emac->base + EMAC_TSR);

	if(tx_status & EMAC_TSR_COMP){
		stats->tx_packets++;
		stats->tx_bytes += len;

		dma_unmap_single(&emac->ndev->dev,addr,len,DMA_TO_DEVICE);
		dev_consume_skb_any(emac->tx_skb[emac->tx_desc_start]);

	} else {
		stats->tx_errors++;
		stats->tx_dropped++;

		dma_unmap_single(&emac->ndev->dev,addr,len,DMA_TO_DEVICE);
		dev_kfree_skb_any(emac->tx_skb[emac->tx_desc_start]);
	}

	emac->tx_desc_start++;
	if(emac->tx_desc_start >= EMAC_TX_DESC_NR){
		emac->tx_desc_start = 0;
	}


}

static void emac_rx_complate(struct emac_net_device *emac)
{
	struct emac_buffer_desc *rx_desc = emac->rx_desc;
	struct net_device_stats *stats = &emac->ndev->stats;
	struct net_device *ndev = emac->ndev;
	struct sk_buff *skb;
	uint8_t *data;
	uint32_t rx_status;
	int rxlen,len = EMAC_RX_BUF_SIZE;
	int index = emac->rx_desc_cur,start_index;


	rx_status = readl(emac->base + EMAC_RSR);

	/*
	 * find start of frame
	 */
	while((rx_desc[index].addr & EMAC_RX_DESC_OWNER_MASK) ==
			EMAC_RX_DESC_OWNER_SOFT){
		if(rx_desc[index].ctrl & EMAC_RX_DESC_SOF){
			break;
		}
		index = (index + EMAC_RX_DESC_NR +1)%EMAC_RX_DESC_NR;
	}

	/*
	 * check whether find SOF or not
	 */
	if(((rx_desc[index].addr & EMAC_RX_DESC_OWNER_MASK)
			!= EMAC_RX_DESC_OWNER_SOFT) ||
			!(rx_desc[index].ctrl & EMAC_RX_DESC_SOF)){
		emac->rx_desc_cur = index;
		return ;
	}

	start_index = index;
	/*
	 * find end of frame
	 */
	while((rx_desc[index].addr & EMAC_RX_DESC_OWNER_MASK) ==
			EMAC_RX_DESC_OWNER_SOFT){
		if(rx_desc[index].ctrl & EMAC_RX_DESC_EOF){
			break;
		}
		index = (index + EMAC_RX_DESC_NR +1)%EMAC_RX_DESC_NR;
	}

	/*
	 * check whether find EOF or not
	 */
	if(((rx_desc[index].addr & EMAC_RX_DESC_OWNER_MASK)
			!= EMAC_RX_DESC_OWNER_SOFT) ||
			!(rx_desc[index].ctrl & EMAC_RX_DESC_EOF)){
		emac->rx_desc_cur = index;
		return ;
	}

	rxlen = (rx_desc[index].ctrl >> EMAC_RX_DESC_LENGTH_SHIFT)
			&EMAC_RX_DESC_LENGTH_MASK;

	skb = netdev_alloc_skb_ip_align(ndev, rxlen);
	if(!skb){
		return ;
	}

	stats->rx_bytes += rxlen;
	stats->rx_packets++;

	data = skb_put(skb,rxlen);

	while(rxlen > 0){
		if(rxlen < len){
			len = rxlen;
		}

		memcpy(data,emac->rx_buf_base + EMAC_RX_BUF_SIZE * start_index,len);

		data += len;
		rxlen -= len;

		rx_desc[start_index].addr &= ~EMAC_RX_DESC_OWNER_MASK;

		start_index = (start_index + EMAC_RX_DESC_NR + 1)%EMAC_RX_DESC_NR;

	}

	emac->rx_desc_cur = start_index;


	skb->protocol = eth_type_trans(skb,ndev);

	netif_receive_skb(skb);


}

static const struct net_device_ops emac_netdev_ops = {
	.ndo_open		= emac_open,
	.ndo_stop		= emac_stop,
	.ndo_start_xmit		= emac_start_xmit,
	.ndo_tx_timeout		= emac_timeout,
	.ndo_do_ioctl		= emac_ioctl,
	.ndo_set_mac_address	= emac_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
};


static void emac_hw_init(struct emac_net_device *emac)
{
	dma_addr_t rx_buf;
	struct emac_buffer_desc *rx_desc,*tx_desc;
	uint8_t * addr;
	int i;

	emac->rx_desc = emac->virt_buffer_base;
	emac->tx_desc = emac->virt_buffer_base +
			EMAC_RX_DESC_NR * sizeof(*rx_desc);
	emac->phys_rx_desc = emac->phys_buffer_base ;
	emac->phys_tx_desc = emac->phys_buffer_base +
			EMAC_RX_DESC_NR * sizeof(*rx_desc);

	rx_buf = emac->phys_buffer_base +
			(EMAC_RX_DESC_NR + EMAC_TX_DESC_NR) * sizeof(*rx_desc);

	emac->rx_buf_base = emac->virt_buffer_base +
			(EMAC_RX_DESC_NR + EMAC_TX_DESC_NR) * sizeof(*rx_desc);

	rx_desc = emac->rx_desc;
	tx_desc = emac->tx_desc;

	emac->rx_desc_cur = 0;
	emac->tx_desc_start = emac->tx_desc_end = 0;

	for(i = 0;i < EMAC_RX_DESC_NR;i++){
		rx_desc->addr = rx_buf;
		rx_desc++;
		rx_buf += EMAC_RX_BUF_SIZE;
	}
	emac->rx_desc[EMAC_RX_DESC_NR-1].ctrl |= EMAC_RX_DESC_WRAP;

	for(i = 0;i < EMAC_TX_DESC_NR;i++){
		tx_desc->ctrl |= EMAC_TX_DESC_USED;
		tx_desc++;
	}
	emac->tx_desc[EMAC_TX_DESC_NR-1].ctrl |= EMAC_TX_DESC_WRAP;


	writel(emac->phys_rx_desc,emac->base + EMAC_RBQP);
	writel(emac->phys_tx_desc,emac->base + EMAC_TBQP);

	addr = emac->ndev->dev_addr;
	writel(addr[0]|addr[1]<<8|addr[2]<<16|addr[3]<<24,emac->base + EMAC_SA1B);
	writel(addr[4]|addr[5]<<8,emac->base + EMAC_SA1T);


	emac->event_mask = EMAC_INT_EV_RCOMP|EMAC_INT_EV_TCOMP;
	emac->event_status = 0;
	writel(emac->event_mask,emac->base + EMAC_IER);

}




static irqreturn_t emac_irq_thread(int irq, void *dev)
{
	struct emac_net_device *emac = dev;

	while(emac->event_status){
		if(test_and_clear_bit(EMAC_INT_EV_TCOMP_SHIFT,
				&emac->event_status)){
			emac_tx_complate(emac);

		}
		if(test_and_clear_bit(EMAC_INT_EV_RCOMP_SHIFT,
				&emac->event_status)){
			emac_rx_complate(emac);

		}
	}

	writel(emac->event_mask,emac->base + EMAC_IER);

	return IRQ_HANDLED;
}

static irqreturn_t emac_irq_handle(int irq, void *dev)
{
	struct emac_net_device *emac = dev;
	uint32_t status;

	status = readl(emac->base + EMAC_ISR);
	status &= emac->event_mask;

	if(status){
		emac->event_status |= status;
		writel(status,emac->base + EMAC_IDR);
		return IRQ_WAKE_THREAD;
	}
	return IRQ_NONE;
}


static int at91sam9_emac_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct net_device *ndev;
	struct device_node *np = pdev->dev.of_node;
	struct emac_net_device *emac;
	const char *mac_addr;
	int irq;
	int ret;


	ndev = devm_alloc_etherdev(&pdev->dev,sizeof(struct emac_net_device));
	if(!ndev){
		dev_err(&pdev->dev,"could not alloc device.\n");
		return -ENOMEM;
	}


	SET_NETDEV_DEV(ndev, &pdev->dev);

	emac = netdev_priv(ndev);
	memset(emac,0,sizeof(*emac));

	emac->ndev = ndev;


	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0) {
		dev_err(&pdev->dev, "error getting resources.\n");
		return -ENXIO;
	}

	emac->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(emac->base)) {
		return PTR_ERR(emac->base);
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, emac_irq_handle,
			emac_irq_thread, 0, DRIVER_NAME, emac);
	if(ret)
		return ret;


	mac_addr = of_get_mac_address(np);
	if (mac_addr)
		memcpy(ndev->dev_addr, mac_addr, ETH_ALEN);

	/* Check if the MAC address is valid, if not get a random one */
	if (!is_valid_ether_addr(ndev->dev_addr)) {
		eth_hw_addr_random(ndev);
		dev_warn(&pdev->dev, "using random MAC address %pM\n",
			 ndev->dev_addr);
	}

	if(dma_coerce_mask_and_coherent(&pdev->dev,DMA_BIT_MASK(32))){
		dev_warn(&pdev->dev,"No suitable DMA memory available");
	}

	emac->buffer_size = EMAC_RX_DESC_NR * (EMAC_RX_BUF_SIZE + sizeof(struct emac_buffer_desc)) +
			EMAC_TX_DESC_NR * sizeof(struct emac_buffer_desc);

	emac->virt_buffer_base = dmam_alloc_coherent(&pdev->dev,
			emac->buffer_size,&emac->phys_buffer_base,GFP_KERNEL);
	if(emac->virt_buffer_base == NULL){
		dev_err(&pdev->dev,"Failed to alloc dma buffer");
		return -ENOMEM;
	}
	memset(emac->virt_buffer_base,0,emac->buffer_size);

	ndev->netdev_ops = &emac_netdev_ops;

	emac_hw_init(emac);

	platform_set_drvdata(pdev, ndev);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "Registering netdev failed!\n");
		return -ENODEV;
	}


	return 0;
}

static int at91sam9_emac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_netdev(ndev);

	return 0;
}

static const struct of_device_id at91sam9_emac_of_match[] = {
	{ .compatible = "atmel,at91sam9-emac",},
};

static struct platform_driver at91sam9_emac_driver = {
	.driver = {
		.name   = DRIVER_NAME,
		.of_match_table = of_match_ptr(at91sam9_emac_of_match),
	},
	.probe          = at91sam9_emac_probe,
	.remove         = at91sam9_emac_remove,
};
module_platform_driver(at91sam9_emac_driver);




MODULE_DEVICE_TABLE(of, at91sam9_emac_of_match);
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("Atmel EMAC driver");
MODULE_LICENSE("GPL v2");



