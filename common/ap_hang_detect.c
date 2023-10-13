/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AP hang detect logic */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "power_button.h"
#include "timer.h"
#include "util.h"

/* Console output macro */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, "APHD: " format, ##args)

static uint16_t reboot_timeout_sec;
static uint8_t bootstatus = EC_HANG_DETECT_AP_BOOT_NORMAL;

/**
 * hang detect handlers for reboot.
 */
static void hang_detect_reboot(void)
{
	/* If we're rebooting the AP, stop hang detection */
	CPRINTS("Triggering reboot");
	chipset_reset(CHIPSET_RESET_HANG_REBOOT);
	bootstatus = EC_HANG_DETECT_AP_BOOT_EC_WDT;
}
DECLARE_DEFERRED(hang_detect_reboot);

static void hang_detect_reload(void)
{
	CPRINTS("Reloaded on AP request (timeout: %ds)", reboot_timeout_sec);
	hook_call_deferred(&hang_detect_reboot_data,
			   reboot_timeout_sec * SECOND);
}

static void hang_detect_cancel(void)
{
	CPRINTS("Stop on AP request");
	hook_call_deferred(&hang_detect_reboot_data, -1);
}

/*****************************************************************************/
/* Host command */

static enum ec_status
hang_detect_host_command(struct host_cmd_handler_args *args)
{
	const struct ec_params_hang_detect *p = args->params;
	struct ec_response_hang_detect *r = args->response;
	enum ec_status ret = EC_RES_SUCCESS;
	enum chipset_shutdown_reason ec_reason;

	switch (p->command) {
	case EC_HANG_DETECT_CMD_RELOAD:
		/* Handle reload hang timer on request */
		if (reboot_timeout_sec < EC_HANG_DETECT_MIN_TIMEOUT) {
			CPRINTS("Reboot timeout has to be greater than %ds",
				EC_HANG_DETECT_MIN_TIMEOUT);
			ret = EC_RES_INVALID_PARAM;
			break;
		}
		hang_detect_reload();
		break;

	case EC_HANG_DETECT_CMD_CANCEL:
		/* Handle cancel hang timer on request */
		hang_detect_cancel();
		/* Clear reboot timeout - it must be set every watchdog setup */
		reboot_timeout_sec = 0;
		break;

	case EC_HANG_DETECT_CMD_SET_TIMEOUT:
		if (p->reboot_timeout_sec < EC_HANG_DETECT_MIN_TIMEOUT) {
			CPRINTS("Reboot timeout has to be greater than %ds",
				EC_HANG_DETECT_MIN_TIMEOUT);
			ret = EC_RES_INVALID_PARAM;
			break;
		}

		/* Cancel currently running AP hang detect timer */
		hang_detect_cancel();
		/* Save new reboot timeout */
		reboot_timeout_sec = p->reboot_timeout_sec;
		CPRINTS("reboot timeout: %d(s)", reboot_timeout_sec);
		break;

	case EC_HANG_DETECT_CMD_GET_STATUS:
		ec_reason = chipset_get_shutdown_reason();
		args->response_size = sizeof(*r);
		/**
		 * chipset_get_shutdown_reason() provides the last reason the EC
		 * has rebooted AP. It is not aware of any AP-initiated reboot
		 * or shutdown. For example, if EC-watchdog triggered the AP
		 * reboot and later the AP was powered off or rebooted (e.g.
		 * with reboot command or powered-off in UI) the
		 * chipset_get_shutdown_reason() will still return the
		 * CHIPSET_RESET_HANG_REBOOT as the last reset reason. To
		 * address this issue, the watchdog kernel module has a shutdown
		 * callback that sends EC_CMD_HANG_DETECT with
		 * EC_HANG_DETECT_CMD_CLEAR_STATUS set every time the AP is
		 * shutting down or rebooting gracefully (gracefully here means
		 * "not triggered by watchdog") to inform that AP is closing
		 * normally.
		 */
		if (ec_reason == CHIPSET_RESET_HANG_REBOOT &&
		    bootstatus == EC_HANG_DETECT_AP_BOOT_EC_WDT)
			r->status = EC_HANG_DETECT_AP_BOOT_EC_WDT;
		else
			r->status = EC_HANG_DETECT_AP_BOOT_NORMAL;
		CPRINTS("EC Watchdog status %d", r->status);
		break;

	case EC_HANG_DETECT_CMD_CLEAR_STATUS:
		CPRINTS("Clearing bootstatus");
		bootstatus = EC_HANG_DETECT_AP_BOOT_NORMAL;
		break;

	default:
		CPRINTS("Unknown command (%04x)", p->command);
		ret = EC_RES_INVALID_PARAM;
		break;
	}

	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_HANG_DETECT, hang_detect_host_command,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console command */

static int command_hang_detect(int argc, const char **argv)
{
	ccprintf("reboot timeout: %d(s)\n", reboot_timeout_sec);
	ccprintf("bootstatus: %02x\n", bootstatus);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hangdet, command_hang_detect, NULL,
			"Print hang detect state");
