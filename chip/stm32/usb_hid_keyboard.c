/* Copyright 2016 The Chromium OS Authors. All rights reserved.
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
#include "usb_hid_keyboard.h"
#include "usb_hid_hw.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

#define HID_KEYBOARD_REPORT_SIZE  8

/* HID descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_HID_KEYBOARD) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_HID_KEYBOARD,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = USB_HID_SUBCLASS_BOOT,
	.bInterfaceProtocol = USB_HID_PROTOCOL_KEYBOARD,
	.iInterface = 0,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_HID_KEYBOARD, 81) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_HID_KEYBOARD,
	.bmAttributes = 0x03 /* Interrupt endpoint */,
	.wMaxPacketSize = HID_KEYBOARD_REPORT_SIZE,
	.bInterval = 40 /* ms polling interval */
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
	0xC0        /* End Collection */
};

const struct usb_hid_descriptor USB_CUSTOM_DESC(USB_IFACE_HID_KEYBOARD, hid) = {
	.bLength = 9,
	.bDescriptorType = USB_HID_DT_HID,
	.bcdHID = 0x0100,
	.bCountryCode = 0x00, /* Hardware target country */
	.bNumDescriptors = 1,
	.desc = {{
		.bDescriptorType = USB_HID_DT_REPORT,
		.wDescriptorLength = sizeof(report_desc)
	}}
};

static usb_uint hid_ep_buf[HID_KEYBOARD_REPORT_SIZE / 2] __usb_ram;

void set_keyboard_report(uint64_t rpt)
{
	memcpy_to_usbram((void *) usb_sram_addr(hid_ep_buf), &rpt, sizeof(rpt));
	/* enable TX */
	STM32_TOGGLE_EP(USB_EP_HID_KEYBOARD, EP_TX_MASK, EP_TX_VALID, 0);
}

static void hid_keyboard_tx(void)
{
	hid_tx(USB_EP_HID_KEYBOARD);
}

static void hid_keyboard_reset(void)
{
	hid_reset(USB_EP_HID_KEYBOARD, hid_ep_buf, HID_KEYBOARD_REPORT_SIZE);
}

USB_DECLARE_EP(USB_EP_HID_KEYBOARD, hid_keyboard_tx, hid_keyboard_tx,
	       hid_keyboard_reset);

static int hid_keyboard_iface_request(usb_uint *ep0_buf_rx,
				      usb_uint *ep0_buf_tx)
{
	return hid_iface_request(ep0_buf_rx, ep0_buf_tx,
				 report_desc, sizeof(report_desc));
}
USB_DECLARE_IFACE(USB_IFACE_HID_KEYBOARD, hid_keyboard_iface_request)

static int command_hid_kb(int argc, char **argv)
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
DECLARE_CONSOLE_COMMAND(hid_kb, command_hid_kb,
			"[<HID keycode>]",
			"test USB HID driver");
