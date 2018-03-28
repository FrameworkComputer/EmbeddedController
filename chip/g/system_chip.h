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
 * Returns True if a rollback was detected during system_preinit.
 *
 * system_rollback_detected only returns True from rollback until the AP boots
 * and then enters deep sleep. The system won't know that it rolled back on
 * resume from deep sleep.
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
 * Functions to update INFO1 rollback mask based on one or both RW image
 * headers.
 */
void system_update_rollback_mask_with_active_img(void);
void system_update_rollback_mask_with_both_imgs(void);

/**
 * Scan INFO1 rollback map and infomap fields of both RW and RW_B image
 * headers, and return a string showing how many zeros are there at the base
 * of in each of these objects.
 *
 * The passed in parameters are the memory area to put the string in and the
 * size of this memory area.
 */
void system_get_rollback_bits(char *value, size_t value_size);

/**
 * Set the rollback counter to a value which would ensure a rollback on the
 * next boot.
 */
void system_ensure_rollback(void);

/**
 * Enables holding external pins across soft chip resets. Application firmware
 * is responsible for disengaging pinhold upon reset.
 */
void system_pinhold_on_reset_enable(void);

/**
 * Disables holding external pins across soft chip resets.
 */
void system_pinhold_on_reset_disable(void);

/**
 * Disengages pinhold if engaged.
 */
void system_pinhold_disengage(void);

#endif /* __CROS_EC_SYSTEM_CHIP_H */
