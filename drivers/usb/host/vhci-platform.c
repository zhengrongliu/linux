/*
 * vhci-platform.c
 *
 *  Created on: Nov 24, 2018
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
#include <linux/platform_device.h>

#include "vhci-hcd.h"

#define DRIVE_NAME "usbhcd-vhci-plat"


static int vhci_platform_reset(struct usb_hcd *hcd);

static struct hc_driver __read_mostly vhci_platform_hc_driver;

static const struct vhci_driver_overrides vhci_platform_overrides __initconst = {
	.reset = vhci_platform_reset,
};



static int vhci_platform_reset(struct usb_hcd *hcd)
{
	vhci_setup(hcd);
	return 0;
}

static int vhci_platform_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct vhci_hcd *vhci;
	struct usb_hcd *hcd;
	int ret,irq;

	if (usb_disabled())
		return -ENODEV;

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Error: DMA mask configuration failed\n");
		return ret;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "no irq provided");
		return ret;
	}

	irq = ret;

	hcd = usb_create_hcd(&vhci_platform_hc_driver, &pdev->dev,
			     dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;


	vhci = hcd_to_vhci(hcd);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vhci->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vhci->base)){
		dev_err(&pdev->dev,"failed to get io memory base");
		ret = PTR_ERR(vhci->base);
		goto failed_get_iomap;
	}

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if(ret)
		goto failed_add_hcd;

	platform_set_drvdata(pdev, hcd);
	return 0;


failed_add_hcd:
failed_get_iomap:

	usb_put_hcd(hcd);
	return ret;
}

static int vhci_platform_remove(struct platform_device *pdev)
{
	struct vhci_hcd *vhci;
	struct usb_hcd *hcd;

	vhci = platform_get_drvdata(pdev);

	hcd = vhci_to_hcd(vhci);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	return 0;
}



static const struct of_device_id vhci_platform_of_match[] = {
	{ .compatible = "virtio,platform-vhci",},
};




static struct platform_driver vhci_platform_driver = {
	.driver = {
		.name   = DRIVE_NAME,
		.of_match_table = of_match_ptr(vhci_platform_of_match),
	},
	.probe          = vhci_platform_probe,
	.remove         = vhci_platform_remove,
};

static int __init vhci_platform_init(void)
{
	int ret;

	if (usb_disabled())
		return -ENODEV;

	ret = vhci_init_driver(&vhci_platform_hc_driver, &vhci_platform_overrides);
	if(ret)
		return ret;

	return platform_driver_register(&vhci_platform_driver);
}
module_init(vhci_platform_init);

static void __exit vhci_platform_exit(void)
{
	platform_driver_unregister(&vhci_platform_driver);
}
module_exit(vhci_platform_exit);



MODULE_DEVICE_TABLE(of, vhci_platform_of_match);
MODULE_ALIAS("platform:" DRIVE_NAME);
MODULE_AUTHOR("Zhengrong Liu<towering@126.com>");
MODULE_DESCRIPTION("USB VHCI Platform Driver");
MODULE_LICENSE("GPL v2");

