/* Copyright 2016 The ChromiumOS Authors
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
#include "usb_api.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "usb_hid_hw.h"
#include "usb_hid_touchpad.h"
#include "usb_hw.h"
#include "util.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USB, format, ##args)

static const int touchpad_debug;

static struct queue const report_queue =
	QUEUE_NULL(8, struct usb_hid_touchpad_report);
static struct mutex report_queue_mutex;

#define HID_TOUCHPAD_REPORT_SIZE sizeof(struct usb_hid_touchpad_report)

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

/*
 * HID: Report Descriptor
 * TODO(b/35582031): There are ways to reduce flash usage, as the
 * Finger Usage is repeated 5 times.
 */
static const uint8_t report_desc[] =
	REPORT_DESC(CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE,
		    CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X,
		    CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y,
		    CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X,
		    CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y);

/* A 256-byte default blob for the 'device certification status' feature report.
 *
 * TODO(b/113248108): do we need a real certification?
 */
static const uint8_t device_cert_response[] = {
	REPORT_ID_DEVICE_CERT,

	0xFC,
	0x28,
	0xFE,
	0x84,
	0x40,
	0xCB,
	0x9A,
	0x87,
	0x0D,
	0xBE,
	0x57,
	0x3C,
	0xB6,
	0x70,
	0x09,
	0x88,
	0x07,
	0x97,
	0x2D,
	0x2B,
	0xE3,
	0x38,
	0x34,
	0xB6,
	0x6C,
	0xED,
	0xB0,
	0xF7,
	0xE5,
	0x9C,
	0xF6,
	0xC2,
	0x2E,
	0x84,
	0x1B,
	0xE8,
	0xB4,
	0x51,
	0x78,
	0x43,
	0x1F,
	0x28,
	0x4B,
	0x7C,
	0x2D,
	0x53,
	0xAF,
	0xFC,
	0x47,
	0x70,
	0x1B,
	0x59,
	0x6F,
	0x74,
	0x43,
	0xC4,
	0xF3,
	0x47,
	0x18,
	0x53,
	0x1A,
	0xA2,
	0xA1,
	0x71,
	0xC7,
	0x95,
	0x0E,
	0x31,
	0x55,
	0x21,
	0xD3,
	0xB5,
	0x1E,
	0xE9,
	0x0C,
	0xBA,
	0xEC,
	0xB8,
	0x89,
	0x19,
	0x3E,
	0xB3,
	0xAF,
	0x75,
	0x81,
	0x9D,
	0x53,
	0xB9,
	0x41,
	0x57,
	0xF4,
	0x6D,
	0x39,
	0x25,
	0x29,
	0x7C,
	0x87,
	0xD9,
	0xB4,
	0x98,
	0x45,
	0x7D,
	0xA7,
	0x26,
	0x9C,
	0x65,
	0x3B,
	0x85,
	0x68,
	0x89,
	0xD7,
	0x3B,
	0xBD,
	0xFF,
	0x14,
	0x67,
	0xF2,
	0x2B,
	0xF0,
	0x2A,
	0x41,
	0x54,
	0xF0,
	0xFD,
	0x2C,
	0x66,
	0x7C,
	0xF8,
	0xC0,
	0x8F,
	0x33,
	0x13,
	0x03,
	0xF1,
	0xD3,
	0xC1,
	0x0B,
	0x89,
	0xD9,
	0x1B,
	0x62,
	0xCD,
	0x51,
	0xB7,
	0x80,
	0xB8,
	0xAF,
	0x3A,
	0x10,
	0xC1,
	0x8A,
	0x5B,
	0xE8,
	0x8A,
	0x56,
	0xF0,
	0x8C,
	0xAA,
	0xFA,
	0x35,
	0xE9,
	0x42,
	0xC4,
	0xD8,
	0x55,
	0xC3,
	0x38,
	0xCC,
	0x2B,
	0x53,
	0x5C,
	0x69,
	0x52,
	0xD5,
	0xC8,
	0x73,
	0x02,
	0x38,
	0x7C,
	0x73,
	0xB6,
	0x41,
	0xE7,
	0xFF,
	0x05,
	0xD8,
	0x2B,
	0x79,
	0x9A,
	0xE2,
	0x34,
	0x60,
	0x8F,
	0xA3,
	0x32,
	0x1F,
	0x09,
	0x78,
	0x62,
	0xBC,
	0x80,
	0xE3,
	0x0F,
	0xBD,
	0x65,
	0x20,
	0x08,
	0x13,
	0xC1,
	0xE2,
	0xEE,
	0x53,
	0x2D,
	0x86,
	0x7E,
	0xA7,
	0x5A,
	0xC5,
	0xD3,
	0x7D,
	0x98,
	0xBE,
	0x31,
	0x48,
	0x1F,
	0xFB,
	0xDA,
	0xAF,
	0xA2,
	0xA8,
	0x6A,
	0x89,
	0xD6,
	0xBF,
	0xF2,
	0xD3,
	0x32,
	0x2A,
	0x9A,
	0xE4,
	0xCF,
	0x17,
	0xB7,
	0xB8,
	0xF4,
	0xE1,
	0x33,
	0x08,
	0x24,
	0x8B,
	0xC4,
	0x43,
	0xA5,
	0xE5,
	0x24,
	0xC2,
};

/* Device capabilities feature report. */
static const uint8_t device_caps_response[] = {
	REPORT_ID_DEVICE_CAPS,

	MAX_FINGERS, /* Contact Count Maximum */
	0x00, /* Pad Type: Depressible click-pad */
};

const struct usb_hid_descriptor USB_CUSTOM_DESC_VAR(USB_IFACE_HID_TOUCHPAD, hid,
						    hid_desc_tp) = {
	.bLength = 9,
	.bDescriptorType = USB_HID_DT_HID,
	.bcdHID = 0x0100,
	.bCountryCode = 0x00, /* Hardware target country */
	.bNumDescriptors = 1,
	.desc = { { .bDescriptorType = USB_HID_DT_REPORT,
		    .wDescriptorLength = sizeof(report_desc) } }
};

static usb_uint hid_ep_buf[DIV_ROUND_UP(HID_TOUCHPAD_REPORT_SIZE, 2)] __usb_ram;

/*
 * Write a report to EP, must be called with queue mutex held, and caller
 * must first check that EP is not busy.
 */
static void write_touchpad_report(struct usb_hid_touchpad_report *report)
{
	memcpy_to_usbram((void *)usb_sram_addr(hid_ep_buf), report,
			 sizeof(*report));
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
	    (STM32_USB_EP(USB_EP_HID_TOUCHPAD) & EP_TX_MASK) == EP_TX_VALID) {
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

		delta = (int)((uint16_t)(now - report.timestamp)) *
			USB_HID_TOUCHPAD_TIMESTAMP_UNIT;

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
	    (STM32_USB_EP(USB_EP_HID_TOUCHPAD) & EP_TX_MASK) != EP_TX_VALID &&
	    queue_count(&report_queue) == 0) {
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

static int get_report(uint8_t report_id, uint8_t report_type,
		      const uint8_t **buffer_ptr, int *buffer_size)
{
	switch (report_id) {
	case REPORT_ID_DEVICE_CAPS:
		*buffer_ptr = device_caps_response;
		*buffer_size = MIN(sizeof(device_caps_response), *buffer_size);
		return 0;
	case REPORT_ID_DEVICE_CERT:
		*buffer_ptr = device_cert_response;
		*buffer_size = MIN(sizeof(device_cert_response), *buffer_size);
		return 0;
	}
	return -1;
}

static const struct usb_hid_config_t hid_config_tp = {
	.report_desc = report_desc,
	.report_size = sizeof(report_desc),
	.hid_desc = &hid_desc_tp,
	.get_report = get_report,
};

static int hid_touchpad_iface_request(usb_uint *ep0_buf_rx,
				      usb_uint *ep0_buf_tx)
{
	return hid_iface_request(ep0_buf_rx, ep0_buf_tx, &hid_config_tp);
}
USB_DECLARE_IFACE(USB_IFACE_HID_TOUCHPAD, hid_touchpad_iface_request)
