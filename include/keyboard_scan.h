/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#ifndef __CROS_EC_KEYBOARD_SCAN_H
#define __CROS_EC_KEYBOARD_SCAN_H

#include "common.h"
#include "compile_time_macros.h"
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
	uint8_t actual_key_mask[KEYBOARD_COLS_MAX];
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
	/* No keys other than keyboard-controlled reset keys */
	BOOT_KEY_NONE = 0,
	BOOT_KEY_ESC = BIT(0),
	BOOT_KEY_DOWN_ARROW = BIT(1),
	BOOT_KEY_LEFT_SHIFT = BIT(2),
};

#if defined(HAS_TASK_KEYSCAN) && defined(CONFIG_KEYBOARD_BOOT_KEYS)
/**
 * Returns mask of all the keys held down at boot time in addition to the
 * keyboard-controlled reset keys. If more than one boot key is held, mask bits
 * will be set for each of those keys. Since more than one bit can be set,
 * caller needs to ensure that boot keys match as intended.
 *
 * Returns BOOT_NONE if no additional key is held or if none of the keys
 * specifically checked was pressed, or reset was not caused by a
 * keyboard-controlled reset or if any key *other* than boot keys, power, or
 * refresh is also pressed.
 */
uint32_t keyboard_scan_get_boot_keys(void);
#else
static inline uint32_t keyboard_scan_get_boot_keys(void)
{
	return BOOT_KEY_NONE;
}
#endif

/**
 * Return a pointer to the current debounced keyboard matrix state, which is
 * KEYBOARD_COLS_MAX bytes long.
 */
const uint8_t *keyboard_scan_get_state(void);

enum kb_scan_disable_masks {
	/* Reasons why keyboard scanning should be disabled */
	KB_SCAN_DISABLE_LID_CLOSED   = (1<<0),
	KB_SCAN_DISABLE_POWER_BUTTON = (1<<1),
	KB_SCAN_DISABLE_LID_ANGLE    = (1<<2),
	KB_SCAN_DISABLE_USB_SUSPENDED = (1<<3),
};

#ifdef HAS_TASK_KEYSCAN
/**
 * Enable/disable keyboard scanning. Scanning will be disabled if any disable
 * reason bit is set. Scanning is enabled only if no disable reasons are set.
 *
 * @param enable Clear(=1) or set(=0) disable-bits from the mask.
 * @param mask Disable reasons from kb_scan_disable_masks
 */
void keyboard_scan_enable(int enable, enum kb_scan_disable_masks mask);

/**
 * Clears typematic key
 */
void clear_typematic_key(void);
#else
static inline void keyboard_scan_enable(int enable,
		enum kb_scan_disable_masks mask) { }
#endif

#ifdef CONFIG_KEYBOARD_SUPPRESS_NOISE
/**
 * Indicate to audio codec that a key has been pressed.
 *
 * Boards may supply this function to suppress audio noise.
 */
void keyboard_suppress_noise(void);
#endif

#ifdef CONFIG_KEYBOARD_LANGUAGE_ID
/**
 * Get the KEYBOARD ID for a keyboard
 *
 * @return A value that identifies keyboard variants. Its meaning and
 * the number of bits actually used is the supported keyboard layout.
 */
int keyboard_get_keyboard_id(void);
#endif

#ifdef CONFIG_KEYBOARD_RUNTIME_KEYS
void set_vol_up_key(uint8_t row, uint8_t col);
#else
static inline void set_vol_up_key(uint8_t row, uint8_t col) {}
#endif

#endif  /* __CROS_EC_KEYBOARD_SCAN_H */
