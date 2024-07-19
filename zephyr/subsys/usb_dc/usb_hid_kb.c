/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hid_vivaldi.h"
#include "hooks.h"
#include "keyboard_config.h"
#include "queue.h"
#include "task.h"
#include "usb_dc.h"
#include "usb_hid.h"

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/usb_device.h>
LOG_MODULE_DECLARE(usb_hid_kb, LOG_LEVEL_INF);

/*
 * The keyboard HID number should be set as 0 since the coreboot BIOS parses
 * the interface protocol of the first hid deivce. The coreboot BIOS doesn't
 * send SET_PROTOCOL(boot protocol) request to EC if the interface protocol is
 * not HID_BOOT_IFACE_CODE_KEYBOARD. This assertion can be removed after the
 * coreboot BIOS supports interface protocol check for each hid device.
 */
BUILD_ASSERT(CONFIG_USB_DC_KEYBOARD_HID_NUM == 0,
	     "The hid number of keyboard device should be set as 0.");

BUILD_ASSERT(CONFIG_USB_DC_KEYBOARD_HID_NUM < CONFIG_USB_HID_DEVICE_COUNT,
	     "The hid number of keyboard is invaild.");

#define KB_DEV_NAME                 \
	(CONFIG_USB_HID_DEVICE_NAME \
	 "_" STRINGIFY(CONFIG_USB_DC_KEYBOARD_HID_NUM))

#define HID_EP_BUSY_FLAG 0

#define HID_KEYBOARD_MODIFIER_LOW 0xe0
#define HID_KEYBOARD_MODIFIER_HIGH 0xe7

/* Special keys/switches */
#define HID_KEYBOARD_EXTRA_LOW 0xf0
#define HID_KEYBOARD_EXTRA_HIGH 0xf1
#define HID_KEYBOARD_ASSISTANT_KEY 0xf0

#if defined(CONFIG_USB_DC_KEYBOARD_ASSISTANT_KEY) || \
	defined(CONFIG_USB_DC_KEYBOARD_TABLET_MODE_SWITCH)
#define HID_KEYBOARD_EXTRA_FIELD
#endif

/*
 * Vendor-defined Usage Page 0xffd1:
 *  - 0x18: Assistant key
 *  - 0x19: Tablet mode switch
 */
#ifdef HID_KEYBOARD_EXTRA_FIELD
#ifdef CONFIG_USB_DC_KEYBOARD_ASSISTANT_KEY
#define KEYBOARD_ASSISTANT_KEY_DESC                                         \
	HID_USAGE_MIN8(0x18), HID_USAGE_MAX8(0x18), HID_LOGICAL_MIN8(0x00), \
		HID_LOGICAL_MAX8(0x01), HID_REPORT_SIZE(1),                 \
		HID_REPORT_COUNT(1), HID_INPUT(0x02)
#else
/* No assistant key: just pad 1 bit. */
#define KEYBOARD_ASSISTANT_KEY_DESC \
	HID_REPORT_COUNT(1), HID_REPORT_SIZE(1), HID_INPUT(0x01)

#endif /* !CONFIG_USB_DC_KEYBOARD_ASSISTANT_KEY */

#ifdef CONFIG_USB_DC_KEYBOARD_TABLET_MODE_SWITCH
#define KEYBOARD_TABLET_MODE_SWITCH_DESC                                    \
	HID_USAGE_MIN8(0x19), HID_USAGE_MAX8(0x19), HID_LOGICAL_MIN8(0x00), \
		HID_LOGICAL_MAX8(0x01), HID_REPORT_SIZE(1),                 \
		HID_REPORT_COUNT(1), HID_INPUT(0x02)
#else
/* No tablet mode swtch: just pad 1 bit. */
#define KEYBOARD_TABLET_MODE_SWITCH_DESC \
	HID_REPORT_COUNT(1), HID_REPORT_SIZE(1), HID_INPUT(0x01)
#endif /* CONFIG_USB_DC_KEYBOARD_TABLET_MODE_SWITCH */

#define KEYBOARD_VENDOR_DESC                                                   \
	0x06, 0xd1, 0xff, /* Usage Page (Vendor-defined 0xffd1) */             \
		KEYBOARD_ASSISTANT_KEY_DESC, KEYBOARD_TABLET_MODE_SWITCH_DESC, \
		HID_REPORT_COUNT(1), HID_REPORT_SIZE(6), HID_INPUT(0x01)
#endif /* HID_KEYBOARD_EXTRA_FIELD */

/* HID : Report Descriptor */
static const uint8_t hid_report_desc[] = {
	HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
	HID_USAGE(HID_USAGE_GEN_DESKTOP_KEYBOARD),
	HID_COLLECTION(HID_COLLECTION_APPLICATION),
	HID_REPORT_SIZE(1),
	HID_REPORT_COUNT(8),
	HID_INPUT(0x01),
	HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP_KEYPAD),
	HID_USAGE_MIN8(HID_KEYBOARD_MODIFIER_LOW),
	HID_USAGE_MAX8(HID_KEYBOARD_MODIFIER_HIGH),
	HID_LOGICAL_MIN8(0x00),
	HID_LOGICAL_MAX8(0x01),
	HID_REPORT_SIZE(1),
	HID_REPORT_COUNT(8),
	HID_INPUT(0x02),
	HID_REPORT_COUNT(6),
	HID_REPORT_SIZE(8),
	HID_LOGICAL_MIN8(0x0),
	HID_LOGICAL_MAX8(0xa4),
	HID_USAGE_MIN8(0x00),
	HID_USAGE_MAX8(0xa4),
	HID_INPUT(0x00),

#ifdef KEYBOARD_VENDOR_DESC
	KEYBOARD_VENDOR_DESC,
#endif

#ifdef CONFIG_USB_DC_HID_VIVALDI
	KEYBOARD_TOP_ROW_DESC,
	KEYBOARD_TOP_ROW_FEATURE_DESC,
#endif
	HID_END_COLLECTION
};

/* The standard Chrome OS keyboard matrix table. See HUT 1.12v2 Table 12 and
 * https://www.w3.org/TR/DOM-Level-3-Events-code .
 *
 * Assistant key is mapped as 0xf0, but this key code is never actually send.
 */
const uint8_t keycodes[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 0x00, 0x00, 0xe0, 0xe3, 0xe4, HID_KEYBOARD_ASSISTANT_KEY, 0x00,
	  0x00 },
	{ 0xe3, 0x29, 0x2b, 0x35, 0x04, 0x1d, 0x1e, 0x14 },
	{ 0x3a, 0x3d, 0x3c, 0x3b, 0x07, 0x06, 0x20, 0x08 },
	{ 0x05, 0x0a, 0x17, 0x22, 0x09, 0x19, 0x21, 0x15 },
	{ 0x43, 0x40, 0x3f, 0x3e, 0x16, 0x1b, 0x1f, 0x1a },
	{ 0x87, 0x00, 0x30, 0x00, 0x0e, 0x36, 0x25, 0x0c },
	{ 0x11, 0x0b, 0x1c, 0x23, 0x0d, 0x10, 0x24, 0x18 },
	{ 0x00, 0x00, 0x64, 0x00, 0x00, 0xe1, 0x00, 0xe5 },
	{ 0x2e, 0x34, 0x2F, 0x2d, 0x33, 0x38, 0x27, 0x13 },
	{ 0x00, 0x42, 0x41, 0x68, 0x0f, 0x37, 0x26, 0x12 },
	{ 0xe6, 0x00, 0x89, 0x00, 0x31, 0x00, 0xe2, 0x00 },
	{ 0x00, 0x2a, 0x00, 0x31, 0x28, 0x2c, 0x51, 0x52 },
	{ 0x00, 0x8a, 0x00, 0x8b, 0x00, 0x00, 0x4f, 0x50 },
};

/*
 * Note: This first 8 bytes of this report format cannot be changed, as that
 * would break HID Boot protocol compatibility (see HID 1.11 "Appendix B: Boot
 * Interface Descriptors").
 */
struct usb_hid_keyboard_report {
	union {
		uint8_t boot_modifiers; /* boot protocol */
		uint8_t report_id; /* report protocol */
	} byte_0;

	union {
		uint8_t reserved; /* boot protocol */
		uint8_t report_modifiers; /* report protocol */
	} byte_1;

	uint8_t keys[6];
	/* Non-boot protocol fields below */
#ifdef HID_KEYBOARD_EXTRA_FIELD
	/* Assistant/tablet mode switch bitmask */
	uint8_t extra;
#endif
#ifdef CONFIG_USB_DC_HID_VIVALDI
	uint32_t top_row; /* bitmap of top row action keys */
#endif
} __packed;

static struct usb_hid_keyboard_report report;

static struct queue const report_queue =
	QUEUE_NULL(32, struct usb_hid_keyboard_report);
static struct k_mutex *report_queue_mutex;
static void hid_kb_proc_queue(void);
DECLARE_DEFERRED(hid_kb_proc_queue);

static const struct device *hid_dev;

static bool boot_protocol;
static ATOMIC_DEFINE(hid_ep_in_busy, 1);

static int kb_get_report(const struct device *dev,
			 struct usb_setup_packet *setup, int32_t *len,
			 uint8_t **data)
{
	/* The report type is in the high byte */
	switch ((setup->wValue & 0xFF00) >> 8) {
	case REPORT_TYPE_INPUT:
		*data = (uint8_t *)&report;
		*len = sizeof(report);
		return 0;
	case REPORT_TYPE_FEATURE:
#ifdef CONFIG_USB_DC_HID_VIVALDI
		*len = get_vivaldi_feature_report(*data);
		if (*len != 0) {
			return 0;
		}
#endif
		__attribute__((fallthrough));
	case REPORT_TYPE_OUTPUT:
		__attribute__((fallthrough));
	default:
		break;
	}
	return -ENOTSUP;
}

static void protocol_cb(const struct device *dev, uint8_t protocol)
{
	ARG_UNUSED(dev);
	LOG_DBG("new protocol %s\n",
		(protocol == HID_PROTOCOL_BOOT) ? "boot" : "report");

	boot_protocol = (protocol == HID_PROTOCOL_BOOT) ? true : false;
}

static void int_in_ready_cb(const struct device *dev)
{
	ARG_UNUSED(dev);
	atomic_clear_bit(hid_ep_in_busy, HID_EP_BUSY_FLAG);
}

static const struct hid_ops ops = {
	.protocol_change = protocol_cb,
	.get_report = kb_get_report,
	.int_in_ready = int_in_ready_cb,
};

static bool generate_keyboard_report(uint8_t keycode, int is_pressed)
{
	static uint8_t modifiers;
	bool valid = false;
	uint8_t mask;
	uint32_t action_key_mask = 0;

#ifdef CONFIG_USB_DC_HID_VIVALDI
	action_key_mask = vivaldi_convert_function_key(keycode);
#endif

	if (action_key_mask) {
#ifdef CONFIG_USB_DC_HID_VIVALDI
		if (is_pressed)
			report.top_row |= action_key_mask;
		else
			report.top_row &= ~action_key_mask;
		valid = true;
#endif
	} else if (keycode >= HID_KEYBOARD_EXTRA_LOW &&
		   keycode <= HID_KEYBOARD_EXTRA_HIGH) {
#ifdef HID_KEYBOARD_EXTRA_FIELD
		mask = 0x01 << (keycode - HID_KEYBOARD_EXTRA_LOW);
		if (is_pressed)
			report.extra |= mask;
		else
			report.extra &= ~mask;
		valid = true;
#endif
	} else if (keycode >= HID_KEYBOARD_MODIFIER_LOW &&
		   keycode <= HID_KEYBOARD_MODIFIER_HIGH) {
		mask = 0x01 << (keycode - HID_KEYBOARD_MODIFIER_LOW);
		if (is_pressed)
			modifiers |= mask;
		else
			modifiers &= ~mask;
		valid = true;
	} else if (is_pressed) {
		/*
		 * Add keycode to the list of keys (does nothing if the
		 * array is already full).
		 */
		for (int i = 0; i < ARRAY_SIZE(report.keys); i++) {
			/* Is key already pressed? */
			if (report.keys[i] == keycode)
				break;
			if (report.keys[i] == 0) {
				report.keys[i] = keycode;
				valid = true;
				break;
			}
		}
	} else {
		/*
		 * Remove keycode from the list of keys (does nothing
		 * if the key is not in the array).
		 */
		for (int i = 0; i < ARRAY_SIZE(report.keys); i++) {
			if (report.keys[i] == keycode) {
				report.keys[i] = 0;
				valid = true;
				break;
			}
		}
	}

	if (valid) {
		if (boot_protocol) {
			report.byte_0.boot_modifiers = modifiers;
			report.byte_1.reserved = 0x0;
		} else {
			report.byte_0.report_id = 0x0;
			report.byte_1.report_modifiers = modifiers;
		}
	}
	return valid;
}

__overridable void keyboard_state_changed(int row, int col, int is_pressed)
{
	static int print_full = 1;
	uint8_t keycode = keycodes[col][row];

	if (!hid_dev) {
		return;
	}

	if (!keycode) {
		LOG_ERR("unknown key at %d/%d\n", row, col);
		return;
	}

	if (generate_keyboard_report(keycode, is_pressed)) {
		if (!check_usb_is_configured()) {
			return;
		}

		if (check_usb_is_suspended()) {
			if (!request_usb_wake()) {
				return;
			}
		}

		mutex_lock(report_queue_mutex);
		if (queue_is_full(&report_queue)) {
			if (print_full)
				LOG_WRN("keyboard queue full\n");
			print_full = 0;

			queue_advance_head(&report_queue, 1);
		} else {
			print_full = 1;
		}
		queue_add_unit(&report_queue, &report);
		mutex_unlock(report_queue_mutex);

		hook_call_deferred(&hid_kb_proc_queue_data, 0);
	}
}

static void hid_kb_proc_queue(void)
{
	struct usb_hid_keyboard_report kb_data;
	int ret;
	size_t size;

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

	if (!atomic_test_and_set_bit(hid_ep_in_busy, HID_EP_BUSY_FLAG)) {
		queue_peek_units(&report_queue, &kb_data, 0, 1);

		size = boot_protocol ? 8 :
				       sizeof(struct usb_hid_keyboard_report);
		ret = hid_int_ep_write(hid_dev, (uint8_t *)&kb_data, size,
				       NULL);
		if (ret) {
			LOG_INF("hid kb write error, %d", ret);
			atomic_clear_bit(hid_ep_in_busy, HID_EP_BUSY_FLAG);
		} else {
			queue_advance_head(&report_queue, 1);
		}
	}

	mutex_unlock(report_queue_mutex);
	hook_call_deferred(&hid_kb_proc_queue_data, 1 * MSEC);
}

static int usb_hid_kb_init(void)
{
	hid_dev = device_get_binding(KB_DEV_NAME);
	if (!hid_dev) {
		LOG_ERR("failed to get hid device");
		return -ENXIO;
	}

	usb_hid_register_device(hid_dev, hid_report_desc,
				sizeof(hid_report_desc), &ops);

#ifdef CONFIG_USB_HID_BOOT_PROTOCOL
	if (usb_hid_set_proto_code(hid_dev, HID_BOOT_IFACE_CODE_KEYBOARD)) {
		LOG_WRN("failed to set interface protocol code");
	}
#endif

	usb_hid_init(hid_dev);
	atomic_clear_bit(hid_ep_in_busy, HID_EP_BUSY_FLAG);

	return 0;
}
SYS_INIT(usb_hid_kb_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
