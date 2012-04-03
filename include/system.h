/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#ifndef __CROS_EC_SYSTEM_H
#define __CROS_EC_SYSTEM_H

#include "common.h"

/* Reset causes */
enum system_reset_cause_t {
	/* Unknown reset cause */
	SYSTEM_RESET_UNKNOWN = 0,
	/* System reset cause is known, but not one of the causes
	 * listed below */
	SYSTEM_RESET_OTHER,
	/* Brownout */
	SYSTEM_RESET_BROWNOUT,
	/* Power-on reset */
	SYSTEM_RESET_POWER_ON,
	/* Reset caused by asserting reset (RST#) pin */
	SYSTEM_RESET_RESET_PIN,
	/* Software requested cold reset */
	SYSTEM_RESET_SOFT_COLD,
	/* Software requested warm reset */
	SYSTEM_RESET_SOFT_WARM,
	/* Watchdog timer reset */
	SYSTEM_RESET_WATCHDOG,
	/* the RTC alarm triggered power on */
	SYSTEM_RESET_RTC_ALARM,
	/* the Wake pin triggered power on */
	SYSTEM_RESET_WAKE_PIN,
	/* the low battery detection triggered power on */
	SYSTEM_RESET_LOW_BATTERY,
};

/* System images */
enum system_image_copy_t {
	SYSTEM_IMAGE_UNKNOWN = 0,
	SYSTEM_IMAGE_RO,
	SYSTEM_IMAGE_RW_A,
	SYSTEM_IMAGE_RW_B
};

/* Pre-initializes the module.  This occurs before clocks or tasks are
 * set up. */
int system_pre_init(void);

/* System common pre-initialization; called after chip-specific
 * system_pre_init(). */
int system_common_pre_init(void);

/* Initializes the system module. */
int system_init(void);

/* Returns the cause of the last reset, or SYSTEM_RESET_UNKNOWN if
 * the cause is not known. */
enum system_reset_cause_t system_get_reset_cause(void);

/* Record the cause of the last reset. */
void system_set_reset_cause(enum system_reset_cause_t cause);

/* Returns a text description of the last reset cause. */
const char *system_get_reset_cause_string(void);

/* Returns the image copy which is currently running. */
enum system_image_copy_t system_get_image_copy(void);

/* Returns non-zero if the system has switched between image copies at least
 * once since the last real boot. */
int system_jumped_to_this_image(void);

/* Returns true if the given range is overlapped with the active image. */
int system_unsafe_to_overwrite(uint32_t offset, uint32_t size);

/* Returns a text description of the image copy which is currently running. */
const char *system_get_image_copy_string(void);

/* Jumps to the specified image copy.  Only works from RO firmware. */
int system_run_image_copy(enum system_image_copy_t copy);

/* Returns the version string for an image copy, or an empty string if
 * error.  If copy==SYSTEM_IMAGE_UNKNOWN, returns the version for the
 * currently-running image. */
const char *system_get_version(enum system_image_copy_t copy);

/* Returns information about the build including the version
 * the build date and user/machine.
 */
const char *system_get_build_info(void);

/* Resets the system.  If is_cold!=0, performs a cold reset (which
 * resets on-chip peripherals); else performs a warm reset (which does
 * not reset on-chip peripherals).  If successful, does not return.
 * Returns error if the reboot type cannot be requested (e.g. brownout
 * or reset pin). */
int system_reset(int is_cold);

/* Sets a scratchpad register to the specified value.  The scratchpad
 * register must maintain its contents across a software-requested
 * warm reset. */
int system_set_scratchpad(uint32_t value);

/* Returns the current scratchpad register value. */
uint32_t system_get_scratchpad(void);

/* TODO: request sleep.  How do we want to handle transitioning
 * to/from low-power states? */

/* put the system in hibernation for the specified duration */
void system_hibernate(uint32_t seconds, uint32_t microseconds);

/* minimum duration to get proper hibernation */
#define SYSTEM_HIB_MINIMUM_DURATION 0, 1000

#endif  /* __CROS_EC_SYSTEM_H */
