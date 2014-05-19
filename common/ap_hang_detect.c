/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AP hang detect logic */

#include "ap_hang_detect.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power_button.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

static struct ec_params_hang_detect hdparams;

static int active;  /* Is hang detect timer active / counting? */
static int timeout_will_reboot;  /* Will the deferred call reboot the AP? */

/**
 * Handle the hang detect timer expiring.
 */
static void hang_detect_deferred(void)
{
	/* If we're no longer active, nothing to do */
	if (!active)
		return;

	/* If we're rebooting the AP, stop hang detection */
	if (timeout_will_reboot) {
		CPRINTS("hang detect triggering warm reboot");
		host_set_single_event(EC_HOST_EVENT_HANG_REBOOT);
		chipset_reset(0);
		active = 0;
		return;
	}

	/* Otherwise, we're starting with the host event */
	CPRINTS("hang detect sending host event");
	host_set_single_event(EC_HOST_EVENT_HANG_DETECT);

	/* If we're also rebooting, defer for the remaining delay */
	if (hdparams.warm_reboot_timeout_msec) {
		CPRINTS("hang detect continuing (for reboot)");
		timeout_will_reboot = 1;
		hook_call_deferred(hang_detect_deferred,
				   (hdparams.warm_reboot_timeout_msec -
				    hdparams.host_event_timeout_msec) * MSEC);
	} else {
		/* Not rebooting, so go back to idle */
		active = 0;
	}
}
DECLARE_DEFERRED(hang_detect_deferred);

/**
 * Start the hang detect timers.
 */
static void hang_detect_start(const char *why)
{
	/* If already active, don't restart timer */
	if (active)
		return;

	if (hdparams.host_event_timeout_msec) {
		CPRINTS("hang detect started on %s (for event)", why);
		timeout_will_reboot = 0;
		active = 1;
		hook_call_deferred(hang_detect_deferred,
				   hdparams.host_event_timeout_msec * MSEC);
	} else if (hdparams.warm_reboot_timeout_msec) {
		CPRINTS("hang detect started on %s (for reboot)", why);
		timeout_will_reboot = 1;
		active = 1;
		hook_call_deferred(hang_detect_deferred,
				   hdparams.warm_reboot_timeout_msec * MSEC);
	}
}

/**
 * Stop the hang detect timers.
 */
static void hang_detect_stop(const char *why)
{
	if (active)
		CPRINTS("hang detect stopped on %s", why);

	active = 0;
}

void hang_detect_stop_on_host_command(void)
{
	if (hdparams.flags & EC_HANG_STOP_ON_HOST_COMMAND)
		hang_detect_stop("host cmd");
}

/*****************************************************************************/
/* Hooks */

static void hang_detect_power_button(void)
{
	if (power_button_is_pressed()) {
		if (hdparams.flags & EC_HANG_START_ON_POWER_PRESS)
			hang_detect_start("power button");
	} else {
		if (hdparams.flags & EC_HANG_STOP_ON_POWER_RELEASE)
			hang_detect_stop("power button");
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, hang_detect_power_button,
	     HOOK_PRIO_DEFAULT);

static void hang_detect_lid(void)
{
	if (lid_is_open()) {
		if (hdparams.flags & EC_HANG_START_ON_LID_OPEN)
			hang_detect_start("lid open");
	} else {
		if (hdparams.flags & EC_HANG_START_ON_LID_CLOSE)
			hang_detect_start("lid close");
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, hang_detect_lid, HOOK_PRIO_DEFAULT);

static void hang_detect_resume(void)
{
	if (hdparams.flags & EC_HANG_START_ON_RESUME)
		hang_detect_start("resume");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, hang_detect_resume, HOOK_PRIO_DEFAULT);

static void hang_detect_suspend(void)
{
	if (hdparams.flags & EC_HANG_STOP_ON_SUSPEND)
		hang_detect_stop("suspend");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, hang_detect_suspend, HOOK_PRIO_DEFAULT);

static void hang_detect_shutdown(void)
{
	/* Stop the timers */
	hang_detect_stop("shutdown");

	/* Disable hang detection; it must be enabled every boot */
	memset(&hdparams, 0, sizeof(hdparams));
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, hang_detect_shutdown, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Host command */

static int hang_detect_host_command(struct host_cmd_handler_args *args)
{
	const struct ec_params_hang_detect *p = args->params;

	/* Handle stopping hang timer on request */
	if (p->flags & EC_HANG_STOP_NOW) {
		hang_detect_stop("ap request");

		/* Ignore the other params */
		return EC_RES_SUCCESS;
	}

	/* Handle starting hang timer on request */
	if (p->flags & EC_HANG_START_NOW) {
		hang_detect_start("ap request");

		/* Ignore the other params */
		return EC_RES_SUCCESS;
	}

	/* If hang detect transitioning to disabled, stop timers */
	if (hdparams.flags && !p->flags)
		hang_detect_stop("ap flags=0");

	/* Save new params */
	hdparams = *p;
	CPRINTS("hang detect flags=0x%x, event=%d ms, reboot=%d ms",
		hdparams.flags, hdparams.host_event_timeout_msec,
		hdparams.warm_reboot_timeout_msec);

	/*
	 * If warm reboot timeout is shorter than host event timeout, ignore
	 * the host event timeout because a warm reboot will win.
	 */
	if (hdparams.warm_reboot_timeout_msec &&
	    hdparams.warm_reboot_timeout_msec <=
	    hdparams.host_event_timeout_msec)
		hdparams.host_event_timeout_msec = 0;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HANG_DETECT,
		     hang_detect_host_command,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console command */

static int command_hang_detect(int argc, char **argv)
{
	ccprintf("flags:  0x%x\n", hdparams.flags);

	ccputs("event:  ");
	if (hdparams.host_event_timeout_msec)
		ccprintf("%d ms\n", hdparams.host_event_timeout_msec);
	else
		ccputs("disabled\n");

	ccputs("reboot: ");
	if (hdparams.warm_reboot_timeout_msec)
		ccprintf("%d ms\n", hdparams.warm_reboot_timeout_msec);
	else
		ccputs("disabled\n");

	ccputs("status: ");
	if (active)
		ccprintf("active for %s\n",
			 timeout_will_reboot ? "reboot" : "event");
	else
		ccputs("inactive\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hangdet, command_hang_detect,
			NULL,
			"Print hang detect state",
			NULL);
