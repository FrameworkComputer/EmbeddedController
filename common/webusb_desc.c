/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* WebUSB platform descriptor */

#include "common.h"
#include "usb_descriptor.h"
#include "util.h"

#ifndef CONFIG_USB_BOS
#error "CONFIG_USB_BOS must be defined to use WebUSB descriptor"
#endif

const void *webusb_url = USB_URL_DESC(HTTPS, CONFIG_WEBUSB_URL);

/*
 * Platform Descriptor in the device Binary Object Store
 * as defined by USB 3.1 spec chapter 9.6.2.
 */
static struct {
	struct usb_bos_hdr_descriptor bos;
	struct usb_platform_descriptor platform;
} bos_desc = {
	.bos = {
		.bLength = USB_DT_BOS_SIZE,
		.bDescriptorType = USB_DT_BOS,
		.wTotalLength = (USB_DT_BOS_SIZE + USB_DT_PLATFORM_SIZE),
		.bNumDeviceCaps = 1,  /* platform caps */
	},
	.platform = {
		.bLength = USB_DT_PLATFORM_SIZE,
		.bDescriptorType = USB_DT_DEVICE_CAPABILITY,
		.bDevCapabilityType = USB_DC_DTYPE_PLATFORM,
		.bReserved = 0,
		.PlatformCapUUID = USB_PLAT_CAP_WEBUSB,
		.bcdVersion = 0x0100,
		.bVendorCode = 0x01,
		.iLandingPage = 1,
	},
};

const struct bos_context bos_ctx = {
	.descp = (void *)&bos_desc,
	.size = sizeof(bos_desc),
};
