/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include <stdbool.h>
#include <stdint.h>
#include "compile_time_macros.h"
#include "console.h"
#include "hooks.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_tc_sm.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

/*
 * Retimer firmware update is initiated by AP.
 * The operations requested by AP are:
 * 0 - USB_RETIMER_FW_UPDATE_QUERY_PORT
 * 1 - USB_RETIMER_FW_UPDATE_SUSPEND_PD
 * 2 - USB_RETIMER_FW_UPDATE_RESUME_PD
 * 3 - USB_RETIMER_FW_UPDATE_GET_MUX
 * 4 - USB_RETIMER_FW_UPDATE_SET_USB
 * 5 - USB_RETIMER_FW_UPDATE_SET_SAFE
 * 6 - USB_RETIMER_FW_UPDATE_SET_TBT
 * 7 - USB_RETIMER_FW_UPDATE_DISCONNECT
 *
 * Operation 0 is processed immediately.
 * Operations 1 to 7 are deferred and processed inside tc_run().
 * Operations 1/2/3 can be processed any time; while 4/5/6/7 have
 * to be processed when PD task is suspended.
 * Two TC flags are created for this situation.
 * If Op 1/2/3 is received, TC_FLAGS_USB_RETIMER_FW_UPDATE_RUN
 * is set, PD task will be waken up and process it.
 * If 4/5/6/7 is received, TC_FLAGS_USB_RETIMER_FW_UPDATE_LTD_RUN is
 * set, PD task should be in suspended mode and process it.
 *
 */

/* Track current port AP requested to update retimer firmware */
static int cur_port;
static int last_op; /* Operation received from AP via ACPI_WRITE */
/* MUX value returned to ACPI_READ */
static int last_mux_result = USB_RETIMER_FW_UPDATE_INVALID_MUX;

__overridable int usb_retimer_fw_update_query_port(void)
{
	return 0;
}

int usb_retimer_fw_update_get_result(void)
{
	int result = 0;

	switch (last_op) {
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
	case USB_RETIMER_FW_UPDATE_RESUME_PD:
		result = pd_is_port_enabled(cur_port);
		break;
	case USB_RETIMER_FW_UPDATE_QUERY_PORT:
		result = usb_retimer_fw_update_query_port();
		break;
	case USB_RETIMER_FW_UPDATE_GET_MUX:
	case USB_RETIMER_FW_UPDATE_SET_USB:
	case USB_RETIMER_FW_UPDATE_SET_SAFE:
	case USB_RETIMER_FW_UPDATE_SET_TBT:
	case USB_RETIMER_FW_UPDATE_DISCONNECT:
		result = last_mux_result;
		break;
	default:
		break;
	}

	return result;
}

void usb_retimer_fw_update_process_op_cb(int port)
{
	switch (last_op) {
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
		pd_set_suspend(port, 1);
		break;
	case USB_RETIMER_FW_UPDATE_RESUME_PD:
		pd_set_suspend(port, 0);
		break;
	case USB_RETIMER_FW_UPDATE_GET_MUX:
		last_mux_result = usb_mux_get(port);
		break;
	case USB_RETIMER_FW_UPDATE_SET_USB:
		usb_mux_set(port, USB_PD_MUX_USB_ENABLED,
			USB_SWITCH_CONNECT, pd_get_polarity(port));
		last_mux_result = usb_mux_get(port);
		break;
	case USB_RETIMER_FW_UPDATE_SET_SAFE:
		usb_mux_set_safe_mode(port);
		last_mux_result = usb_mux_get(port);
		break;
	case USB_RETIMER_FW_UPDATE_SET_TBT:
		usb_mux_set(port, USB_PD_MUX_TBT_COMPAT_ENABLED,
			USB_SWITCH_CONNECT, pd_get_polarity(port));
		last_mux_result = usb_mux_get(port);
		break;
	case USB_RETIMER_FW_UPDATE_DISCONNECT:
		usb_mux_set(port, USB_PD_MUX_NONE,
			USB_SWITCH_DISCONNECT, pd_get_polarity(port));
		last_mux_result = usb_mux_get(port);
		break;
	default:
		break;
	}
}

void usb_retimer_fw_update_process_op(int port, int op)
{
	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	/*
	 * TODO(b/179220036): check not overlapping requests;
	 * not change cur_port if retimer scan is in progress
	 */
	last_op = op;

	switch (op) {
	case USB_RETIMER_FW_UPDATE_QUERY_PORT:
		break;
	/* Operations can't be processed in ISR, defer to later */
	case USB_RETIMER_FW_UPDATE_GET_MUX:
		last_mux_result = USB_RETIMER_FW_UPDATE_INVALID_MUX;
		tc_usb_firmware_fw_update_run(port);
		break;
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
	case USB_RETIMER_FW_UPDATE_RESUME_PD:
		cur_port = port;
		tc_usb_firmware_fw_update_run(port);
		break;
	case USB_RETIMER_FW_UPDATE_SET_USB:
	case USB_RETIMER_FW_UPDATE_SET_SAFE:
	case USB_RETIMER_FW_UPDATE_SET_TBT:
	case USB_RETIMER_FW_UPDATE_DISCONNECT:
		if (pd_is_port_enabled(port)) {
			last_mux_result = USB_RETIMER_FW_UPDATE_ERR;
		} else {
			last_mux_result = USB_RETIMER_FW_UPDATE_INVALID_MUX;
			tc_usb_firmware_fw_update_limited_run(port);
		}
		break;
	default:
		break;
	}
}
