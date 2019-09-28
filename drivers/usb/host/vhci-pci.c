/*
 * vhci-pci.c
 *
 *  Created on: Dec 14, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/pci.h>

#include "vhci-hcd.h"

#define DRIVE_NAME "usbhcd-vhci-pci"


static int vhci_pci_reset(struct usb_hcd *hcd);

static struct hc_driver __read_mostly vhci_pci_hc_driver;

static const struct vhci_driver_overrides vhci_pci_overrides __initconst = {
	.reset = vhci_pci_reset,
};



static int vhci_pci_reset(struct usb_hcd *hcd)
{
	struct vhci_hcd *vhci = hcd_to_vhci(hcd);
	vhci->base = hcd->regs;
	printk(KERN_INFO "reg baes %p\n",hcd->regs);
	vhci_setup(hcd);
	return 0;
}



static int vhci_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return usb_hcd_pci_probe(pdev, id);
}

static void vhci_pci_remove(struct pci_dev *pdev)
{
	pci_clear_mwi(pdev);
	usb_hcd_pci_remove(pdev);
}

static const struct pci_device_id vhci_pci_of_match [] = {
	{
		PCI_DEVICE(0x0623,0x0528),
		.driver_data = (unsigned long) &vhci_pci_hc_driver,
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, vhci_pci_of_match);



static struct pci_driver vhci_pci_driver = {
	.name =		DRIVE_NAME,
	.id_table =	vhci_pci_of_match,

	.probe =	vhci_pci_probe,
	.remove =	vhci_pci_remove,
};


static int __init vhci_pci_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	vhci_init_driver(&vhci_pci_hc_driver, &vhci_pci_overrides);

	return pci_register_driver(&vhci_pci_driver);
}
module_init(vhci_pci_init);

static void __exit vhci_pci_cleanup(void)
{
	pci_unregister_driver(&vhci_pci_driver);
}
module_exit(vhci_pci_cleanup);

MODULE_DEVICE_TABLE(pci, vhci_pci_of_match);
MODULE_ALIAS("pci:" DRIVE_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("USB VHCI PCI Driver");
MODULE_LICENSE("GPL v2");
