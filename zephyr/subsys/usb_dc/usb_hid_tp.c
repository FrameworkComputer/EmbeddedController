/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "hooks.h"
#include "queue.h"
#include "task.h"
#include "usb_dc.h"
#include "usb_hid_touchpad.h"
#include "util.h"

#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/usb_device.h>
LOG_MODULE_DECLARE(usb_hid_tp, LOG_LEVEL_INF);

BUILD_ASSERT(CONFIG_USB_DC_TOUCHPAD_HID_NUM < CONFIG_USB_HID_DEVICE_COUNT,
	     "The hid number of touchpad is invaild.");
#define TP_DEV_NAME                 \
	(CONFIG_USB_HID_DEVICE_NAME \
	 "_" STRINGIFY(CONFIG_USB_DC_TOUCHPAD_HID_NUM))

#define TP_NODE DT_ALIAS(usb_hid_tp)
BUILD_ASSERT(DT_NODE_EXISTS(TP_NODE),
	     "Unsupported board: usb-hid-tp devicetree alias is not defined.");

static const struct device *hid_dev;
static struct queue const report_queue =
	QUEUE_NULL(8, struct usb_hid_touchpad_report);
static struct k_mutex *report_queue_mutex;
static ATOMIC_DEFINE(hid_ep_in_busy, 1);

#define HID_EP_BUSY_FLAG 0

#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X \
	DT_PROP_OR(TP_NODE, logical_max_x, 0)
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y \
	DT_PROP_OR(TP_NODE, logical_max_y, 0)
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE \
	DT_PROP_OR(TP_NODE, max_pressure, 0)
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X \
	DT_PROP_OR(TP_NODE, physical_max_x, 0)
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y \
	DT_PROP_OR(TP_NODE, physical_max_y, 0)

#define FINGER_USAGE                                                           \
	0x05, 0x0D, /*   Usage Page (Digitizer) */                             \
		0x09, 0x22, /*   Usage (Finger) */                             \
		0xA1, 0x02, /*   Collection (Logical) */                       \
		0x09, 0x47, /*     Usage (Confidence) */                       \
		0x09, 0x42, /*     Usage (Tip Switch) */                       \
		0x09, 0x32, /*     Usage (In Range) */                         \
		0x15, 0x00, /*     Logical Minimum (0) */                      \
		0x25, 0x01, /*     Logical Maximum (1) */                      \
		0x75, 0x01, /*     Report Size (1) */                          \
		0x95, 0x03, /*     Report Count (3) */                         \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x09, 0x51, /*     Usage (0x51) Contact identifier */          \
		0x75, 0x04, /*     Report Size (4) */                          \
		0x95, 0x01, /*     Report Count (1) */                         \
		0x25, 0x0F, /*     Logical Maximum (15) */                     \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x05, 0x0D, /*     Usage Page (Digitizer) */ /*     Logical    \
								Maximum of     \
								Pressure */    \
		0x26, (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE & 0xFF),   \
		(CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE >> 8), 0x75,     \
		0x09, /*     Report Size (9) */                                \
		0x09, 0x30, /*     Usage (Tip pressure) */                     \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x26, 0xFF, 0x0F, /*     Logical Maximum (4095) */             \
		0x75, 0x0C, /*     Report Size (12) */                         \
		0x09, 0x48, /*     Usage (WIDTH) */                            \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x09, 0x49, /*     Usage (HEIGHT) */                           \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x05, 0x01, /*     Usage Page (Generic Desktop Ctrls) */       \
		0x75, 0x0C, /*     Report Size (12) */                         \
		0x55, 0x0E, /*     Unit Exponent (-2) */                       \
		0x65, 0x11, /*     Unit (System: SI Linear, Length: cm) */     \
		0x09, 0x30, /*     Usage (X) */                                \
		0x35, 0x00, /*     Physical Minimum (0) */                     \
		0x26, (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X & 0xff),          \
		(CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X >> 8), /*     Logical   \
								 Maximum */    \
		0x46, (CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X & 0xff),         \
		(CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X >> 8), /*     Physical \
								  Maximum      \
								  (tenth of    \
								  mm) */       \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0x26, (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y & 0xff),          \
		(CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y >> 8), /*     Logical   \
								 Maximum */    \
		0x46, (CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y & 0xff),         \
		(CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y >> 8), /*     Physical \
								  Maximum      \
								  (tenth of    \
								  mm) */       \
		0x09, 0x31, /*     Usage (Y) */                                \
		0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		0xC0 /*   End Collection */

static const uint8_t report_desc[] = {
	/* Touchpad Collection */
	0x05, 0x0D, /* Usage Page (Digitizer) */
	0x09, 0x05, /* Usage (Touch Pad) */
	0xA1, 0x01, /* Collection (Application) */
	0x85, REPORT_ID_TOUCHPAD, /*   Report ID (1, Touch) */
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
	0x05, 0x0D, /*   Usage Page (Digitizer) */
	0x09, 0x54, /*   Usage (Contact count) */
	0x25, MAX_FINGERS, /*   Logical Maximum (MAX_FINGERS) */
	0x75, 0x07, /*   Report Size (7) */
	0x95, 0x01, /*   Report Count (1) */
	0x81, 0x02, /*   Input (Data,Var,Abs) */
	/* Button */
	0x05, 0x01, /*   Usage Page(Generic Desktop Ctrls) */
	0x05, 0x09, /*   Usage (Button) */
	0x19, 0x01, /*   Usage Minimum (0x01) */
	0x29, 0x01, /*   Usage Maximum (0x01) */
	0x15, 0x00, /*   Logical Minimum (0) */
	0x25, 0x01, /*   Logical Maximum (1) */
	0x75, 0x01, /*   Report Size (1) */
	0x95, 0x01, /*   Report Count (1) */
	0x81, 0x02, /*   Input (Data,Var,Abs) */
	/* Timestamp */
	0x05, 0x0D, /*   Usage Page (Digitizer) */
	0x55, 0x0C, /*   Unit Exponent (-4) */
	0x66, 0x01, 0x10, /*   Unit (Seconds) */
	0x47, 0xFF, 0xFF, 0x00, 0x00, /*   Physical Maximum (65535) */
	0x27, 0xFF, 0xFF, 0x00, 0x00, /*   Logical Maximum (65535) */
	0x75, 0x10, /*   Report Size (16) */
	0x95, 0x01, /*   Report Count (1) */
	0x09, 0x56, /*   Usage (0x56, Relative Scan Time) */
	0x81, 0x02, /*   Input (Data,Var,Abs) */

	0x85, REPORT_ID_DEVICE_CAPS, /*   Report ID (Device Capabilities) */
	0x09, 0x55, /*   Usage (Contact Count Maximum) */
	0x09, 0x59, /*   Usage (Pad Type) */
	0x25, 0x0F, /*   Logical Maximum (15) */
	0x75, 0x08, /*   Report Size (8) */
	0x95, 0x02, /*   Report Count (2) */
	0xB1, 0x02, /*   Feature (Data,Var,Abs) */

	/* Page 0xFF, usage 0xC5 is device certificate. */
	0x06, 0x00, 0xFF, /*   Usage Page (Vendor Defined) */
	0x85, REPORT_ID_DEVICE_CERT, /*   Report ID (Device Certification) */
	0x09, 0xC5, /*   Usage (Vendor Usage 0xC5) */
	0x15, 0x00, /*   Logical Minimum (0) */
	0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
	0x75, 0x08, /*   Report Size (8) */
	0x96, 0x00, 0x01, /*   Report Count (256) */
	0xB1, 0x02, /*   Feature (Data,Var,Abs) */

	0xC0, /* End Collection */
};

/* A 256-byte default blob for the 'device certification status' feature report.
 */
static uint8_t device_cert_response[] = {
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
static uint8_t device_caps_response[] = {
	REPORT_ID_DEVICE_CAPS,

	MAX_FINGERS, /* Contact Count Maximum */
	0x00, /* Pad Type: Depressible click-pad */
};

static void hid_tp_proc_queue(void);
DECLARE_DEFERRED(hid_tp_proc_queue);

static void write_tp_report(struct usb_hid_touchpad_report *report)
{
	if (!atomic_test_and_set_bit(hid_ep_in_busy, HID_EP_BUSY_FLAG)) {
		int ret = hid_int_ep_write(hid_dev, (uint8_t *)report,
					   sizeof(*report), NULL);

		if (ret) {
			LOG_ERR("hid tp write error, %d", ret);
		}
	}
}

static int tp_get_report(const struct device *dev,
			 struct usb_setup_packet *setup, int32_t *len,
			 uint8_t **data)
{
	switch (setup->wValue & 0xFF) {
	case REPORT_ID_DEVICE_CAPS:
		*data = device_caps_response;
		*len = sizeof(device_caps_response);
		return 0;
	case REPORT_ID_DEVICE_CERT:
		*data = device_cert_response;
		*len = sizeof(device_cert_response);
		return 0;
	}
	return -ENOTSUP;
}

static void int_in_ready_cb(const struct device *dev)
{
	ARG_UNUSED(dev);
	atomic_clear_bit(hid_ep_in_busy, HID_EP_BUSY_FLAG);
}

static const struct hid_ops ops = {
	.get_report = tp_get_report,
	.int_in_ready = int_in_ready_cb,
};

__overridable void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
	static int print_full = 1;

	if (!hid_dev || !check_usb_is_configured()) {
		return;
	}

	mutex_lock(report_queue_mutex);

	if (!check_usb_is_suspended()) {
		if (queue_is_empty(&report_queue)) {
			write_tp_report(report);
			mutex_unlock(report_queue_mutex);
			return;
		}
	} else {
		if (!request_usb_wake()) {
			mutex_unlock(report_queue_mutex);
			return;
		}
	}

	if (queue_is_full(&report_queue)) {
		if (print_full)
			LOG_WRN("touchpad queue full\n");
		print_full = 0;

		queue_advance_head(&report_queue, 1);
	} else {
		print_full = 1;
	}
	queue_add_unit(&report_queue, report);

	mutex_unlock(report_queue_mutex);

	hook_call_deferred(&hid_tp_proc_queue_data, 0);
}

static void hid_tp_proc_queue(void)
{
	struct usb_hid_touchpad_report report;

	mutex_lock(report_queue_mutex);

	/* clear queue if the usb dc status is reset or disconected */
	if (!check_usb_is_configured() && !check_usb_is_suspended()) {
		queue_remove_units(&report_queue, NULL,
				   queue_count(&report_queue));
		mutex_unlock(report_queue_mutex);
		return;
	}

	if (queue_is_empty(&report_queue)) {
		mutex_unlock(report_queue_mutex);
		return;
	}

	queue_peek_units(&report_queue, &report, 0, 1);

	write_tp_report(&report);

	queue_advance_head(&report_queue, 1);

	mutex_unlock(report_queue_mutex);
	hook_call_deferred(&hid_tp_proc_queue_data, 1 * MSEC);
}

static int usb_hid_tp_init(void)
{
	hid_dev = device_get_binding(TP_DEV_NAME);

	if (!hid_dev) {
		LOG_ERR("failed to get hid device");
		return -ENXIO;
	}

	usb_hid_register_device(hid_dev, report_desc, sizeof(report_desc),
				&ops);

	usb_hid_init(hid_dev);
	atomic_clear_bit(hid_ep_in_busy, HID_EP_BUSY_FLAG);

	return 0;
}
SYS_INIT(usb_hid_tp_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
