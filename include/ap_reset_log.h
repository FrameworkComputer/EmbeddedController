/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_AP_RESET_LOG_H
#define __CROS_EC_AP_RESET_LOG_H

#include "common.h"
#include "ec_commands.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_CMD_AP_RESET_LOG

/**
 * Initialize reset logs and next reset log.
 */
void init_reset_log(void);

/**
 * Report that the AP is being reset to the reset log.
 */
void report_ap_reset(enum chipset_shutdown_reason reason);

/**
 * Get statistics about AP resets.
 *
 * @param reset_log_entries       Pointer to array of log entries.
 * @param num_reset_log_entries   Number of items in reset_log_entries.
 * @param resets_since_ec_boot    Number of AP resets since EC boot.
 */
enum ec_error_list
get_ap_reset_stats(struct ap_reset_log_entry *reset_log_entries,
		   size_t num_reset_log_entries,
		   uint32_t *resets_since_ec_boot);

/**
 * Check the reason given in the last call to report_ap_reset() .
 *
 * @return Reason argument that was passed to the last call to
 * report_ap_reset(). Zero if report_ap_reset() has not been called.
 */
enum chipset_shutdown_reason chipset_get_shutdown_reason(void);

#else /* !CONFIG_CMD_AP_RESET_LOG */

static inline void init_reset_log(void)
{
}

static inline void report_ap_reset(enum chipset_shutdown_reason reason)
{
}

test_mockable_static_inline enum ec_error_list
get_ap_reset_stats(struct ap_reset_log_entry *reset_log_entries,
		   size_t num_reset_log_entries, uint32_t *resets_since_ec_boot)
{
	return EC_SUCCESS;
}

static inline enum chipset_shutdown_reason chipset_get_shutdown_reason(void)
{
	return CHIPSET_RESET_UNKNOWN;
}

#endif /* !CONFIG_CMD_AP_RESET_LOG */

#ifdef TEST_BUILD
/**
 * @brief Gets the number of AP resets since the EC booted. Takes the reset log
 *        mutex for thread safety.
 *
 * @return uint32_t AP reset count
 */
uint32_t test_chipset_get_ap_resets_since_ec_boot(void);

/**
 * @brief Corrupts the stored reset log checksum, which forces init_reset_log()
 *        to wipe the log and fully reset.
 */
void test_chipset_corrupt_reset_log_checksum(void);
#endif /* TEST_BUILD */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_AP_RESET_LOG_H */
