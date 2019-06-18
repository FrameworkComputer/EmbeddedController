/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "link_defs.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_descriptor.h"
#include "usb_hid.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

#define HID_REPORT_SIZE  8

/* HID descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_HID) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_HID_KEYBOARD,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = USB_HID_SUBCLASS_BOOT,
	.bInterfaceProtocol = USB_HID_PROTOCOL_KEYBOARD,
	.iInterface = USB_STR_HID_KEYBOARD_NAME,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_HID, 81) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_HID_KEYBOARD,
	.bmAttributes = 0x03 /* Interrupt endpoint */,
	.wMaxPacketSize = HID_REPORT_SIZE,
	.bInterval = 32 /* ms polling interval */
};
const struct usb_hid_descriptor USB_CUSTOM_DESC(USB_IFACE_HID, hid) = {
	.bLength = 9,
	.bDescriptorType = USB_HID_DT_HID,
	.bcdHID = 0x0100,
	.bCountryCode = 0x00, /* Hardware target country */
	.bNumDescriptors = 1,
	.desc = {
		{.bDescriptorType = USB_HID_DT_REPORT,
		.wDescriptorLength = 45}
	}
};

/* HID : Report Descriptor */
static const uint8_t report_desc[] = {
	0x05, 0x01, /* Usage Page (Generic Desktop) */
	0x09, 0x06, /* Usage (Keyboard) */
	0xA1, 0x01, /* Collection (Application) */
	0x05, 0x07, /* Usage Page (Key Codes) */
	0x19, 0xE0, /* Usage Minimum (224) */
	0x29, 0xE7, /* Usage Maximum (231) */
	0x15, 0x00, /* Logical Minimum (0) */
	0x25, 0x01, /* Logical Maximum (1) */
	0x75, 0x01, /* Report Size (1) */
	0x95, 0x08, /* Report Count (8) */
	0x81, 0x02, /* Input (Data, Variable, Absolute), ;Modifier byte */

	0x95, 0x01, /* Report Count (1) */
	0x75, 0x08, /* Report Size (8) */
	0x81, 0x01, /* Input (Constant), ;Reserved byte */

	0x95, 0x06, /* Report Count (6) */
	0x75, 0x08, /* Report Size (8) */
	0x15, 0x00, /* Logical Minimum (0) */
	0x25, 0x65, /* Logical Maximum(101) */
	0x05, 0x07, /* Usage Page (Key Codes) */
	0x19, 0x00, /* Usage Minimum (0) */
	0x29, 0x65, /* Usage Maximum (101) */
	0x81, 0x00, /* Input (Data, Array), ;Key arrays (6 bytes) */
	0xC0,       /* End Collection */
	0x00        /* Padding */
};

static uint8_t hid_ep_buf[HID_REPORT_SIZE];
static struct g_usb_desc hid_ep_desc;

void set_keyboard_report(uint64_t rpt)
{
	memcpy(hid_ep_buf, &rpt, sizeof(rpt));
	hid_ep_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY | DIEPDMA_IOC |
			    DIEPDMA_TXBYTES(HID_REPORT_SIZE);
	/* enable TX */
	GR_USB_DIEPCTL(USB_EP_HID_KEYBOARD) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
}

static void hid_tx(void)
{
	/* clear IT */
	GR_USB_DIEPINT(USB_EP_HID_KEYBOARD) = 0xffffffff;
	return;
}

static void hid_reset(void)
{
	hid_ep_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_BSY | DIEPDMA_IOC;
	hid_ep_desc.addr = hid_ep_buf;
	GR_USB_DIEPDMA(USB_EP_HID_KEYBOARD) = (uint32_t)&hid_ep_desc;
	GR_USB_DIEPCTL(USB_EP_HID_KEYBOARD) = DXEPCTL_MPS(HID_REPORT_SIZE) |
				     DXEPCTL_USBACTEP | DXEPCTL_EPTYPE_INT |
				     DXEPCTL_TXFNUM(USB_EP_HID_KEYBOARD);
	GR_USB_DAINTMSK |= DAINT_INEP(USB_EP_HID_KEYBOARD);
}

USB_DECLARE_EP(USB_EP_HID_KEYBOARD, hid_tx, hid_tx, hid_reset);

static int hid_iface_request(struct usb_setup_packet *req)
{
	if ((req->bmRequestType & USB_DIR_IN) &&
	    req->bRequest == USB_REQ_GET_DESCRIPTOR &&
	    req->wValue == (USB_HID_DT_REPORT << 8)) {
		/* Setup : HID specific : Get Report descriptor */
		return load_in_fifo(report_desc,
				    MIN(req->wLength,
					sizeof(report_desc)));
	}

	/* Anything else we'll stall */
	return -1;
}
USB_DECLARE_IFACE(USB_IFACE_HID_KEYBOARD, hid_iface_request);

#ifdef CR50_DEV
/* Just for debugging */
static int command_hid(int argc, char **argv)
{
	uint8_t keycode = 0x0a; /* 'G' key */

	if (argc >= 2) {
		char *e;

		keycode = strtoi(argv[1], &e, 16);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	/* press then release the key */
	set_keyboard_report((uint32_t)keycode << 16);
	udelay(50 * MSEC);
	set_keyboard_report(0x000000);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hid, command_hid,
			"[<HID keycode>]",
			"test USB HID driver");
#endif
