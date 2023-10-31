/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_reset_log.h"
#include "common.h"
#include "link_defs.h"
#include "system.h"
#include "task.h"

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
