/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "hooks.h"
#include "queue.h"
#include "task.h"
#include "usb_hid_touchpad.h"
#include "usbd_init.h"
#include "util.h"

#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
LOG_MODULE_DECLARE(usb_hid_tp, LOG_LEVEL_INF);

#define TP_NODE DT_ALIAS(usb_hid_tp)
BUILD_ASSERT(DT_NODE_EXISTS(TP_NODE),
	     "Unsupported board: usb-hid-tp devicetree alias is not defined.");

static struct hid_dev_t touchpad = {
	.report_queue = QUEUE_NULL(CONFIG_USBD_HID_TOUCHPAD_QUEUE_SIZE,
				   struct usb_hid_touchpad_report),
};
static void hid_tp_proc_queue(void);
DECLARE_DEFERRED(hid_tp_proc_queue);

static const uint8_t report_desc[] =
	REPORT_DESC(DT_PROP_OR(TP_NODE, max_pressure, 0),
		    DT_PROP_OR(TP_NODE, logical_max_x, 0),
		    DT_PROP_OR(TP_NODE, logical_max_y, 0),
		    DT_PROP_OR(TP_NODE, physical_max_x, 0),
		    DT_PROP_OR(TP_NODE, physical_max_y, 0));

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

static int write_tp_report(struct usb_hid_touchpad_report *report)
{
	int ret = -EBUSY;

	if (touchpad.report_protocol == HID_PROTOCOL_BOOT) {
		LOG_DBG("%s: protocol error", touchpad.dev->name);
		return -EPROTO;
	}

	if (!atomic_test_bit(&touchpad.state, HID_CLASS_IFACE_READY)) {
		LOG_ERR("%s iface is not ready", touchpad.dev->name);
		return -EACCES;
	}

	if (atomic_test_bit(&touchpad.state, HID_CLASS_SUSPENDED)) {
		ret = request_usb_wake();
		if (ret) {
			return ret;
		}
	}

	if (!atomic_test_and_set_bit(&touchpad.state, HID_EP_IN_BUSY)) {
		ret = hid_device_submit_report(touchpad.dev, sizeof(*report),
					       (uint8_t *)report);
		if (ret) {
			LOG_ERR("%s: failed to submit report, %d",
				touchpad.dev->name, ret);
			atomic_clear_bit(&touchpad.state, HID_EP_IN_BUSY);
			return ret;
		}
	}
	return ret;
}

static void tp_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);

	if (ready) {
		atomic_set_bit(&touchpad.state, HID_CLASS_IFACE_READY);
	} else {
		atomic_clear_bit(&touchpad.state, HID_CLASS_IFACE_READY);
		mutex_lock(touchpad.report_queue_mutex);
		if (queue_count(&touchpad.report_queue) != 0) {
			queue_remove_units(&touchpad.report_queue, NULL,
					   queue_count(&touchpad.report_queue));
		}
		mutex_unlock(touchpad.report_queue_mutex);
	}
}

static int tp_get_report(const struct device *dev, const uint8_t type,
			 const uint8_t id, const uint16_t len,
			 uint8_t *const buf)
{
	ARG_UNUSED(dev);

	switch (id) {
	case REPORT_ID_DEVICE_CAPS:
		if (len >= ARRAY_SIZE(device_caps_response)) {
			memcpy(buf, device_caps_response, len);
			return ARRAY_SIZE(device_caps_response);
		}
		return -ENOTSUP;
	case REPORT_ID_DEVICE_CERT:
		if (len >= ARRAY_SIZE(device_cert_response)) {
			memcpy(buf, device_cert_response, len);
			return ARRAY_SIZE(device_cert_response);
		}
		return -ENOTSUP;
	default:
		break;
	}
	return -ENOTSUP;
}

static void tp_in_ready(const struct device *dev)
{
	ARG_UNUSED(dev);

	atomic_clear_bit(&touchpad.state, HID_EP_IN_BUSY);
}

static void tp_set_protocol(const struct device *dev, uint8_t protocol)
{
	ARG_UNUSED(dev);

	touchpad.report_protocol = protocol;
}

static const struct hid_device_ops ops = {
	.iface_ready = tp_iface_ready,
	.get_report = tp_get_report,
	.input_report_done = tp_in_ready,
	.set_protocol = tp_set_protocol,
};

__overridable void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
	static bool print_full = true;

	if (!touchpad.dev ||
	    !atomic_test_bit(&touchpad.state, HID_CLASS_IFACE_READY)) {
		return;
	}

	mutex_lock(touchpad.report_queue_mutex);

	if (queue_is_empty(&touchpad.report_queue)) {
		int ret = write_tp_report(report);

		if (ret != -EBUSY) {
			mutex_unlock(touchpad.report_queue_mutex);
			return;
		}
	}

	if (queue_is_full(&touchpad.report_queue)) {
		if (print_full) {
			LOG_WRN("touchpad queue is full");
		}
		print_full = false;

		queue_advance_head(&touchpad.report_queue, 1);
	} else {
		print_full = true;
	}
	queue_add_unit(&touchpad.report_queue, report);

	mutex_unlock(touchpad.report_queue_mutex);

	hook_call_deferred(&hid_tp_proc_queue_data, 0);
}

static void hid_tp_proc_queue(void)
{
	struct usb_hid_touchpad_report report;

	mutex_lock(touchpad.report_queue_mutex);

	if (!atomic_test_bit(&touchpad.state, HID_CLASS_IFACE_READY) ||
	    touchpad.report_protocol == HID_PROTOCOL_BOOT) {
		queue_remove_units(&touchpad.report_queue, NULL,
				   queue_count(&touchpad.report_queue));
		mutex_unlock(touchpad.report_queue_mutex);
		return;
	}

	if (queue_is_empty(&touchpad.report_queue)) {
		mutex_unlock(touchpad.report_queue_mutex);
		return;
	}

	queue_peek_units(&touchpad.report_queue, &report, 0, 1);

	if (write_tp_report(&report) != -EBUSY) {
		queue_advance_head(&touchpad.report_queue, 1);
	}

	mutex_unlock(touchpad.report_queue_mutex);
	hook_call_deferred(&hid_tp_proc_queue_data, 1 * USEC_PER_MSEC);
}

__maybe_unused static void tp_msg_deferred(void)
{
	switch (usb_message) {
	case USBD_MSG_RESET:
		touchpad.report_protocol = HID_PROTOCOL_REPORT;
		atomic_clear_bit(&touchpad.state, HID_EP_IN_BUSY);
		break;
	case USBD_MSG_SUSPEND:
		atomic_set_bit(&touchpad.state, HID_CLASS_SUSPENDED);
		break;
	case USBD_MSG_RESUME:
		atomic_clear_bit(&touchpad.state, HID_CLASS_SUSPENDED);
		break;
	default:
		break;
	}
}

DECLARE_DEFERRED(tp_msg_deferred);

static int usb_hid_tp_init(void)
{
	int ret;

	touchpad.dev = DEVICE_DT_GET(DT_NODELABEL(hid_tp_dev));

	if (!touchpad.dev) {
		LOG_ERR("failed to get hid touchpad device");
		return -ENXIO;
	}

	ret = hid_device_register(touchpad.dev, report_desc,
				  sizeof(report_desc), &ops);
	if (ret) {
		LOG_ERR("failed to register hid touchpad device, %d", ret);
		goto error;
	}

	ret = usb_msg_deferred_register(&tp_msg_deferred_data);
	if (ret) {
		LOG_ERR("failed to register touchpad message deferred work");
		goto error;
	}

	atomic_clear_bit(&touchpad.state, HID_EP_IN_BUSY);

	return 0;

error:
	touchpad.dev = NULL;
	return ret;
}
SYS_INIT(usb_hid_tp_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
