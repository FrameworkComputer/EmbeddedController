/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset common code for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_POWER_AP
static int command_apreset(int argc, char **argv)
{
	/* Force the chipset to reset */
	ccprintf("Issuing AP reset...\n");
	chipset_reset(CHIPSET_RESET_CONSOLE_CMD);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apreset, command_apreset,
			NULL,
			"Issue AP reset");

static int command_apshutdown(int argc, char **argv)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_CONSOLE_CMD);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apshutdown, command_apshutdown,
			NULL,
			"Force AP shutdown");

#endif

#ifdef CONFIG_HOSTCMD_AP_RESET
static int host_command_apreset(struct host_cmd_handler_args *args)
{
	/* Force the chipset to reset */
	chipset_reset(CHIPSET_RESET_HOST_CMD);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_AP_RESET,
		     host_command_apreset,
		     EC_VER_MASK(0));

#endif

#ifdef CONFIG_CMD_AP_RESET_LOG
static struct mutex reset_log_mutex;
static int next_reset_log;
static uint32_t ap_resets_since_ec_boot;
/* keep reset_logs size a power of 2 */
static struct ap_reset_log_entry reset_logs[4];

void report_ap_reset(enum chipset_shutdown_reason reason)
{
	timestamp_t now = get_time();
	uint32_t now_ms = (uint32_t)(now.val / MSEC);

	mutex_lock(&reset_log_mutex);
	reset_logs[next_reset_log].reset_cause = reason;
	reset_logs[next_reset_log++].reset_time_ms = now_ms;
	next_reset_log &= ARRAY_SIZE(reset_logs) - 1;
	ap_resets_since_ec_boot++;
	mutex_unlock(&reset_log_mutex);
}

static int host_command_get_uptime_info(struct host_cmd_handler_args *args)
{
	/*
	 * In the current implementation, not all terms are preserved across a
	 * sysjump.  Future implementations may preserve additional information.
	 *
	 * time_since_ec_boot_ms:   preserved, but wraps at ~50 days
	 * ec_reset_flags:          preserved, with 'sysjump' added
	 * ap_resets_since_ec_boot: Not preserved
	 * recent_ap_reset[*]:      Not preserved
	 */
	struct ec_response_uptime_info *r = args->response;
	timestamp_t now = get_time();
	uint32_t now_ms = (uint32_t)(now.val / MSEC);
	size_t log_address = 0;
	size_t i = 0;

	r->time_since_ec_boot_ms = now_ms;
	r->ec_reset_flags = system_get_reset_flags();

	memset(r->recent_ap_reset, 0, sizeof(r->recent_ap_reset));

	mutex_lock(&reset_log_mutex);
	r->ap_resets_since_ec_boot = ap_resets_since_ec_boot;
	for (i = 0;
	     i != ARRAY_SIZE(reset_logs) && i != ARRAY_SIZE(r->recent_ap_reset);
	     ++i) {
		log_address = (next_reset_log + i) &
			(ARRAY_SIZE(reset_logs) - 1);
		r->recent_ap_reset[i] = reset_logs[log_address];
	}
	mutex_unlock(&reset_log_mutex);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_UPTIME_INFO,
		     host_command_get_uptime_info,
		     EC_VER_MASK(0));

#endif  /* !CONFIG_AP_RESET_LOG */

