/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "clock.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "link_defs.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "usb_hid_hw.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

struct __attribute__((__packed__)) usb_hid_keyboard_report {
	uint8_t modifiers; /* bitmap of modifiers 224-231 */
	uint8_t reserved; /* 0x0 */
	uint8_t keys[6];
};

#define HID_KEYBOARD_REPORT_SIZE sizeof(struct usb_hid_keyboard_report)

#define HID_KEYBOARD_EP_INTERVAL_MS 32 /* ms */

/* Modifiers keycode range */
#define HID_KEYBOARD_MODIFIER_LOW 0xe0
#define HID_KEYBOARD_MODIFIER_HIGH 0xe7

/* The standard Chrome OS keyboard matrix table. See HUT 1.12v2 Table 12 and
 * https://www.w3.org/TR/DOM-Level-3-Events-code .
 */
const uint8_t keycodes[KEYBOARD_ROWS][KEYBOARD_COLS] = {
	{ 0x00, 0xe3, 0x3a, 0x05, 0x43, 0x87, 0x11, 0x00, 0x2e,
	  0x00, 0xe6, 0x00, 0x00 },
	{ 0x00, 0x29, 0x3d, 0x0a, 0x40, 0x00, 0x0b, 0x00, 0x34,
	  0x42, 0x00, 0x2a, 0x90 },
	{ 0xe0, 0x2b, 0x3c, 0x17, 0x3f, 0x30, 0x1c, 0x64, 0x2F,
	  0x41, 0x89, 0x00, 0x00 },
	{ 0x00, 0x35, 0x3b, 0x22, 0x3e, 0x00, 0x23, 0x00, 0x2d,
	  0x68, 0x00, 0x31, 0x91 },
	{ 0xe4, 0x04, 0x07, 0x09, 0x16, 0x0e, 0x0d, 0x00, 0x33,
	  0x0f, 0x31, 0x28, 0x00 },
	{ 0x00, 0x1d, 0x06, 0x19, 0x1b, 0x36, 0x10, 0xe1, 0x38,
	  0x37, 0x00, 0x2c, 0x00 },
	{ 0x00, 0x1e, 0x20, 0x21, 0x1f, 0x25, 0x24, 0x00, 0x27,
	  0x26, 0xe2, 0x51, 0x4f },
	{ 0x00, 0x14, 0x08, 0x15, 0x1a, 0x0c, 0x18, 0xe5, 0x13,
	  0x12, 0x00, 0x52, 0x50 }
};

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
	.bInterval = HID_KEYBOARD_EP_INTERVAL_MS /* ms polling interval */
};

/* HID : Report Descriptor */
static const uint8_t report_desc[] = {
	0x05, 0x01, /* Usage Page (Generic Desktop) */
	0x09, 0x06, /* Usage (Keyboard) */
	0xA1, 0x01, /* Collection (Application) */

	/* Modifiers */
	0x05, 0x07, /* Usage Page (Key Codes) */
	0x19, HID_KEYBOARD_MODIFIER_LOW, /* Usage Minimum */
	0x29, HID_KEYBOARD_MODIFIER_HIGH, /* Usage Maximum */
	0x15, 0x00, /* Logical Minimum (0) */
	0x25, 0x01, /* Logical Maximum (1) */
	0x75, 0x01, /* Report Size (1) */
	0x95, 0x08, /* Report Count (8) */
	0x81, 0x02, /* Input (Data, Variable, Absolute), ;Modifier byte */

	0x95, 0x01, /* Report Count (1) */
	0x75, 0x08, /* Report Size (8) */
	0x81, 0x01, /* Input (Constant), ;Reserved byte */

	/* Normal keys */
	0x95, 0x06, /* Report Count (6) */
	0x75, 0x08, /* Report Size (8) */
	0x15, 0x00, /* Logical Minimum (0) */
	0x25, 0xa4, /* Logical Maximum (164) */
	0x05, 0x07, /* Usage Page (Key Codes) */
	0x19, 0x00, /* Usage Minimum (0) */
	0x29, 0xa4, /* Usage Maximum (164) */
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

#define EP_BUF_SIZE DIV_ROUND_UP(HID_KEYBOARD_REPORT_SIZE, 2)

static usb_uint hid_ep_buf[2][EP_BUF_SIZE] __usb_ram;
static volatile int hid_current_buf;

static volatile int hid_ep_data_ready;

static struct usb_hid_keyboard_report report;

static void write_keyboard_report(void)
{
	/* Prevent the interrupt handler from sending the data (which would use
	 * an incomplete buffer).
	 */
	hid_ep_data_ready = 0;
	hid_current_buf = hid_current_buf ? 0 : 1;
	memcpy_to_usbram((void *) usb_sram_addr(hid_ep_buf[hid_current_buf]),
			 &report, sizeof(report));

	/* Tell the interrupt handler to send the next buffer. */
	hid_ep_data_ready = 1;
	if ((STM32_USB_EP(USB_EP_HID_KEYBOARD) & EP_TX_MASK) == EP_TX_VALID) {
		/* Endpoint is busy: we sneak in an address change to give us a
		 * chance to send the most updated report. However, there is no
		 * guarantee that this buffer is the one actually sent, so we
		 * keep hid_ep_data_ready = 1, which will send a duplicated
		 * report.
		 */
		btable_ep[USB_EP_HID_KEYBOARD].tx_addr =
			usb_sram_addr(hid_ep_buf[hid_current_buf]);
		hid_ep_data_ready = 1;
	} else if (atomic_read_clear(&hid_ep_data_ready)) {
		/* Endpoint is not busy, and interrupt handler did not just
		 * send our last buffer: swap buffer, enable TX.
		 */
		btable_ep[USB_EP_HID_KEYBOARD].tx_addr =
			usb_sram_addr(hid_ep_buf[hid_current_buf]);
		STM32_TOGGLE_EP(USB_EP_HID_KEYBOARD, EP_TX_MASK,
				EP_TX_VALID, 0);
	}
}

static void hid_keyboard_tx(void)
{
	hid_tx(USB_EP_HID_KEYBOARD);
	if (hid_ep_data_ready) {
		/* swap buffer, enable TX */
		btable_ep[USB_EP_HID_KEYBOARD].tx_addr =
			usb_sram_addr(hid_ep_buf[hid_current_buf]);
		STM32_TOGGLE_EP(USB_EP_HID_KEYBOARD, EP_TX_MASK,
				EP_TX_VALID, 0);
	}
	hid_ep_data_ready = 0;
}

static void hid_keyboard_reset(void)
{
	hid_reset(USB_EP_HID_KEYBOARD, hid_ep_buf[hid_current_buf],
		  HID_KEYBOARD_REPORT_SIZE);
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

void keyboard_clear_buffer(void)
{
	memset(&report, 0, sizeof(report));
	write_keyboard_report();
}

void keyboard_state_changed(int row, int col, int is_pressed)
{
	int i;
	uint8_t mask;
	uint8_t keycode = keycodes[row][col];

	if (!keycode) {
		CPRINTF("Unknown key at %d/%d\n", row, col);
		return;
	}

	if (keycode >= HID_KEYBOARD_MODIFIER_LOW &&
	    keycode <= HID_KEYBOARD_MODIFIER_HIGH) {
		mask = 0x01 << (keycode - HID_KEYBOARD_MODIFIER_LOW);
		if (is_pressed)
			report.modifiers |= mask;
		else
			report.modifiers &= ~mask;

		write_keyboard_report();
		return;
	}

	if (is_pressed) {
		/* Add keycode to the list of keys */
		for (i = 0; i < ARRAY_SIZE(report.keys); i++) {
			/* Is key already pressed? */
			if (report.keys[i] == keycode)
				return;
			if (report.keys[i] == 0) {
				report.keys[i] = keycode;
				write_keyboard_report();
				return;
			}
		}
		/* Too many keys, ignoring. */
	} else {
		/* Remove keycode from the list of keys */
		for (i = 0; i < ARRAY_SIZE(report.keys); i++) {
			if (report.keys[i] == keycode) {
				report.keys[i] = 0;
				write_keyboard_report();
				return;
			}
		}
		/* Couldn't find the key... */
	}
}
