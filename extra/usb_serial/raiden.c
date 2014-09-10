/*
 * USB Serial module for Raiden USB debug serial console forwarding.
 * SubClass and Protocol allocated in go/usb-ids
 *
 * Copyright (c) 2014 The Chromium OS Authors <chromium-os-dev@chromium.org>
 * Author: Anton Staaf <robotboy@chromium.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

MODULE_LICENSE("GPL");

#define USB_VENDOR_ID_GOOGLE		0x18d1
#define USB_SUBCLASS_GOOGLE_SERIAL	0x50
#define USB_PROTOCOL_GOOGLE_SERIAL	0x01

static struct usb_device_id const ids[] = {
	{ USB_VENDOR_AND_INTERFACE_INFO(USB_VENDOR_ID_GOOGLE,
					USB_CLASS_VENDOR_SPEC,
					USB_SUBCLASS_GOOGLE_SERIAL,
					USB_PROTOCOL_GOOGLE_SERIAL) },
        { 0 }
};

MODULE_DEVICE_TABLE(usb, ids);

static struct usb_serial_driver device =
{
	.driver    = { .owner = THIS_MODULE,
		       .name  = "Google" },
	.id_table  = ids,
	.num_ports = 1,
};

static struct usb_serial_driver * const drivers[] = { &device, NULL };

module_usb_serial_driver(drivers, ids);
