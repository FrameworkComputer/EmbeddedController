/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#ifndef __CROS_EC_SYSTEM_H
#define __CROS_EC_SYSTEM_H

#include "atomic.h"
#include "common.h"
#include "timer.h"

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

/**
 * Pre-initializes the module.  This occurs before clocks or tasks are
 * set up.
 */
void system_pre_init(void);

/**
 * System common pre-initialization; called after chip-specific
 * system_pre_init().
 */
void system_common_pre_init(void);

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

/**
 * Check if system is locked down for normal consumer use.
 *
 * @return non-zero if the system is locked down for normal consumer use.
 * Potentially-dangerous developer and/or factory commands must be disabled
 * unless this command returns 0.
 *
 * This should be controlled by the same mechanism which write-protects the
 * read-only image (so that the only way to unlock the system is to unprotect
 * the read-only image).
 */
int system_is_locked(void);

/**
 * Disable jumping between images for the rest of this boot.
 */
void system_disable_jump(void);

/**
 * Return the image copy which is currently running.
 */
enum system_image_copy_t system_get_image_copy(void);

/**
 * Return non-zero if the system has switched between image copies at least
 * once since the last real boot.
 */
int system_jumped_to_this_image(void);

/**
 * Preserve data across a jump between images.
 *
 * This may ONLY be called from within a HOOK_SYSJUMP handler.
 *
 * @param tag		Data type
 * @param size          Size of data; must be less than 255 bytes.
 * @param version       Data version, so that tag data can evolve as firmware
 *			is updated.
 * @param data		Pointer to data to save
 * @return EC_SUCCESS, or non-zero if error.
 */
int system_add_jump_tag(uint16_t tag, int version, int size, const void *data);

/**
 * Retrieve previously stored jump data
 *
 * This retrieves data stored by a previous image's call to
 * system_add_jump_tag().
 *
 * @param tag		Data type to retrieve
 * @param version	Set to data version if successful
 * @param size		Set to data size if successful
 * @return		A pointer to the data, or NULL if no matching tag is
 *			found.  This pointer will be 32-bit aligned.
 */
const uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size);

/**
 * Return the address just past the last usable byte in RAM.
 */
uintptr_t system_usable_ram_end(void);

/**
 * Return non-zero if the given range is overlapped with the active image.
 */
int system_unsafe_to_overwrite(uint32_t offset, uint32_t size);

/**
 * Return a text description of the image copy which is currently running.
 */
const char *system_get_image_copy_string(void);

/**
 * Return a text description of the passed image copy parameter.
 */
const char *system_image_copy_t_to_string(enum system_image_copy_t copy);

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

/**
 * Jump to the specified image copy.
 */
int system_run_image_copy(enum system_image_copy_t copy);

/**
 * Get the version string for an image
 *
 * @param copy		Image copy to get version from, or SYSTEM_IMAGE_UNKNOWN
 *			to get the version for the currently running image.
 * @return The version string for the image copy, or an empty string if
 * error.
 */
const char *system_get_version(enum system_image_copy_t copy);

/**
 * Return the board version number.  The meaning of this number is
 * board-dependent; boards where the code actually cares about this should
 * declare enum board_version in board.h.
 */
int system_get_board_version(void);

/**
 * Return information about the build including the version, build date and
 * user/machine which performed the build.
 */
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

/**
 * Reset the system.
 *
 * @param flags		Reset flags; see SYSTEM_RESET_* above.
 */
void system_reset(int flags);

/**
 * Set a scratchpad register to the specified value.
 *
 * The scratchpad register must maintain its contents across a
 * software-requested warm reset.
 *
 * @param value		Value to store.
 * @return EC_SUCCESS, or non-zero if error.
 */
int system_set_scratchpad(uint32_t value);

/**
 * Return the current scratchpad register value.
 */
uint32_t system_get_scratchpad(void);

/**
 * Return the chip vendor/name/revision string.
 */
const char *system_get_chip_vendor(void);
const char *system_get_chip_name(void);
const char *system_get_chip_revision(void);

/**
 * Get/Set VbNvContext in non-volatile storage.  The block should be 16 bytes
 * long, which is the current size of VbNvContext block.
 *
 * @param block		Pointer to a buffer holding VbNvContext.
 * @return 0 on success, !0 on error.
 */
int system_get_vbnvcontext(uint8_t *block);
int system_set_vbnvcontext(const uint8_t *block);

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
#define SYSTEM_HIB_MINIMUM_DURATION 0, 150000

/**
 * Get/Set console force enable status. This is only supported/used on platform
 * with CONFIG_CONSOLE_RESTRICTED_INPUT defined.
 */
int system_get_console_force_enabled(void);
int system_set_console_force_enabled(int enabled);

/**
 * Read the real-time clock.
 *
 * @return The real-time clock value as a timestamp.
 */
timestamp_t system_get_rtc(void);

/**
 * Enable hibernate interrupt
 */
void system_enable_hib_interrupt(void);

/* Low power modes for idle API */
enum {
	/*
	 * Sleep masks to prevent going in to deep sleep.
	 */
	SLEEP_MASK_AP_RUN   = (1 << 0), /* the main CPU is running */
	SLEEP_MASK_UART     = (1 << 1), /* UART communication on-going */
	SLEEP_MASK_I2C      = (1 << 2), /* I2C master communication on-going */
	SLEEP_MASK_CHARGING = (1 << 3), /* Charging loop on-going */
	SLEEP_MASK_USB_PWR  = (1 << 4), /* USB power loop on-going */
	SLEEP_MASK_USB_PD   = (1 << 5), /* USB PD device connected */
	SLEEP_MASK_SPI      = (1 << 6), /* SPI communications on-going */

	SLEEP_MASK_FORCE_NO_DSLEEP    = (1 << 15), /* Force disable. */


	/*
	 * Sleep masks to prevent using slow speed clock in deep sleep.
	 */
	SLEEP_MASK_JTAG     = (1 << 16), /* JTAG is in use. */
	SLEEP_MASK_CONSOLE  = (1 << 17), /* Console is in use. */

	SLEEP_MASK_FORCE_NO_LOW_SPEED = (1 << 31)  /* Force disable. */
};

/*
 * Current sleep mask. You may read from this variable, but must NOT
 * modify it; use enable_sleep() or disable_sleep() to do that.
 */
extern uint32_t sleep_mask;

/*
 * Macros to use to get whether deep sleep is allowed or whether
 * low speed deep sleep is allowed.
 */

#ifndef CONFIG_LOW_POWER_S0
#define DEEP_SLEEP_ALLOWED           (!(sleep_mask & 0x0000ffff))
#else
#define DEEP_SLEEP_ALLOWED           (!(sleep_mask & 0x0000ffff & \
				       (~SLEEP_MASK_AP_RUN)))
#endif
#define LOW_SPEED_DEEP_SLEEP_ALLOWED (!(sleep_mask & 0xffff0000))

/**
 * Enable low power sleep mask. For low power sleep to take affect, all masks
 * in the sleep mask enum above must be enabled.
 *
 * @param Sleep mask to enable.
 */
static inline void enable_sleep(uint32_t mask)
{
	atomic_clear(&sleep_mask, mask);
}

/**
 * Disable low power sleep mask. For low power sleep to take affect, all masks
 * in the sleep mask enum above must be enabled.
 *
 * @param Sleep mask to enable.
 */
static inline void disable_sleep(uint32_t mask)
{
	atomic_or(&sleep_mask, mask);
}

/**
 * Use hibernate module to set up an RTC interrupt at a given
 * time from now
 *
 * Note: If time given is less than HIB_SET_RTC_MATCH_DELAY_USEC, then it will
 * set the interrupt at exactly HIB_SET_RTC_MATCH_DELAY_USEC.
 *
 * @param seconds      Number of seconds before RTC interrupt
 * @param microseconds Number of microseconds before RTC interrupt
 */
void system_set_rtc_alarm(uint32_t seconds, uint32_t microseconds);

/**
 * Disable and clear the RTC interrupt.
 */
void system_reset_rtc_alarm(void);

#ifdef CONFIG_CODERAM_ARCH
/**
 * Determine which address should be jumped and return address of littel FW
 *
 * Note: This feature is used for code ram arch
 *
 * @param flash_addr  jump address of spi flash for RO or RW region
 */
uint32_t system_get_lfw_address(uint32_t flash_addr);

/**
 * Return whcih region is used in Code RAM
 *
 * Note: This feature is used for code ram arch
 *
 */
enum system_image_copy_t system_get_shrspi_image_copy(void);
#endif
#endif  /* __CROS_EC_SYSTEM_H */
