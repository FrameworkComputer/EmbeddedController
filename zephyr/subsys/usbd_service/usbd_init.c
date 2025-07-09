/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_version.h"
#include "usbd_init.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_DECLARE(usb_device_init, LOG_LEVEL_INF);

USBD_DEVICE_DEFINE(usb_device, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   CONFIG_USB_DEVICE_VID, CONFIG_USB_DEVICE_PID);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, CONFIG_USB_DEVICE_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(product, CONFIG_USB_DEVICE_PRODUCT);
USBD_DESC_STRING_DEFINE(sn, CONFIG_USB_DEVICE_SN,
			USBD_DUT_STRING_SERIAL_NUMBER);

static const uint8_t attributes =
	(IS_ENABLED(CONFIG_USB_DEVICE_SELF_POWERED) ? USB_SCD_SELF_POWERED :
						      0) |
	(IS_ENABLED(CONFIG_USB_DEVICE_REMOTE_WAKEUP) ? USB_SCD_REMOTE_WAKEUP :
						       0);

/*
 * Set the configuration description string to CROS_EC_VERSION32 definition
 * because the application "hammerd" parses this string as the firmware
 * version.
 */
USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, CROS_EC_VERSION32);

USBD_CONFIGURATION_DEFINE(usb_device_fs_config, attributes,
			  CONFIG_USB_DEVICE_MAX_POWER, &fs_cfg_desc);

struct usb_msg_manager msg_manager = {
	.deferred = NULL,
	.callback_count = 0,
	.max_callbacks = 0,
};
enum usbd_msg_type usb_message = USBD_MSG_MAX_NUMBER;

#if defined(CONFIG_CROS_EC_RO) && defined(CONFIG_USBD_HID_KEYBOARD)
__overridable void keyboard_state_changed(int row, int col, int is_pressed)
{
}
#endif /* defined(CONFIG_CROS_EC_RO) && defined(CONFIG_USBD_HID_KEYBOARD) */

#if defined(CONFIG_CROS_EC_RO) && defined(CONFIG_USBD_HID_TOUCHPAD)
#include "usb_hid_touchpad.h"

__overridable void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
}
#endif /* defined(CONFIG_CROS_EC_RO) && defined(CONFIG_USBD_HID_TOUCHPAD) */

int request_usb_wake(void)
{
	if (IS_ENABLED(CONFIG_USB_DEVICE_REMOTE_WAKEUP)) {
		if (!usb_device.status.rwup) {
			return -EACCES;
		}
		return usbd_wakeup_request(&usb_device);
	}

	LOG_ERR("unsupported remote wakeup");
	return -ENOTSUP;
}

int usb_msg_deferred_register(const struct deferred_data *deferred)
{
	if (msg_manager.callback_count == msg_manager.max_callbacks) {
		msg_manager.max_callbacks++;
		msg_manager.deferred = (struct deferred_data *)realloc(
			msg_manager.deferred,
			msg_manager.max_callbacks *
				sizeof(struct deferred_data));
		if (msg_manager.deferred == NULL) {
			LOG_ERR("failed to allocate usb message deferred work "
				"memory");
			return -ENOMEM;
		}
	}
	msg_manager.deferred[msg_manager.callback_count++] = *deferred;
	return 0;
}

uint8_t google_get_in_ep(struct usbd_class_data *const c_data)
{
	struct google_data *data = usbd_class_get_private(c_data);
	struct google_desc *desc = data->desc;

	return desc->in_ep.bEndpointAddress;
}

uint8_t google_get_out_ep(struct usbd_class_data *const c_data)
{
	struct google_data *data = usbd_class_get_private(c_data);
	struct google_desc *desc = data->desc;

	return desc->out_ep.bEndpointAddress;
}

static void usb_device_msg_cb(struct usbd_context *const ctx,
			      const struct usbd_msg *msg)
{
	LOG_INF("usb message: %s", usbd_msg_type_string(msg->type));

	/* broadcast usb event */
	usb_message = msg->type;
	for (int i = 0; i < msg_manager.callback_count; i++) {
		hook_call_deferred(&msg_manager.deferred[i], 0);
	}
}

static int register_fs_classes(struct usbd_context *ctx)
{
	int err = 0;

	STRUCT_SECTION_FOREACH_ALTERNATE(usbd_class_fs, usbd_class_node, c_nd)
	{
		/* Pull everything that is enabled in our configuration. */
		err = usbd_register_class(ctx, c_nd->c_data->name,
					  USBD_SPEED_FS, 1);
		if (err) {
			LOG_ERR("failed to register fs %s (%d)",
				c_nd->c_data->name, err);
			return err;
		}

		LOG_DBG("register fs %s", c_nd->c_data->name);
	}

	return err;
}

static int usb_device_init(void)
{
	int err;

	err = usbd_add_descriptor(&usb_device, &lang);
	if (err) {
		LOG_ERR("failed to initialize language descriptor (%d)", err);
		goto error;
	}

	err = usbd_add_descriptor(&usb_device, &mfr);
	if (err) {
		LOG_ERR("failed to initialize manufacturer descriptor (%d)",
			err);
		goto error;
	}

	err = usbd_add_descriptor(&usb_device, &product);
	if (err) {
		LOG_ERR("failed to initialize product descriptor (%d)", err);
		goto error;
	}

	err = usbd_add_descriptor(&usb_device, &sn);
	if (err) {
		LOG_ERR("failed to initialize SN descriptor (%d)", err);
		goto error;
	}

	enum usbd_speed speed = usbd_caps_speed(&usb_device);

	err = usbd_add_configuration(&usb_device, speed, &usb_device_fs_config);
	if (err) {
		LOG_ERR("failed to add configuration (%d)", err);
		goto error;
	}

	switch (speed) {
	case USBD_SPEED_FS:
		err = register_fs_classes(&usb_device);
		break;
	case USBD_SPEED_HS:
		LOG_ERR("unsupported usb high-speed");
		err = -ENOTSUP;
		break;
	default:
		LOG_ERR("unknown usb speed(%d)", speed);
		err = -ENOTSUP;
		break;
	};
	if (err) {
		goto error;
	}

	err = usbd_device_set_code_triple(&usb_device, speed, 0, 0, 0);
	if (err) {
		LOG_ERR("failed to set descriptor code");
		goto error;
	}

	err = usbd_msg_register_cb(&usb_device, usb_device_msg_cb);
	if (err) {
		LOG_ERR("failed to register message callback");
		goto error;
	}

	err = usbd_init(&usb_device);
	if (err) {
		LOG_ERR("failed to initialize device support");
		goto error;
	}

	err = usbd_enable(&usb_device);
	if (err) {
		LOG_ERR("failed to enable device support");
		goto error;
	}

	return 0;

error:
	if (msg_manager.deferred) {
		free(msg_manager.deferred);
		msg_manager.deferred = NULL;
	}
	return err;
}
SYS_INIT(usb_device_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
