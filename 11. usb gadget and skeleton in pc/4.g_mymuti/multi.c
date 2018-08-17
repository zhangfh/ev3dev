// SPDX-License-Identifier: GPL-2.0+
/*
 * multi.c -- Multifunction Composite driver
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2009 Samsung Electronics
 * Author: Michal Nazarewicz (mina86@mina86.com)
 */


#include <linux/kernel.h>
#include <linux/module.h>

#define DRIVER_DESC		"Multifunction Composite Gadget"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Zhang FangHui");
MODULE_LICENSE("GPL");


#include "f_mass_storage.h"

USB_GADGET_COMPOSITE_OPTIONS();

/***************************** Device Descriptor ****************************/
#define MULTI_VENDOR_NUM	0x1d6b	/* Linux Foundation */
#define MULTI_PRODUCT_NUM	0x0104	/* Multifunction Composite Gadget */

enum {
	__MULTI_NO_CONFIG,
	MULTI_LOOP_CONFIG_NUM,
};


static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */
	.bDeviceClass =		USB_CLASS_MISC /* 0xEF */,
	.bDeviceSubClass =	2,
	.bDeviceProtocol =	1,
	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(MULTI_VENDOR_NUM),
	.idProduct =		cpu_to_le16(MULTI_PRODUCT_NUM),
};

static const struct usb_descriptor_header *otg_desc[2];

enum {
	MULTI_STRING_LOOP_CONFIG_IDX = USB_GADGET_FIRST_AVAIL_IDX,
};

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
	[MULTI_STRING_LOOP_CONFIG_IDX].s = "Multifunction with LOOP",
	{  } /* end of list */
};

static struct usb_gadget_strings *dev_strings[] = {
	&(struct usb_gadget_strings){
		.language	= 0x0409,	/* en-us */
		.strings	= strings_dev,
	},
	NULL,
};


/****************************** Configurations ******************************/
static struct fsg_module_parameters fsg_mod_data = { .stall = 1 };
#ifdef CONFIG_USB_GADGET_DEBUG_FILES
static unsigned int fsg_num_buffers = CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS;
#else
/*
 * Number of buffers we will use.
 * 2 is usually enough for good buffering pipeline
 */
#define fsg_num_buffers	CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS
#endif /* CONFIG_USB_GADGET_DEBUG_FILES */

FSG_MODULE_PARAMETERS(/* no prefix */, fsg_mod_data);

static struct usb_function_instance *fi_loopback;
static struct usb_function_instance *fi_msg;
static struct usb_function *f_loopback;
static struct usb_function *f_msg_loopback;

static int loop_do_config(struct usb_configuration *c)
{
        int ret;

        if (gadget_is_otg(c->cdev->gadget)) {
                c->descriptors = otg_desc;
                c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
        }
        f_loopback = usb_get_function(fi_loopback);
        if (IS_ERR(f_loopback))
                return PTR_ERR(f_loopback);
        ret = usb_add_function(c, f_loopback);
        f_msg_loopback = usb_get_function(fi_msg);
        if (IS_ERR(f_msg_loopback)) {
                ret = PTR_ERR(f_msg_loopback);
                goto err_fsg;
        }
        ret = usb_add_function(c, f_msg_loopback);
        if (ret)
                goto err_run;
        return 0;

err_run:
        usb_put_function(f_msg_loopback);
err_fsg:
        usb_remove_function(c, f_loopback);
        usb_put_function(f_loopback);
        return ret;
}

static __ref int loop_config_register(struct usb_composite_dev *cdev)
{
        static struct usb_configuration config = {
                .bConfigurationValue    = MULTI_LOOP_CONFIG_NUM,
                .bmAttributes           = USB_CONFIG_ATT_SELFPOWER,
        };

        config.label          = strings_dev[MULTI_STRING_LOOP_CONFIG_IDX].s;
        config.iConfiguration = strings_dev[MULTI_STRING_LOOP_CONFIG_IDX].id;

        return usb_add_config(cdev, &config, loop_do_config);
}




/****************************** Gadget Bind ******************************/

static int __ref multi_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	struct fsg_opts *fsg_opts;
	struct fsg_config config;
	int status;

	printk("burns , gadget name %s\n",gadget->name);

	/* set up loop function*/
	fi_loopback = usb_get_function_instance("Loopback");
	if (IS_ERR(fi_loopback)) {
		status = PTR_ERR(fi_loopback);
		goto fail0;
	}

	/* set up mass storage function */
	fi_msg = usb_get_function_instance("mass_storage");
	if (IS_ERR(fi_msg)) {
		status = PTR_ERR(fi_msg);
		goto fail1;
	}
	fsg_config_from_params(&config, &fsg_mod_data, fsg_num_buffers);
	fsg_opts = fsg_opts_from_func_inst(fi_msg);

	fsg_opts->no_configfs = true;
	status = fsg_common_set_num_buffers(fsg_opts->common, fsg_num_buffers);
	if (status)
		goto fail2;

	status = fsg_common_set_cdev(fsg_opts->common, cdev, config.can_stall);
	if (status)
		goto fail_set_cdev;

	fsg_common_set_sysfs(fsg_opts->common, true);
	status = fsg_common_create_luns(fsg_opts->common, &config);
	if (status)
		goto fail_set_cdev;

	fsg_common_set_inquiry_string(fsg_opts->common, config.vendor_name,
				      config.product_name);

	/* allocate string IDs */
	status = usb_string_ids_tab(cdev, strings_dev);
	if (unlikely(status < 0))
		goto fail_string_ids;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	if (gadget_is_otg(gadget) && !otg_desc[0]) {
		struct usb_descriptor_header *usb_desc;

		usb_desc = usb_otg_descriptor_alloc(gadget);
		if (!usb_desc)
			goto fail_string_ids;
		usb_otg_descriptor_init(gadget, usb_desc);
		otg_desc[0] = usb_desc;
		otg_desc[1] = NULL;
	}

	/* register configurations */
	status = loop_config_register(cdev);
	if (unlikely(status < 0))
		goto fail_otg_desc;

	usb_composite_overwrite_options(cdev, &coverwrite);

	/* we're done */
	printk("burns we're done\n");
	dev_info(&gadget->dev, DRIVER_DESC "\n");
	return 0;


	/* error recovery */
fail_otg_desc:
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
fail_string_ids:
	fsg_common_remove_luns(fsg_opts->common);
fail_set_cdev:
	fsg_common_free_buffers(fsg_opts->common);
fail2:
	usb_put_function_instance(fi_msg);
fail1:
        usb_put_function_instance(fi_loopback);
fail0:
        return status;

}

static int multi_unbind(struct usb_composite_dev *cdev)
{
	usb_put_function(f_msg_loopback);
	usb_put_function_instance(fi_msg);
	usb_put_function(f_loopback);
	usb_put_function_instance(fi_loopback);
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;

	return 0;
}


static struct usb_composite_driver multi_driver = {
	.name		= "g_mymulti",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= multi_bind,
	.unbind		= multi_unbind,
	.needs_serial	= 1,
};

module_usb_composite_driver(multi_driver);
