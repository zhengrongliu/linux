/*
 * vhci-hcd.c
 *
 *  Created on: Nov 24, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <asm/unaligned.h>

#include "vhci-hcd.h"

#define VHCI_RING_PARAM_SIZE_POWER2	12
#define VHCI_RING_PARAM_SIZE 		(1<<VHCI_RING_PARAM_SIZE_POWER2)
#define VHCI_RING_PARAM_SIZE_MASK	(VHCI_RING_PARAM_SIZE-1)

struct vhci_qtd{
	struct list_head list;
	struct urb *urb;
	int node_count;
	uint16_t head;
};



static void vhci_qtd_free(struct vhci_hcd *vhci,struct vhci_qtd *qtd);
static struct vhci_qtd *vhci_qtd_alloc(struct vhci_hcd *vhci,struct urb *urb);
static int vhci_urb_status(uint16_t status );
static void vhci_process_ready_list(struct vhci_hcd *vhci);

static const char hcd_name [] = "vhci_hcd";
static struct kmem_cache *vhci_qtd_cache;

static void vhci_free_node_put(struct vhci_hcd *vhci,uint16_t index)
{
	struct vhci_ringnode_base *base = (struct vhci_ringnode_base*)vhci->ring_base;

	if(vhci->free_node_count == 0){
		vhci->free_node_head = index;
		vhci_node_list_init(base,&base[index].list);
		vhci->free_node_count++;
		return ;
	}

	vhci_node_list_insert_tail(base,&base[vhci->free_node_head].list,
			&base[index].list);

	vhci->free_node_count++;
}


static uint16_t vhci_free_node_get(struct vhci_hcd*vhci)
{
	struct vhci_ringnode_base *base = (struct vhci_ringnode_base*)vhci->ring_base;
	uint16_t index;;

	if(vhci->free_node_count == 0){
		return VHCI_RING_INVALID_NODE;
	}

	index = base[vhci->free_node_head].list.prev;
	vhci_node_list_remove(base,&base[index].list);

	vhci->free_node_count--;

	return index;
}

static struct vhci_ringnode_head *vhci_node_head_alloc(struct vhci_hcd *vhci)
{
	union vhci_ringnode *base = (union vhci_ringnode*)vhci->ring_base;
	uint16_t index = vhci_free_node_get(vhci);
	if(index == VHCI_RING_INVALID_NODE){
		return NULL;
	}
	return &base[index].head;
}

static struct vhci_ringnode_segment *vhci_node_segment_alloc(struct vhci_hcd *vhci)
{
	union vhci_ringnode *base = (union vhci_ringnode*)vhci->ring_base;
	uint16_t index = vhci_free_node_get(vhci);
	if(index == VHCI_RING_INVALID_NODE){
		return NULL;
	}
	return &base[index].segment;
}


static void vhci_transfer_done(struct vhci_hcd *vhci)
{
	struct urb *urb;
	struct vhci_qtd *qtd;
	struct vhci_ringnode_head *head;
	uint16_t index;
	int urb_status;

	spin_lock(&vhci->lock);
	index = vhci_readl(vhci,VHCI_DONE);
	while(index != VHCI_RING_INVALID_NODE){
		qtd = vhci->qtd_table[index];
		if(qtd == NULL){
			vhci_err(vhci,"qtd was not exit (index is wrong?)\n");
			continue;
		}

		urb = qtd->urb;
		head = (struct vhci_ringnode_head*)vhci->ring_base + index;


		urb->actual_length = head->length;
		urb_status = vhci_urb_status(head->status);

		vhci_qtd_free(vhci,qtd);
		vhci->qtd_table[index] = NULL;

		vhci_process_ready_list(vhci);


		usb_hcd_unlink_urb_from_ep(vhci_to_hcd(vhci), urb);

		spin_unlock(&vhci->lock);
		usb_hcd_giveback_urb(vhci_to_hcd(vhci), urb, urb_status);
		spin_lock(&vhci->lock);

		index = vhci_readl(vhci,VHCI_DONE);
	}
	spin_unlock(&vhci->lock);


}

static irqreturn_t vhci_irq(struct usb_hcd *hcd)
{
	struct vhci_hcd	*vhci = hcd_to_vhci (hcd);
	uint32_t status ;


	status = vhci_readl(vhci,VHCI_STATUS);

	vhci_writel(vhci,status,VHCI_STATUS);

	if(status & VHCI_ST_XFER_DONE){
		vhci_transfer_done(vhci);
	}

	return IRQ_HANDLED;
}

int vhci_setup (struct usb_hcd *hcd)
{
	struct vhci_hcd	*vhci = hcd_to_vhci (hcd);

	vhci->max_ports = (vhci_readl(vhci,VHCI_FEATURE)>>VHCI_FT_USB2PORTNR_OFFSET)&
			VHCI_FT_USB2PORTNR_MASK;

	if (!(hcd->driver->flags & HCD_LOCAL_MEM))
		hcd->self.sg_tablesize = ~0;

	hcd->self.no_stop_on_short = 1;

	INIT_LIST_HEAD(&vhci->ready);


	vhci_writel(vhci,VHCI_CMD_RESET,VHCI_CMD);

	return 0;
}
EXPORT_SYMBOL_GPL(vhci_setup);

static int vhci_run (struct usb_hcd *hcd)
{
	struct vhci_hcd	*vhci = hcd_to_vhci (hcd);
	dma_addr_t ring_dma;
	int i;

	vhci->qtd_table = devm_kmalloc(hcd->self.controller,
			sizeof(*vhci->qtd_table) << VHCI_RING_PARAM_SIZE_POWER2,GFP_KERNEL);
	if(!vhci->qtd_table){
		return -ENOMEM;
	}


	vhci->ring_base = dmam_alloc_coherent(hcd->self.controller,
			sizeof(union vhci_ringnode)<<VHCI_RING_PARAM_SIZE_POWER2,
			&ring_dma,GFP_KERNEL);
	if(!vhci->ring_base){
		return -ENOMEM;
	}

	for(i = 0; i < VHCI_RING_PARAM_SIZE;i++){
		vhci_free_node_put(vhci,i);
	}

	vhci_writel(vhci,VHCI_ST_XFER_DONE,VHCI_INTMASK);
	vhci_writel(vhci,VHCI_RING_PARAM_SIZE_POWER2,VHCI_RING_SIZE);
	vhci_writel(vhci,ring_dma,VHCI_RING_BASE);
	vhci_writel(vhci,VHCI_CMD_INIT,VHCI_CMD);

	return 0;
}


static void vhci_stop (struct usb_hcd *hcd)
{

}

static void vhci_shutdown (struct usb_hcd *hcd)
{

}

static int vhci_get_frame(struct usb_hcd *hcd)
{
	return 0;
}

static int vhci_urb_status(uint16_t status )
{
	switch(status){
	case VHCI_HEAD_STATUS_XFER_OK:
		return 0;
	case VHCI_HEAD_STATUS_ERROR_NODEV:
		return -ENODEV;
	case VHCI_HEAD_STATUS_XFER_STALL:
		return -EPIPE;
	case VHCI_HEAD_STATUS_ERROR_BABBLE:
		return -EOVERFLOW;
	case VHCI_HEAD_STATUS_ERROR_IO:
		return -EPROTO;
	}
	return 0;
}

static inline unsigned int vhci_port_speed(unsigned int status)
{
	unsigned int speed;
	speed = status & VHCI_PORT_ST_SPEED_MASK;

	switch(speed){
	case VHCI_PORT_ST_SPEED_LOW:
		return USB_PORT_STAT_LOW_SPEED;
	case VHCI_PORT_ST_SPEED_FULL:
	case VHCI_PORT_ST_SPEED_HIGH:
	case VHCI_PORT_ST_SPEED_SUPER:
		return USB_PORT_STAT_HIGH_SPEED;

	}

	return USB_PORT_STAT_HIGH_SPEED;
}

static struct vhci_qtd *vhci_qtd_alloc(struct vhci_hcd *vhci,struct urb *urb)
{
	struct vhci_qtd *qtd;
	int node_count;

	qtd = kmem_cache_zalloc(vhci_qtd_cache, GFP_ATOMIC);
	if(!qtd){
		return NULL;
	}
	qtd->urb = urb;
	urb->hcpriv = qtd;

	/*
	 * caculate node needs
	 */
	node_count = 1;
	if(usb_pipecontrol(urb->pipe)){
		node_count++;
	}

	if(urb->num_mapped_sgs > 0 && urb->transfer_buffer_length > 0){
		node_count += urb->num_mapped_sgs;
	} else if(urb->transfer_buffer_length > 0){
		node_count++;
	}

	if(usb_pipecontrol(urb->pipe)){
		node_count++;
	}

	qtd->node_count = node_count;
	qtd->head = VHCI_RING_INVALID_NODE;


	list_add_tail(&qtd->list,&vhci->ready);

	return qtd;
}

static void vhci_qtd_free(struct vhci_hcd *vhci,struct vhci_qtd *qtd)
{
	struct vhci_ringnode_base  *base = (struct vhci_ringnode_base*)vhci->ring_base;
	struct vhci_ringnode_list *head;

	int i;
	uint16_t index;


	if(qtd->head == VHCI_RING_INVALID_NODE){
		list_del(&qtd->list);
		goto done;
	}

	head = &base[qtd->head].list;

	for(i = 0;i < qtd->node_count-1;i++){
		index = head->next;

		vhci_node_list_remove(base,vhci_node_list_next(base,head));

		vhci_free_node_put(vhci,index);

	}
	vhci_free_node_put(vhci,vhci_node_list_index(base,head));

done:
	kmem_cache_free(vhci_qtd_cache,qtd);
}


static uint8_t vhci_transfer_flags(struct urb *urb)
{
	uint8_t req = 0;

	if(urb->transfer_flags & URB_SHORT_NOT_OK){
		req |= VHCI_REQ_SHORT_NOT_OK;
	} else if(urb->transfer_flags & URB_ZERO_PACKET){
		req |= VHCI_REQ_ZERO_PACKET;
	} else if (urb->transfer_flags & URB_ISO_ASAP){
		req |= VHCI_REQ_ISO_ASAP;
	}


	req |= usb_pipein(urb->pipe);
	req |= usb_pipetype(urb->pipe);

	return req;
}

static inline void vhci_ringnode_head_init(struct vhci_ringnode_head *head,
		uint8_t addr,uint8_t epnum,uint8_t request,
		uint16_t segments,uint64_t length)
{

	head->type = VHCI_NODE_TYPE_HEAD;
	head->status = VHCI_NODE_STATUS_USED;
	head->addr = addr;
	head->epnum = epnum;
	head->request = request;
	head->length = length;
	head->segments = segments;
}

static inline void vhci_ringnode_segment_init(struct vhci_ringnode_segment *segment,
		uint64_t addr, uint64_t length)
{
	segment->type = VHCI_NODE_TYPE_SEGMENT;
	segment->status = VHCI_NODE_STATUS_USED;
	segment->addr = addr;
	segment->length = length;
}


static int vhci_process_qtd(struct vhci_hcd *vhci,struct vhci_qtd *qtd)
{
	struct urb *urb = qtd->urb;
	struct vhci_ringnode_base  *base = (struct vhci_ringnode_base*)vhci->ring_base;
	struct vhci_ringnode_list *list;
	struct vhci_ringnode_segment *segment;
	struct vhci_ringnode_head *head;
	unsigned int node_count;
	int total_len,len;
	dma_addr_t buf;
	struct scatterlist*sg = NULL;
	uint16_t index;

	node_count = qtd->node_count;

	if(urb->num_mapped_sgs > 0){
		sg = urb->sg;
	}


	total_len = urb->transfer_buffer_length;

	head = vhci_node_head_alloc(vhci);//not failed
	BUG_ON(head == NULL);
	list = &head->list;

	vhci_ringnode_head_init(head,usb_pipedevice(urb->pipe),usb_pipeendpoint(urb->pipe),
			vhci_transfer_flags(urb),node_count-1,total_len);
	vhci_node_list_init(base,list);

	/*
	 * setup packet for control transaction
	 */
	if(usb_pipecontrol(urb->pipe)){
		segment = vhci_node_segment_alloc(vhci); //will not failed
		BUG_ON(segment == NULL);

		vhci_ringnode_segment_init(segment,urb->setup_dma,sizeof(struct usb_ctrlrequest));
		vhci_node_list_insert_tail(base,list,&segment->list);
	}

	while(total_len > 0){
		len = sg?min_t(int,sg_dma_len(sg),total_len):total_len;
		buf = sg?sg_dma_address(sg):urb->transfer_dma;

		segment = vhci_node_segment_alloc(vhci); //will not failed
		BUG_ON(segment == NULL);

		vhci_ringnode_segment_init(segment,buf,len);
		vhci_node_list_insert_tail(base,list,&segment->list);

		if(sg){
			sg = sg_next(sg);
		}
		total_len -= len;
	}

	/*
	 * ack packet for control transaction
	 */
	if(usb_pipecontrol(urb->pipe)){
		segment = vhci_node_segment_alloc(vhci); //will not failed
		BUG_ON(segment == NULL);

		vhci_ringnode_segment_init(segment,0,0);
		vhci_node_list_insert_tail(base,list,&segment->list);
	}

	/*
	 * record request
	 */
	index = vhci_node_list_index(base,list);
	vhci->qtd_table[index] = qtd;
	qtd->head = index;


	/*
	 * submit transfer
	 */
	vhci_writel(vhci,index,VHCI_PARAM);
	vhci_writel(vhci,VHCI_CMD_SUBMIT,VHCI_CMD);
	return 0;
}

static void vhci_process_ready_list(struct vhci_hcd *vhci)
{
	struct vhci_qtd *qtd;
	struct list_head *ptr,*next;
	int ret;


	list_for_each_safe(ptr,next,&vhci->ready){
		qtd = list_entry(ptr,struct vhci_qtd,list);
		if(vhci->free_node_count < qtd->node_count){
			break;
		}

		ret = vhci_process_qtd(vhci,qtd);
		if(ret < 0){
			break;
		}

		list_del(ptr);
	}

}

static int vhci_urb_enqueue(struct usb_hcd *hcd,
				struct urb *urb, gfp_t mem_flags)
{
	struct vhci_hcd *vhci = hcd_to_vhci(hcd);
	struct vhci_qtd *qtd;
	unsigned long flags;
	int ret = 0;



        spin_lock_irqsave(&vhci->lock, flags);
        ret = usb_hcd_link_urb_to_ep(hcd,urb);
        if(ret){
        	goto failed_link;
        }

	qtd = vhci_qtd_alloc(vhci,urb);
	if(!qtd){
		ret = -ENOMEM;
		goto failed_alloc_qtd;
	}

	if(vhci->free_node_count < qtd->node_count ){
		goto failed_ring_full;
	}

	vhci_process_ready_list(vhci);

        spin_unlock_irqrestore(&vhci->lock, flags);

	return 0;

failed_alloc_qtd:
	usb_hcd_unlink_urb_from_ep(hcd, urb);
failed_ring_full:
failed_link:
        spin_unlock_irqrestore(&vhci->lock, flags);
	return ret;
}

static int vhci_urb_dequeue(struct usb_hcd *hcd,
				struct urb *urb, int status)
{
	struct vhci_hcd *vhci = hcd_to_vhci(hcd);
	struct vhci_qtd *qtd;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&vhci->lock, flags);
	ret = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (ret)
		goto done;

	if(!urb->hcpriv){
		vhci_err(vhci,"urb hcpriv is NULL\n");
		goto done;
	}

	qtd = urb->hcpriv;
	if(qtd->head == VHCI_RING_INVALID_NODE){
		vhci_qtd_free(vhci,qtd);

		usb_hcd_unlink_urb_from_ep(vhci_to_hcd(vhci), urb);

		spin_unlock(&vhci->lock);
		usb_hcd_giveback_urb(vhci_to_hcd(vhci), urb, status);
		spin_lock(&vhci->lock);
		goto done;
	}


	vhci_writel(vhci,qtd->head,VHCI_PARAM);
	vhci_writel(vhci,VHCI_CMD_CANCEL,VHCI_CMD);

done:
	spin_unlock_irqrestore(&vhci->lock, flags);

	return 0;
}

static void vhci_endpoint_disable(struct usb_hcd *hcd,
			struct usb_host_endpoint *ep)
{

}

static void vhci_endpoint_reset(struct usb_hcd *hcd,
			struct usb_host_endpoint *ep)
{

}

static int vhci_hub_status_data (struct usb_hcd *hcd, char *buf)
{
	struct vhci_hcd	*vhci = hcd_to_vhci (hcd);
	uint32_t value;
	uint32_t mask;
	int len;
	int i = 0;

	len = (vhci->max_ports+1+7)>>3;

	mask = VHCI_PORT_ST_C_MASK;

	memset(buf,0,len);

	for(i = 0;i < vhci->max_ports;i++){
		value = vhci_readl(vhci,VHCI_PORT_STATUS(i));
		if(!(value & mask)){
			continue;
		}

		if(i < 7){
			buf[0] |= 1<<(i+1);
		} else {
			buf[(i+1)>>3] |= 1<<((i+1)&0x7);
		}
	}

	return len;
}

static void vhci_hub_descriptor (struct vhci_hcd *vhci,
		struct usb_hub_descriptor *desc)
{
	u16 temp;

	desc->bDescriptorType = USB_DT_HUB;
	desc->bPwrOn2PwrGood = 10;	/* ehci 1.0, 2.3.9 says 20ms max */
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = vhci->max_ports;
	temp = 1 + (vhci->max_ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	/* two bitmaps:  ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	memset(&desc->u.hs.DeviceRemovable[0], 0, temp);
	memset(&desc->u.hs.DeviceRemovable[temp], 0xff, temp);

	temp = HUB_CHAR_INDV_PORT_OCPM|HUB_CHAR_NO_LPSM;

	desc->wHubCharacteristics = cpu_to_le16(temp);
}


static void vhci_hub_clear_port_feature(struct vhci_hcd *vhci,
		unsigned int port,uint16_t value)
{

	switch(value){
	case USB_PORT_FEAT_C_CONNECTION:
		vhci_writel(vhci, VHCI_PORT_ST_C_CONNECTION, VHCI_PORT_STATUS(port));
		break;
	case USB_PORT_FEAT_C_RESET:
		vhci_writel(vhci, VHCI_PORT_ST_C_RESET, VHCI_PORT_STATUS(port));
		break;
	case USB_PORT_FEAT_C_ENABLE:
		vhci_writel(vhci, VHCI_PORT_ST_C_ENABLE, VHCI_PORT_STATUS(port));
		break;
	case USB_PORT_FEAT_CONNECTION:
		vhci_writel(vhci, VHCI_PORT_ST_CONNECTION, VHCI_PORT_STATUS(port));
		break;
	case USB_PORT_FEAT_ENABLE:
		vhci_writel(vhci, VHCI_PORT_ST_ENABLE, VHCI_PORT_STATUS(port));
		break;
	case USB_PORT_FEAT_RESET:
		vhci_writel(vhci, VHCI_PORT_ST_RESET, VHCI_PORT_STATUS(port));
		break;
	case USB_PORT_FEAT_POWER:
		vhci_writel(vhci, VHCI_PORT_ST_POWER, VHCI_PORT_STATUS(port));
		break;
	}
}

static void vhci_hub_set_port_feature(struct vhci_hcd*vhci,
		unsigned int port ,uint16_t value)
{
	switch(value){
	case USB_PORT_FEAT_RESET:
		vhci_writel(vhci,VHCI_PORT_CMD_RESET,VHCI_PORT_CMD(port));
		break;
	case USB_PORT_FEAT_POWER:
		vhci_writel(vhci,VHCI_PORT_ST_POWER,VHCI_PORT_STATUS(port));
		break;
	}
}

static int vhci_hub_control (struct usb_hcd *hcd,
				u16 typeReq, u16 wValue, u16 wIndex,
				char *buf, u16 wLength)
{
	struct vhci_hcd *vhci = hcd_to_vhci(hcd);
	uint32_t temp,status;
	unsigned int port;
	int ret = 0;

	port = wIndex &0xff;
	port -= (port>0);

	switch (typeReq) {
	case GetHubDescriptor:
		vhci_hub_descriptor (vhci, (struct usb_hub_descriptor *)buf);
		break;
	case GetHubStatus:
		memset (buf, 0, 4);
		break;
	case GetPortStatus:
		status = 0;

		temp = vhci_readl(vhci,VHCI_PORT_STATUS(port));
		if (temp & VHCI_PORT_ST_C_CONNECTION)
			status |= USB_PORT_STAT_C_CONNECTION << 16;
		if (temp & VHCI_PORT_ST_CONNECTION){
			status |= USB_PORT_STAT_CONNECTION;
			status |= vhci_port_speed(temp);
		}
		if (temp & VHCI_PORT_ST_C_ENABLE)
			status |= USB_PORT_STAT_C_ENABLE << 16;
		if (temp & VHCI_PORT_ST_ENABLE)
			status |= USB_PORT_STAT_ENABLE;

		if (temp & VHCI_PORT_ST_C_RESET)
			status |= USB_PORT_STAT_C_RESET << 16;
		if (temp & VHCI_PORT_ST_RESET)
			status |= USB_PORT_STAT_RESET;

		if (temp & VHCI_PORT_ST_POWER)
			status |= USB_PORT_STAT_POWER;

		put_unaligned_le32(status, buf);
		break;
	case ClearPortFeature:
		vhci_hub_clear_port_feature(vhci,port,wValue);
		break;

	case SetPortFeature:
		vhci_hub_set_port_feature(vhci,port,wValue);
		break;


	}
	return ret;
}

static int vhci_bus_suspend(struct usb_hcd *hcd)
{
	return 0;
}

static int vhci_bus_resume(struct usb_hcd *hcd)
{
	return 0;
}

static void vhci_clear_tt_buffer_complete(struct usb_hcd *hcd,
				struct usb_host_endpoint *ep)
{

}

static void vhci_remove_device(struct usb_hcd *hcd, struct usb_device *dev)
{

}

static const struct hc_driver vhci_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"VHCI Host Controller",
	.hcd_priv_size =	sizeof(struct vhci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			vhci_irq,
	.flags =		HCD_MEMORY | HCD_USB2 | HCD_BH,

	/*
	 * basic lifecycle operations
	 */
	.reset =		vhci_setup,
	.start =		vhci_run,
	.stop =			vhci_stop,
	.shutdown =		vhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		vhci_urb_enqueue,
	.urb_dequeue =		vhci_urb_dequeue,
	.endpoint_disable =	vhci_endpoint_disable,
	.endpoint_reset =	vhci_endpoint_reset,
	.clear_tt_buffer_complete =	vhci_clear_tt_buffer_complete,

	/*
	 * scheduling support
	 */
	.get_frame_number =	vhci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	vhci_hub_status_data,
	.hub_control =		vhci_hub_control,
	.bus_suspend =		vhci_bus_suspend,
	.bus_resume =		vhci_bus_resume,

	/*
	 * device support
	 */
	.free_dev =		vhci_remove_device,
};


int vhci_init_driver(struct hc_driver *drv,
		const struct vhci_driver_overrides *over)
{
	/* Copy the generic table to drv and then apply the overrides */
	*drv = vhci_hc_driver;

	vhci_qtd_cache = kmem_cache_create("vhci_qtd_cache",
		sizeof(struct vhci_qtd), 0, 0, NULL);
	if (!vhci_qtd_cache)
		return -ENOMEM;


	if (over) {
		if (over->reset)
			drv->reset = over->reset;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vhci_init_driver);

