/*
 * vhci-hcd.h
 *
 *  Created on: Nov 24, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */

#ifndef DRIVERS_USB_HOST_VHCI_HCD_H_
#define DRIVERS_USB_HOST_VHCI_HCD_H_

#include <linux/io.h>
#include <linux/spinlock.h>

#include "vhci.h"

struct vhci_hcd{
	void __iomem *base;

	int max_ports;

	void *ring_base;

	spinlock_t lock;

	struct list_head ready;
	void **qtd_table;

	int free_node_count;
	int free_node_head;
};

struct vhci_driver_overrides {
	int (*reset)(struct usb_hcd *hcd);
	int (*start)(struct usb_hcd *hcd);
};

#define vhci_dbg(vhci, fmt, args...) \
	dev_dbg(vhci_to_hcd(vhci)->self.controller, fmt, ## args)
#define vhci_err(vhci, fmt, args...) \
	dev_err(vhci_to_hcd(vhci)->self.controller, fmt, ## args)
#define vhci_info(vhci, fmt, args...) \
	dev_info(vhci_to_hcd(vhci)->self.controller, fmt, ## args)
#define vhci_warn(vhci, fmt, args...) \
	dev_warn(vhci_to_hcd(vhci)->self.controller, fmt, ## args)



static inline struct vhci_hcd *hcd_to_vhci(struct usb_hcd *hcd)
{
	return (struct vhci_hcd *) (hcd->hcd_priv);
}

static inline struct usb_hcd *vhci_to_hcd(struct vhci_hcd *vhci)
{
	return container_of((void *) vhci, struct usb_hcd, hcd_priv);
}

static inline unsigned int vhci_readl(const struct vhci_hcd *vhci,unsigned int reg)
{
	return readl(vhci->base + reg);
}

static inline void vhci_writel(const struct vhci_hcd *vhci,
		const unsigned int val, unsigned int reg)
{
	writel(val,vhci->base + reg);
}

int vhci_setup (struct usb_hcd *hcd);
int vhci_init_driver(struct hc_driver *drv,
		      const struct vhci_driver_overrides *over);

#endif /* DRIVERS_USB_HOST_VHCI_HCD_H_ */
