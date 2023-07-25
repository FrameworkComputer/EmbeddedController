/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_dc.h"

#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/usb_device.h>
LOG_MODULE_DECLARE(usb_dc, LOG_LEVEL_INF);

struct usb_controller_status {
	bool suspended;
	bool configured;
};

struct usb_controller_status usb_dc_status;

static void status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
	switch (status) {
	case USB_DC_RESET:
		usb_dc_status.configured = false;
		usb_dc_status.suspended = false;
		break;
	case USB_DC_CONFIGURED:
		usb_dc_status.configured = true;
		break;
	case USB_DC_DISCONNECTED:
		usb_dc_status.configured = false;
		usb_dc_status.suspended = false;
		break;
	case USB_DC_SUSPEND:
		usb_dc_status.suspended = true;
		break;
	case USB_DC_RESUME:
		usb_dc_status.suspended = false;
		break;
	default:
		break;
	}
}

bool check_usb_is_suspended(void)
{
	return usb_dc_status.suspended;
}

bool check_usb_is_configured(void)
{
	return usb_dc_status.configured;
}

bool request_usb_wake(void)
{
	if (IS_ENABLED(CONFIG_USB_DEVICE_REMOTE_WAKEUP)) {
		usb_wakeup_request();
		return usb_dc_status.suspended ? false : true;
	}
	return false;
}

static int usb_dc_init(void)
{
	int ret = usb_enable(status_cb);

	if (ret != 0) {
		LOG_ERR("failed to enable usb");
		return ret;
	}

	return 0;
}

SYS_INIT(usb_dc_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
