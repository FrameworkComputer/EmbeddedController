/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#ifndef __CROS_EC_KEYBOARD_SCAN_H
#define __CROS_EC_KEYBOARD_SCAN_H

#include "common.h"
#include "keyboard_config.h"

struct keyboard_scan_config {
	/* Delay between setting up output and waiting for it to settle */
	uint16_t output_settle_us;
	/* Times for debouncing key-down and key-up */
	uint16_t debounce_down_us;
	uint16_t debounce_up_us;
	/* Time between start of scans when in polling mode */
	uint16_t scan_period_us;
	/*
	 * Minimum time between end of one scan and start of the next one.
	 * This ensures keyboard scanning doesn't starve the rest of the system
	 * if the scan period is set too short, or if other higher-priority
	 * system activity is starving the keyboard scan task too.
	 */
	uint16_t min_post_scan_delay_us;

	/* Revert to interrupt mode after no keyboard activity for this long */
	uint32_t poll_timeout_us;
	/* Mask with 1 bits only for keys that actually exist */
	uint8_t actual_key_mask[KEYBOARD_COLS];
};

/**
 * Initializes the module.
 */
void keyboard_scan_init(void);

/**
 * Return a pointer to the keyboard scan config.
 */
struct keyboard_scan_config *keyboard_scan_get_config(void);
/*
 * Which is probably this.
 */
extern struct keyboard_scan_config keyscan_config;

/* Key held down at keyboard-controlled reset boot time. */
enum boot_key {
	BOOT_KEY_NONE,  /* No keys other than keyboard-controlled reset keys */
	BOOT_KEY_ESC,
	BOOT_KEY_DOWN_ARROW,
	BOOT_KEY_OTHER = -1,  /* None of the above */
};

#ifdef HAS_TASK_KEYSCAN
/**
 * Return the key held down at boot time in addition to the keyboard-controlled
 * reset keys.  Returns BOOT_KEY_OTHER if none of the keys specifically checked
 * was pressed, or reset was not caused by a keyboard-controlled reset.
 */
enum boot_key keyboard_scan_get_boot_key(void);
#else
static inline enum boot_key keyboard_scan_get_boot_key(void)
{
	return BOOT_KEY_NONE;
}
#endif

/**
 * Return a pointer to the current debounced keyboard matrix state, which is
 * KEYBOARD_COLS bytes long.
 */
const uint8_t *keyboard_scan_get_state(void);

#ifdef HAS_TASK_KEYSCAN
/**
 * Enables/disables keyboard matrix scan.
 */
void keyboard_scan_enable(int enable);
#else
static inline void keyboard_scan_enable(int enable) { }
#endif

/**
 * Returns if keyboard matrix scanning is enabled/disabled.
 */
int keyboard_scan_is_enabled(void);

#ifdef CONFIG_KEYBOARD_SUPPRESS_NOISE
/**
 * Indicate to audio codec that a key has been pressed.
 *
 * Boards may supply this function to suppress audio noise.
 */
void keyboard_suppress_noise(void);
#endif

#endif  /* __CROS_EC_KEYBOARD_SCAN_H */
