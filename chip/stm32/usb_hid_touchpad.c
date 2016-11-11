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
#include "usb_hid_hw.h"
#include "usb_hid_touchpad.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

#define HID_TOUCHPAD_REPORT_SIZE  sizeof(struct usb_hid_touchpad_report)

/*
 * Touchpad EP interval: Make sure this value is smaller than the typical
 * interrupt interval from the trackpad.
 */
#define HID_TOUCHPAD_EP_INTERVAL_MS 8 /* ms */

/* HID descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_HID_TOUCHPAD) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_HID_TOUCHPAD,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_HID_TOUCHPAD, 81) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_HID_TOUCHPAD,
	.bmAttributes = 0x03 /* Interrupt endpoint */,
	.wMaxPacketSize = HID_TOUCHPAD_REPORT_SIZE,
	.bInterval = HID_TOUCHPAD_EP_INTERVAL_MS /* polling interval */
};

/*
 * HID: Report Descriptor
 * TODO(crosbug.com/p/59083): There are ways to reduce flash usage, as the
 * Finger Usage is repeated 5 times.
 * TODO(crosbug.com/p/59083): Touchpad specific values should be probed from
 * touchpad itself.
 */
static const uint8_t report_desc[] = {
	0x05, 0x0D,        /* Usage Page (Digitizer) */
	0x09, 0x04,        /* Usage (Touch Screen) */
	0xA1, 0x01,        /* Collection (Application) */
	0x85, 0x01,        /*   Report ID (1, Touch) */
	/* Finger 0 */
	0x09, 0x22,        /*   Usage (Finger) */
	0xA1, 0x02,        /*   Collection (Logical) */
	0x09, 0x42,        /*     Usage (Tip Switch) */
	0x15, 0x00,        /*     Logical Minimum (0) */
	0x25, 0x01,        /*     Logical Maximum (1) */
	0x75, 0x01,        /*     Report Size (1) */
	0x95, 0x01,        /*     Report Count (1) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x32,        /*     Usage (In Range) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x75, 0x06,        /*     Report Size (6) */
	0x09, 0x51,        /*     Usage (0x51) Contact identifier */
	0x25, 0x1F,        /*     Logical Maximum (31) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x09, 0x48,        /*     Usage (WIDTH) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x49,        /*     Usage (HEIGHT) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x01,        /*     Usage Page (Generic Desktop Ctrls) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x55, 0x0E,        /*     Unit Exponent (-2) */
	0x65, 0x11,        /*     Unit (System: SI Linear, Length: cm) */
	0x09, 0x30,        /*     Usage (X) */
	/* FIXME: Physical/logical dimensions should come from trackpad info */
	0x35, 0x00,        /*     Physical Minimum (0) */
	0x26, 0x86, 0x0C,  /*     Logical Maximum (3206) */
	0x46, 0xF8, 0x03,  /*     Physical Maximum (10.16 cm) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x26, 0xf7, 0x06,  /*     Logical Maximum (1783) */
	0x46, 0x36, 0x02,  /*     Physical Maximum (5.66 cm) */
	0x09, 0x31,        /*     Usage (Y) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x08,        /*     Report Size (8) */
	0x09, 0x30,        /*     Usage (Tip pressure) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0xC0,              /*   End Collection */
	/* Finger 1 */
	0x09, 0x22,        /*   Usage (Finger) */
	0xA1, 0x02,        /*   Collection (Logical) */
	0x09, 0x42,        /*     Usage (Tip Switch) */
	0x15, 0x00,        /*     Logical Minimum (0) */
	0x25, 0x01,        /*     Logical Maximum (1) */
	0x75, 0x01,        /*     Report Size (1) */
	0x95, 0x01,        /*     Report Count (1) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x32,        /*     Usage (In Range) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x75, 0x06,        /*     Report Size (6) */
	0x09, 0x51,        /*     Usage (0x51) Contact identifier */
	0x25, 0x1F,        /*     Logical Maximum (31) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x09, 0x48,        /*     Usage (WIDTH) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x49,        /*     Usage (HEIGHT) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x01,        /*     Usage Page (Generic Desktop Ctrls) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x55, 0x0E,        /*     Unit Exponent (-2) */
	0x65, 0x11,        /*     Unit (System: SI Linear, Length: cm) */
	0x09, 0x30,        /*     Usage (X) */
	/* FIXME: Physical/logical dimensions should come from trackpad info */
	0x35, 0x00,        /*     Physical Minimum (0) */
	0x26, 0x86, 0x0C,  /*     Logical Maximum (3206) */
	0x46, 0xF8, 0x03,  /*     Physical Maximum (10.16 cm) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x26, 0xf7, 0x06,  /*     Logical Maximum (1783) */
	0x46, 0x36, 0x02,  /*     Physical Maximum (5.66 cm) */
	0x09, 0x31,        /*     Usage (Y) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x08,        /*     Report Size (8) */
	0x09, 0x30,        /*     Usage (Tip pressure) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0xC0,              /*   End Collection */
	/* Finger 2 */
	0x09, 0x22,        /*   Usage (Finger) */
	0xA1, 0x02,        /*   Collection (Logical) */
	0x09, 0x42,        /*     Usage (Tip Switch) */
	0x15, 0x00,        /*     Logical Minimum (0) */
	0x25, 0x01,        /*     Logical Maximum (1) */
	0x75, 0x01,        /*     Report Size (1) */
	0x95, 0x01,        /*     Report Count (1) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x32,        /*     Usage (In Range) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x75, 0x06,        /*     Report Size (6) */
	0x09, 0x51,        /*     Usage (0x51) Contact identifier */
	0x25, 0x1F,        /*     Logical Maximum (31) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x09, 0x48,        /*     Usage (WIDTH) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x49,        /*     Usage (HEIGHT) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x01,        /*     Usage Page (Generic Desktop Ctrls) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x55, 0x0E,        /*     Unit Exponent (-2) */
	0x65, 0x11,        /*     Unit (System: SI Linear, Length: cm) */
	0x09, 0x30,        /*     Usage (X) */
	/* FIXME: Physical/logical dimensions should come from trackpad info */
	0x35, 0x00,        /*     Physical Minimum (0) */
	0x26, 0x86, 0x0C,  /*     Logical Maximum (3206) */
	0x46, 0xF8, 0x03,  /*     Physical Maximum (10.16 cm) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x26, 0xf7, 0x06,  /*     Logical Maximum (1783) */
	0x46, 0x36, 0x02,  /*     Physical Maximum (5.66 cm) */
	0x09, 0x31,        /*     Usage (Y) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x08,        /*     Report Size (8) */
	0x09, 0x30,        /*     Usage (Tip pressure) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0xC0,              /*   End Collection */
	/* Finger 3 */
	0x09, 0x22,        /*   Usage (Finger) */
	0xA1, 0x02,        /*   Collection (Logical) */
	0x09, 0x42,        /*     Usage (Tip Switch) */
	0x15, 0x00,        /*     Logical Minimum (0) */
	0x25, 0x01,        /*     Logical Maximum (1) */
	0x75, 0x01,        /*     Report Size (1) */
	0x95, 0x01,        /*     Report Count (1) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x32,        /*     Usage (In Range) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x75, 0x06,        /*     Report Size (6) */
	0x09, 0x51,        /*     Usage (0x51) Contact identifier */
	0x25, 0x1F,        /*     Logical Maximum (31) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x09, 0x48,        /*     Usage (WIDTH) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x49,        /*     Usage (HEIGHT) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x01,        /*     Usage Page (Generic Desktop Ctrls) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x55, 0x0E,        /*     Unit Exponent (-2) */
	0x65, 0x11,        /*     Unit (System: SI Linear, Length: cm) */
	0x09, 0x30,        /*     Usage (X) */
	/* FIXME: Physical/logical dimensions should come from trackpad info */
	0x35, 0x00,        /*     Physical Minimum (0) */
	0x26, 0x86, 0x0C,  /*     Logical Maximum (3206) */
	0x46, 0xF8, 0x03,  /*     Physical Maximum (10.16 cm) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x26, 0xf7, 0x06,  /*     Logical Maximum (1783) */
	0x46, 0x36, 0x02,  /*     Physical Maximum (5.66 cm) */
	0x09, 0x31,        /*     Usage (Y) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x08,        /*     Report Size (8) */
	0x09, 0x30,        /*     Usage (Tip pressure) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0xC0,              /*   End Collection */
	/* Finger 4 */
	0x09, 0x22,        /*   Usage (Finger) */
	0xA1, 0x02,        /*   Collection (Logical) */
	0x09, 0x42,        /*     Usage (Tip Switch) */
	0x15, 0x00,        /*     Logical Minimum (0) */
	0x25, 0x01,        /*     Logical Maximum (1) */
	0x75, 0x01,        /*     Report Size (1) */
	0x95, 0x01,        /*     Report Count (1) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x32,        /*     Usage (In Range) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x75, 0x06,        /*     Report Size (6) */
	0x09, 0x51,        /*     Usage (0x51) Contact identifier */
	0x25, 0x1F,        /*     Logical Maximum (31) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x09, 0x48,        /*     Usage (WIDTH) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x09, 0x49,        /*     Usage (HEIGHT) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x01,        /*     Usage Page (Generic Desktop Ctrls) */
	0x75, 0x0C,        /*     Report Size (12) */
	0x55, 0x0E,        /*     Unit Exponent (-2) */
	0x65, 0x11,        /*     Unit (System: SI Linear, Length: cm) */
	0x09, 0x30,        /*     Usage (X) */
	/* FIXME: Physical/logical dimensions should come from trackpad info */
	0x35, 0x00,        /*     Physical Minimum (0) */
	0x26, 0x86, 0x0C,  /*     Logical Maximum (3206) */
	0x46, 0xF8, 0x03,  /*     Physical Maximum (10.16 cm) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x26, 0xf7, 0x06,  /*     Logical Maximum (1783) */
	0x46, 0x36, 0x02,  /*     Physical Maximum (5.66 cm) */
	0x09, 0x31,        /*     Usage (Y) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0x05, 0x0D,        /*     Usage Page (Digitizer) */
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */
	0x75, 0x08,        /*     Report Size (8) */
	0x09, 0x30,        /*     Usage (Tip pressure) */
	0x81, 0x02,        /*     Input (Data,Var,Abs) */
	0xC0,              /*   End Collection */
	/* Contact count */
	0x05, 0x0D,        /*   Usage Page (Digitizer) */
	0x09, 0x54,        /*   Usage (Contact count) */
	0x75, 0x07,        /*   Report Size (7) */
	0x95, 0x01,        /*   Report Count (1) */
	0x81, 0x02,        /*   Input (Data,Var,Abs) */
	/* Button */
	0x05, 0x01,        /*   Usage Page (Generic Desktop Ctrls) */
	0x05, 0x09,        /*   Usage (Button) */
	0x19, 0x01,        /*   Usage Minimum (0x01) */
	0x29, 0x01,        /*   Usage Maximum (0x01) */
	0x15, 0x00,        /*   Logical Minimum (0) */
	0x25, 0x01,        /*   Logical Maximum (1) */
	0x75, 0x01,        /*   Report Size (1) */
	0x95, 0x01,        /*   Report Count (1) */
	0x81, 0x02,        /*   Input (Data,Var,Abs) */
	0xC0,              /* End Collection */
};

const struct usb_hid_descriptor USB_CUSTOM_DESC(USB_IFACE_HID_TOUCHPAD, hid) = {
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

static usb_uint hid_ep_buf[DIV_ROUND_UP(HID_TOUCHPAD_REPORT_SIZE, 2)] __usb_ram;

void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
	/*
	 * Endpoint is busy. This should rarely happen as we make sure that
	 * the trackpad interrupt period >= USB interrupt period.
	 *
	 * TODO(crosbug.com/p/59083): Figure out how to handle USB suspend.
	 */
	int timeout = 20; /* Wait up to 5 EP intervals. */

	while ((STM32_USB_EP(USB_EP_HID_TOUCHPAD) & EP_TX_MASK)
			== EP_TX_VALID) {
		msleep(DIV_ROUND_UP(HID_TOUCHPAD_EP_INTERVAL_MS, 4));
		if (!--timeout)
			return;
	}

	memcpy_to_usbram((void *) usb_sram_addr(hid_ep_buf),
			 report, sizeof(*report));
	/* enable TX */
	STM32_TOGGLE_EP(USB_EP_HID_TOUCHPAD, EP_TX_MASK, EP_TX_VALID, 0);
}

static void hid_touchpad_tx(void)
{
	hid_tx(USB_EP_HID_TOUCHPAD);
}

static void hid_touchpad_reset(void)
{
	hid_reset(USB_EP_HID_TOUCHPAD, hid_ep_buf, HID_TOUCHPAD_REPORT_SIZE);
}

USB_DECLARE_EP(USB_EP_HID_TOUCHPAD, hid_touchpad_tx, hid_touchpad_tx,
	       hid_touchpad_reset);

static int hid_touchpad_iface_request(usb_uint *ep0_buf_rx,
				      usb_uint *ep0_buf_tx)
{
	return hid_iface_request(ep0_buf_rx, ep0_buf_tx,
				 report_desc, sizeof(report_desc));
}
USB_DECLARE_IFACE(USB_IFACE_HID_TOUCHPAD, hid_touchpad_iface_request)
