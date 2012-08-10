/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#ifndef __CROS_EC_SYSTEM_H
#define __CROS_EC_SYSTEM_H

#include "common.h"

/* Reset causes */
#define RESET_FLAG_OTHER       (1 << 0)   /* Other known reason */
#define RESET_FLAG_RESET_PIN   (1 << 1)   /* Reset pin asserted */
#define RESET_FLAG_BROWNOUT    (1 << 2)   /* Brownout */
#define RESET_FLAG_POWER_ON    (1 << 3)   /* Power-on reset */
#define RESET_FLAG_WATCHDOG    (1 << 4)   /* Watchdog timer reset */
#define RESET_FLAG_SOFT        (1 << 5)   /* Soft reset trigger by core */
#define RESET_FLAG_HIBERNATE   (1 << 6)   /* Wake from hibernate */
#define RESET_FLAG_RTC_ALARM   (1 << 7)   /* RTC alarm wake */
#define RESET_FLAG_WAKE_PIN    (1 << 8)   /* Wake pin triggered wake */
#define RESET_FLAG_LOW_BATTERY (1 << 9)   /* Low battery triggered wake */
#define RESET_FLAG_SYSJUMP     (1 << 10)  /* Jumped directly to this image */
#define RESET_FLAG_HARD        (1 << 11)  /* Hard reset from software */
#define RESET_FLAG_AP_OFF      (1 << 12)  /* Do not power on AP */
#define RESET_FLAG_PRESERVED   (1 << 13)  /* Some reset flags preserved from
					   * previous boot */

/* System images */
enum system_image_copy_t {
	SYSTEM_IMAGE_UNKNOWN = 0,
	SYSTEM_IMAGE_RO,
	SYSTEM_IMAGE_RW
};

/* Pre-initializes the module.  This occurs before clocks or tasks are
 * set up. */
int system_pre_init(void);

/* System common pre-initialization; called after chip-specific
 * system_pre_init(). */
int system_common_pre_init(void);

/**
 * Get the reset flags.
 *
 * @return Reset flags (RESET_FLAG_*), or 0 if the cause is unknown.
 */
uint32_t system_get_reset_flags(void);

/**
 * Set reset flags.
 *
 * @param flags        Flags to set in reset flags
 */
void system_set_reset_flags(uint32_t flags);

/**
 * Clear reset flags.
 *
 * @param flags        Flags to clear in reset flags
 */
void system_clear_reset_flags(uint32_t flags);

/**
 * Print a description of the reset flags to the console.
 */
void system_print_reset_flags(void);

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

/* Return true if the given range is overlapped with the active image. */
int system_unsafe_to_overwrite(uint32_t offset, uint32_t size);

/* Return a text description of the image copy which is currently running. */
const char *system_get_image_copy_string(void);

/**
 * Return the number of bytes used in the specified image.
 *
 * This is the actual size of code+data in the image, as opposed to the
 * amount of space reserved in flash for that image.
 *
 * @return actual image size in bytes, 0 if the image contains no content or
 * error.
 */
int system_get_image_used(enum system_image_copy_t copy);

/* Jump to the specified image copy. */
int system_run_image_copy(enum system_image_copy_t copy);

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

/* Flags for system_reset() */
/*
 * Hard reset.  Cuts power to the entire system.  If not present, does a soft
 * reset which just resets the core and on-chip peripherals.
 */
#define SYSTEM_RESET_HARD           (1 << 0)
/*
 * Preserve existing reset flags.  Used by flash pre-init when it discovers it
 * needs to do a hard reset to clear write protect registers.
 */
#define SYSTEM_RESET_PRESERVE_FLAGS (1 << 1)
/*
 * Leave AP off on next reboot, instead of powering it on to do EC software
 * sync.
 */
#define SYSTEM_RESET_LEAVE_AP_OFF   (1 << 2)

void system_reset(int flags);

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

/**
 * Put the EC in hibernate (lowest EC power state).
 *
 * @param seconds	Number of seconds to hibernate.
 * @param microseconds	Number of microseconds to hibernate.
 *
 * The EC will hibernate until the wake pin is asserted.  If seconds and/or
 * microseconds is non-zero, the EC will also automatically wake after that
 * period.  If both are zero, the EC will only wake on a wake pin assert.  Very
 * short hibernation delays do not work well; if non-zero, the delays must be
 * at least SYSTEM_HIB_MINIMUM_DURATION.
 *
 * Note although the name is similar, EC hibernate is NOT the same as chipset
 * S4/hibernate.
 */
void system_hibernate(uint32_t seconds, uint32_t microseconds);

/* Minimum duration to get proper hibernation */
#define SYSTEM_HIB_MINIMUM_DURATION 0, 1000

#endif  /* __CROS_EC_SYSTEM_H */
