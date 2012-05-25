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
	SYSTEM_RESET_UNKNOWN = 0,  /* Unknown reset cause */
	SYSTEM_RESET_OTHER,        /* System reset cause is known, but not one
				    * of the causes listed below */
	SYSTEM_RESET_BROWNOUT,     /* Brownout */
	SYSTEM_RESET_POWER_ON,     /* Power-on reset */
	SYSTEM_RESET_RESET_PIN,    /* Reset pin asserted */
	SYSTEM_RESET_SOFT,         /* Soft reset trigger by core */
	SYSTEM_RESET_WATCHDOG,     /* Watchdog timer reset */
	SYSTEM_RESET_RTC_ALARM,    /* RTC alarm wake */
	SYSTEM_RESET_WAKE_PIN,     /* Wake pin triggered wake */
	SYSTEM_RESET_LOW_BATTERY,  /* Low battery triggered wake */
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

/* Returns the cause of the last reset, or SYSTEM_RESET_UNKNOWN if
 * the cause is not known. */
enum system_reset_cause_t system_get_reset_cause(void);

/* Return a Boolean indicating if BIOS should come up in recovery mode */
int system_get_recovery_required(void);

/* Record the cause of the last reset. */
void system_set_reset_cause(enum system_reset_cause_t cause);

/* Return a text description of the last reset cause. */
const char *system_get_reset_cause_string(void);

/* Return non-zero if the system is locked down for normal consumer use.
 * Potentially-dangerous developer and/or factory commands must be disabled
 * unless this command returns 0.
 *
 * This should be controlled by the same mechanism which write-protects the
 * read-only image (so that the only way to unlock the system is to unprotect
 * the read-only image). */
int system_is_locked(void);

/* Disable jumping between images for the rest of this boot. */
void system_disable_jump(void);

/* Return the image copy which is currently running. */
enum system_image_copy_t system_get_image_copy(void);

/* Return non-zero if the system has switched between image copies at least
 * once since the last real boot. */
int system_jumped_to_this_image(void);

/* Preserve data across a jump between images.  <tag> identifies the data
 * type.  <size> must be a multiple of 4 bytes, and less than 255 bytes.
 * <version> is the data version, so that tag data can evolve as firmware
 * is updated.  <data> points to the data to save.
 *
 * This may ONLY be called from within a HOOK_SYSJUMP handler. */
int system_add_jump_tag(uint16_t tag, int version, int size, const void *data);

/* Retrieve data stored by a previous image's call to
 * system_add_jump_tag().  If a matching tag is found, retrieves
 * <size> and <version>, and returns a pointer to the data.  Returns
 * NULL if no matching tag is found. */
const uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size);

/* Return the address just past the last usable byte in RAM. */
int system_usable_ram_end(void);

/* Returns true if the given range is overlapped with the active image. */
int system_unsafe_to_overwrite(uint32_t offset, uint32_t size);

/* Return a text description of the image copy which is currently running. */
const char *system_get_image_copy_string(void);

/* Jump to the specified image copy. */
int system_run_image_copy(enum system_image_copy_t copy,
			  int recovery_request);

/* Return the version string for an image copy, or an empty string if
 * error.  If copy==SYSTEM_IMAGE_UNKNOWN, returns the version for the
 * currently-running image. */
const char *system_get_version(enum system_image_copy_t copy);

/* Return the board version number.  The meaning of this number is
 * board-dependent; see enum board_version in board.h for known versions. */
int system_get_board_version(void);

/* Return information about the build including the version, build date and
 * user/machine which performed the build. */
const char *system_get_build_info(void);

/* Reset the system.  If is_hard, performs a hard reset, which cuts power to
 * the entire system; else performs a soft reset (which resets the core and
 * on-chip peripherals, without actually cutting power to the chip). */
void system_reset(int is_hard);

/* Set a scratchpad register to the specified value.  The scratchpad
 * register must maintain its contents across a software-requested
 * warm reset. */
int system_set_scratchpad(uint32_t value);

/* Return the current scratchpad register value. */
uint32_t system_get_scratchpad(void);

/* Return the chip info */
const char *system_get_chip_vendor(void);
const char *system_get_chip_name(void);
const char *system_get_chip_revision(void);

/* TODO: request sleep.  How do we want to handle transitioning
 * to/from low-power states? */

/* Put the EC in hibernate (lowest EC power state) for the specified
 * duration.  Note that this is NOT the same as chipset S4/hibernate. */
void system_hibernate(uint32_t seconds, uint32_t microseconds);

/* Minimum duration to get proper hibernation */
#define SYSTEM_HIB_MINIMUM_DURATION 0, 1000

#endif  /* __CROS_EC_SYSTEM_H */
