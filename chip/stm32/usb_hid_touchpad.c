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
#include "hwtimer.h"
#include "link_defs.h"
#include "queue.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_api.h"
#include "usb_descriptor.h"
#include "usb_hw.h"
#include "usb_hid.h"
#include "usb_hid_hw.h"
#include "usb_hid_touchpad.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static const int touchpad_debug;

static struct queue const report_queue = QUEUE_NULL(8,
						struct usb_hid_touchpad_report);
static struct mutex report_queue_mutex;

#define HID_TOUCHPAD_REPORT_SIZE  sizeof(struct usb_hid_touchpad_report)

/*
 * Touchpad EP interval: Make sure this value is smaller than the typical
 * interrupt interval from the trackpad.
 */
#define HID_TOUCHPAD_EP_INTERVAL_MS 2 /* ms */

/* Discard TP events older than this time */
#define EVENT_DISCARD_MAX_TIME (1 * SECOND)

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

#define FINGER_USAGE \
	0x05, 0x0D,        /*   Usage Page (Digitizer) */ \
	0x09, 0x22,        /*   Usage (Finger) */ \
	0xA1, 0x02,        /*   Collection (Logical) */ \
	0x09, 0x42,        /*     Usage (Tip Switch) */ \
	0x15, 0x00,        /*     Logical Minimum (0) */ \
	0x25, 0x01,        /*     Logical Maximum (1) */ \
	0x75, 0x01,        /*     Report Size (1) */ \
	0x95, 0x01,        /*     Report Count (1) */ \
	0x81, 0x02,        /*     Input (Data,Var,Abs) */ \
	0x09, 0x32,        /*     Usage (In Range) */ \
	0x81, 0x02,        /*     Input (Data,Var,Abs) */ \
	0x75, 0x04,        /*     Report Size (4) */ \
	0x09, 0x51,        /*     Usage (0x51) Contact identifier */ \
	0x25, 0x0F,        /*     Logical Maximum (15) */ \
	0x81, 0x02,        /*     Input (Data,Var,Abs) */ \
	0x05, 0x0D,        /*     Usage Page (Digitizer) */ \
	0x26, 0xFF, 0x03,  /*     Logical Maximum (1023) */ \
	0x75, 0x0A,        /*     Report Size (10) */ \
	0x09, 0x30,        /*     Usage (Tip pressure) */ \
	0x81, 0x02,        /*     Input (Data,Var,Abs) */ \
	0x26, 0xFF, 0x00,  /*     Logical Maximum (255) */ \
	0x75, 0x0C,        /*     Report Size (12) */ \
	0x09, 0x48,        /*     Usage (WIDTH) */ \
	0x81, 0x02,        /*     Input (Data,Var,Abs) */ \
	0x09, 0x49,        /*     Usage (HEIGHT) */ \
	0x81, 0x02,        /*     Input (Data,Var,Abs) */ \
	0x05, 0x01,        /*     Usage Page (Generic Desktop Ctrls) */ \
	0x75, 0x0C,        /*     Report Size (12) */ \
	0x55, 0x0E,        /*     Unit Exponent (-2) */ \
	0x65, 0x11,        /*     Unit (System: SI Linear, Length: cm) */ \
	0x09, 0x30,        /*     Usage (X) */ \
	0x35, 0x00,        /*     Physical Minimum (0) */ \
	0x26, (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X & 0xff), \
	      (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X >> 8), \
	                   /*     Logical Maximum */ \
	0x46, (CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X & 0xff), \
	      (CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X >> 8), \
	                   /*     Physical Maximum (tenth of mm) */ \
	0x81, 0x02,        /*     Input (Data,Var,Abs) */ \
	0x26, (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y & 0xff), \
	      (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y >> 8), \
	                   /*     Logical Maximum */ \
	0x46, (CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y & 0xff), \
	      (CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y >> 8), \
	                   /*     Physical Maximum (tenth of mm) */ \
	0x09, 0x31,        /*     Usage (Y) */ \
	0x81, 0x02,        /*     Input (Data,Var,Abs) */ \
	0xC0               /*   End Collection */

/*
 * HID: Report Descriptor
 * TODO(b/35582031): There are ways to reduce flash usage, as the
 * Finger Usage is repeated 5 times.
 */
static const uint8_t report_desc[] = {
	0x05, 0x0D,        /* Usage Page (Digitizer) */
	0x09, 0x04,        /* Usage (Touch Screen) */
	0xA1, 0x01,        /* Collection (Application) */
	0x85, 0x01,        /*   Report ID (1, Touch) */
	/* Finger 0 */
	FINGER_USAGE,
	/* Finger 1 */
	FINGER_USAGE,
	/* Finger 2 */
	FINGER_USAGE,
	/* Finger 3 */
	FINGER_USAGE,
	/* Finger 4 */
	FINGER_USAGE,
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
	/* Timestamp */
	0x05, 0x0D,        /*   Usage Page (Digitizer) */
	0x55, 0x0C,        /*   Unit Exponent (-4) */
	0x66, 0x01, 0x10,  /*   Unit (System: SI Linear, Time: Seconds) */
	0x47, 0xFF, 0xFF, 0x00, 0x00,  /*   Physical Maximum (65535) */
	0x27, 0xFF, 0xFF, 0x00, 0x00,  /*   Logical Maximum (65535) */
	0x75, 0x10,        /*   Report Size (16) */
	0x95, 0x01,        /*   Report Count (1) */
	0x09, 0x56,        /*   Usage (0x56, Relative Scan Time) */
	0x81, 0x02,        /*   Input (Data,Var,Abs) */
	0xC0,              /* End Collection */
};

const struct usb_hid_descriptor USB_CUSTOM_DESC_VAR(USB_IFACE_HID_TOUCHPAD,
						hid, hid_desc_tp) = {
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

/*
 * Write a report to EP, must be called with queue mutex held, and caller
 * must first check that EP is not busy.
 */
static void write_touchpad_report(struct usb_hid_touchpad_report *report)
{
	memcpy_to_usbram((void *) usb_sram_addr(hid_ep_buf),
			 report, sizeof(*report));
	/* enable TX */
	STM32_TOGGLE_EP(USB_EP_HID_TOUCHPAD, EP_TX_MASK, EP_TX_VALID, 0);

	/*
	 * Wake the host. This is required to prevent a race between EP getting
	 * reloaded and host suspending the device, as, ideally, we never want
	 * to have EP loaded during suspend, to avoid reporting stale data.
	 */
	usb_wake();
}

static void hid_touchpad_process_queue(void);
DECLARE_DEFERRED(hid_touchpad_process_queue);

static void hid_touchpad_process_queue(void)
{
	struct usb_hid_touchpad_report report;
	uint16_t now;
	int trimming = 0;

	mutex_lock(&report_queue_mutex);

	/* EP is busy, or nothing in queue: do nothing. */
	if (queue_count(&report_queue) == 0)
		goto unlock;

	now = __hw_clock_source_read() / USB_HID_TOUCHPAD_TIMESTAMP_UNIT;

	if (usb_is_suspended() ||
			(STM32_USB_EP(USB_EP_HID_TOUCHPAD) & EP_TX_MASK)
				== EP_TX_VALID) {
		usb_wake();

		/* Let's trim old events from the queue, if any. */
		trimming = 1;
	} else {
		hook_call_deferred(&hid_touchpad_process_queue_data, -1);
	}

	if (touchpad_debug)
		CPRINTS("TPQ t=%d (%d)", trimming, queue_count(&report_queue));

	while (queue_count(&report_queue) > 0) {
		int delta;

		queue_peek_units(&report_queue, &report, 0, 1);

		delta = (int)((uint16_t)(now - report.timestamp))
				* USB_HID_TOUCHPAD_TIMESTAMP_UNIT;

		if (touchpad_debug)
			CPRINTS("evt t=%d d=%d", report.timestamp, delta);

		/* Drop old events */
		if (delta > EVENT_DISCARD_MAX_TIME) {
			queue_advance_head(&report_queue, 1);
			continue;
		}

		if (trimming) {
			/*
			 * If we stil fail to resume, this will discard the
			 * event after the timeout expires.
			 */
			hook_call_deferred(&hid_touchpad_process_queue_data,
					   EVENT_DISCARD_MAX_TIME - delta);
		} else {
			queue_advance_head(&report_queue, 1);
			write_touchpad_report(&report);
		}
		break;
	}

unlock:
	mutex_unlock(&report_queue_mutex);
}

void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
	static int print_full = 1;

	mutex_lock(&report_queue_mutex);

	/* USB/EP ready and nothing in queue, just write the report. */
	if (!usb_is_suspended() &&
	    (STM32_USB_EP(USB_EP_HID_TOUCHPAD) & EP_TX_MASK) != EP_TX_VALID
	    && queue_count(&report_queue) == 0) {
		write_touchpad_report(report);
		mutex_unlock(&report_queue_mutex);
		return;
	}

	/* Else add to queue, dropping oldest event if needed. */
	if (touchpad_debug)
		CPRINTS("sTP t=%d", report->timestamp);
	if (queue_is_full(&report_queue)) {
		if (print_full)
			CPRINTF("TP queue full\n");
		print_full = 0;

		queue_advance_head(&report_queue, 1);
	} else {
		print_full = 1;
	}
	queue_add_unit(&report_queue, report);

	mutex_unlock(&report_queue_mutex);

	hid_touchpad_process_queue();
}

static void hid_touchpad_tx(void)
{
	hid_tx(USB_EP_HID_TOUCHPAD);

	if (queue_count(&report_queue) > 0)
		hook_call_deferred(&hid_touchpad_process_queue_data, 0);
}

static void hid_touchpad_event(enum usb_ep_event evt)
{
	if (evt == USB_EVENT_RESET)
		hid_reset(USB_EP_HID_TOUCHPAD, hid_ep_buf,
			  HID_TOUCHPAD_REPORT_SIZE, NULL, 0);
	else if (evt == USB_EVENT_DEVICE_RESUME &&
			queue_count(&report_queue) > 0)
		hook_call_deferred(&hid_touchpad_process_queue_data, 0);
}

USB_DECLARE_EP(USB_EP_HID_TOUCHPAD, hid_touchpad_tx, hid_touchpad_tx,
	       hid_touchpad_event);

static int hid_touchpad_iface_request(usb_uint *ep0_buf_rx,
				      usb_uint *ep0_buf_tx)
{
	return hid_iface_request(ep0_buf_rx, ep0_buf_tx,
				 report_desc, sizeof(report_desc),
				 &hid_desc_tp);
}
USB_DECLARE_IFACE(USB_IFACE_HID_TOUCHPAD, hid_touchpad_iface_request)
