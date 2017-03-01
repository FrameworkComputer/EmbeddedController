/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* chip/g-specific system function prototypes */

#ifndef __CROS_EC_SYSTEM_CHIP_H
#define __CROS_EC_SYSTEM_CHIP_H

/**
 * On systems with protection from a failing RW update: read the retry counter
 * and act on it.
 *
 * @return EC_SUCCESS if no flash write errors were encounterd.
 */
int system_process_retry_counter(void);

/**
 * On systems with protection from a failing RW update: reset retry
 * counter, this is used after a new image upload is finished, to make
 * sure that the new image has a chance to run.
 */
void system_clear_retry_counter(void);

/**
 * A function provided by some platforms to decrement a retry counter.
 *
 * This should be used whenever a system reset is manually triggered.
 */
void system_decrement_retry_counter(void);

/**
 * A function provided by some platforms to hint that something is going
 * wrong.
 *
 * @return a boolean, set to True if rolling reboot condition is suspected.
 */
int system_rolling_reboot_suspected(void);

/**
 * Compare the rw headers to check if there was a rollback.
 *
 * @return a boolean, set to True if a rollback is detected.
 */
int system_rollback_detected(void);

/**
 * Returns non-zero value when firmware is expected to be abe to detect user
 * request to cut off battery supply.
 */
int system_battery_cutoff_support_required(void);

/**
 * Modify info1 RW rollback mask to match currently executing RW image's
 * header.
 */
void system_update_rollback_mask(void);

#endif /* __CROS_EC_SYSTEM_CHIP_H */
