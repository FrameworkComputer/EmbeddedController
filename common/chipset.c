/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset common code for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "link_defs.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_POWER_AP
static int command_apreset(int argc, const char **argv)
{
	/* Force the chipset to reset */
	ccprintf("Issuing AP reset...\n");
	chipset_reset(CHIPSET_RESET_CONSOLE_CMD);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apreset, command_apreset, NULL, "Issue AP reset");

static int command_apshutdown(int argc, const char **argv)
{
	/*
	 * TODO: Fix the problem that CHIPSET_SHUTDOWN_CONSOLE_CMD is
	 * overwritten by CHIPSET_SHUTDOWN_POWERFAIL (in intel_x86.c).
	 */
	if (IS_ENABLED(CONFIG_POWER_BUTTON_INIT_IDLE)) {
		chip_save_reset_flags(chip_read_reset_flags() |
				      EC_RESET_FLAG_AP_IDLE);
		system_set_reset_flags(EC_RESET_FLAG_AP_IDLE);
		CPRINTS("Saved AP_IDLE flag");
	}

	chipset_force_shutdown(CHIPSET_SHUTDOWN_CONSOLE_CMD);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apshutdown, command_apshutdown, NULL,
			"Force AP shutdown");

#endif

#ifdef CONFIG_HOSTCMD_AP_RESET
static enum ec_status host_command_apreset(struct host_cmd_handler_args *args)
{
	/* Force the chipset to reset */
	chipset_reset(CHIPSET_RESET_HOST_CMD);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_AP_RESET, host_command_apreset, EC_VER_MASK(0));

#endif

#ifdef CONFIG_CMD_AP_RESET_LOG
K_MUTEX_DEFINE(reset_log_mutex);
static int next_reset_log __preserved_logs(next_reset_log);
static uint32_t ap_resets_since_ec_boot;
/* keep reset_logs size a power of 2 */
static struct ap_reset_log_entry reset_logs[4] __preserved_logs(reset_logs);
static int reset_log_checksum __preserved_logs(reset_log_checksum);

/* Calculate reset log checksum */
static int calc_reset_log_checksum(void)
{
	return next_reset_log ^ reset_logs[next_reset_log].reset_cause;
}

/* Initialize reset logs and next reset log */
void init_reset_log(void)
{
	if (next_reset_log < 0 || next_reset_log >= ARRAY_SIZE(reset_logs) ||
	    reset_log_checksum != calc_reset_log_checksum()) {
		reset_log_checksum = 0;
		next_reset_log = 0;
		memset(&reset_logs, 0, sizeof(reset_logs));
	}
}

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

	/* Update checksum */
	reset_log_checksum = calc_reset_log_checksum();
}

test_mockable enum ec_error_list
get_ap_reset_stats(struct ap_reset_log_entry *reset_log_entries,
		   size_t num_reset_log_entries, uint32_t *resets_since_ec_boot)
{
	size_t log_address;
	size_t i;

	if (reset_log_entries == NULL || resets_since_ec_boot == NULL)
		return EC_ERROR_INVAL;

	mutex_lock(&reset_log_mutex);
	*resets_since_ec_boot = ap_resets_since_ec_boot;
	for (i = 0; i != ARRAY_SIZE(reset_logs) && i != num_reset_log_entries;
	     ++i) {
		log_address = (next_reset_log + i) &
			      (ARRAY_SIZE(reset_logs) - 1);
		reset_log_entries[i] = reset_logs[log_address];
	}
	mutex_unlock(&reset_log_mutex);

	return EC_SUCCESS;
}

enum chipset_shutdown_reason chipset_get_shutdown_reason(void)
{
	enum chipset_shutdown_reason reason = CHIPSET_RESET_UNKNOWN;

	mutex_lock(&reset_log_mutex);
	if (ap_resets_since_ec_boot != 0) {
		int i = (next_reset_log == 0) ? ARRAY_SIZE(reset_logs) - 1 :
						next_reset_log - 1;
		reason = reset_logs[i].reset_cause;
	}
	mutex_unlock(&reset_log_mutex);

	return reason;
}

#endif /* !CONFIG_AP_RESET_LOG */

#ifdef TEST_BUILD
uint32_t test_chipset_get_ap_resets_since_ec_boot(void)
{
	uint32_t count;

	mutex_lock(&reset_log_mutex);
	count = ap_resets_since_ec_boot;
	mutex_unlock(&reset_log_mutex);

	return count;
}

void test_chipset_corrupt_reset_log_checksum(void)
{
	reset_log_checksum = ~reset_log_checksum;
}
#endif /* TEST_BUILD */
