/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
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
#include "usb.h"
#include "usb_hid.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

#define HID_REPORT_SIZE  8

/* HID descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_HID) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_HID,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = USB_HID_SUBCLASS_BOOT,
	.bInterfaceProtocol = USB_HID_PROTOCOL_KEYBOARD,
	.iInterface = 0,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_HID, 81) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_HID,
	.bmAttributes = 0x03 /* Interrupt endpoint */,
	.wMaxPacketSize = HID_REPORT_SIZE,
	.bInterval = 40 /* ms polling interval */
};
const struct usb_hid_descriptor USB_CUSTOM_DESC(USB_IFACE_HID, hid) = {
	.bLength = 9,
	.bDescriptorType = USB_HID_DT_HID,
	.bcdHID = 0x0100,
	.bCountryCode = 0x00, /* Hardware target country */
	.bNumDescriptors = 1,
	.desc = {{
		.bDescriptorType = USB_HID_DT_REPORT,
		.wDescriptorLength = 45
	}}
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

static usb_uint hid_ep_buf[HID_REPORT_SIZE / 2] __usb_ram;

void set_keyboard_report(uint64_t rpt)
{
	memcpy_to_usbram(hid_ep_buf, &rpt, sizeof(rpt));
	/* enable TX */
	STM32_TOGGLE_EP(USB_EP_HID, EP_TX_MASK, EP_TX_VALID, 0);
}

static void hid_tx(void)
{
	uint16_t ep = STM32_USB_EP(USB_EP_HID);
	/* clear IT */
	STM32_USB_EP(USB_EP_HID) = (ep & EP_MASK);
	return;
}

static void hid_reset(void)
{
	/* HID interrupt endpoint 1 */
	btable_ep[USB_EP_HID].tx_addr = usb_sram_addr(hid_ep_buf);
	btable_ep[USB_EP_HID].tx_count = 8;
	hid_ep_buf[0] = 0;
	hid_ep_buf[1] = 0;
	hid_ep_buf[2] = 0;
	hid_ep_buf[3] = 0;
	STM32_USB_EP(USB_EP_HID) = (USB_EP_HID << 0) /*Endpoint Address*/ |
				   (3 << 4) /* TX Valid */ |
				   (3 << 9) /* interrupt EP */ |
				   (0 << 12) /* RX Disabled */;
}

USB_DECLARE_EP(USB_EP_HID, hid_tx, hid_tx, hid_reset);

static int hid_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx)
{
	if ((ep0_buf_rx[0] == (USB_DIR_IN | USB_RECIP_INTERFACE |
			      (USB_REQ_GET_DESCRIPTOR << 8))) &&
			      (ep0_buf_rx[1] == (USB_HID_DT_REPORT << 8))) {
		/* Setup : HID specific : Get Report descriptor */
		memcpy_to_usbram(ep0_buf_tx, report_desc,
				 sizeof(report_desc));
		btable_ep[0].tx_count = MIN(ep0_buf_rx[3],
				   sizeof(report_desc));
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID,
			  EP_STATUS_OUT);
		CPRINTF("RPT %04x[l %04x]\n", STM32_USB_EP(0),
			ep0_buf_rx[3]);
		return 0;
	}

	return 1;
}
USB_DECLARE_IFACE(USB_IFACE_HID, hid_iface_request)

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
	udelay(50000);
	set_keyboard_report(0x000000);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hid, command_hid,
			"[<HID keycode>]",
			"test USB HID driver",
			NULL);
