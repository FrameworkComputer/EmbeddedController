/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#ifndef __CROS_EC_KEYBOARD_SCAN_H
#define __CROS_EC_KEYBOARD_SCAN_H

#include "common.h"
#include "compile_time_macros.h"
#include "keyboard_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_ZTEST
extern uint8_t key_vol_up_row;
extern uint8_t key_vol_up_col;
#endif

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
#ifdef CONFIG_KEYBOARD_SCAN_ADC
	/* KSI threshold ADC voltage in mV */
	uint16_t ksi_threshold_mv;
#endif
};

/* Boot key list.  Must be in same order as enum boot_key. */
struct boot_key_entry {
	uint8_t col;
	uint8_t row;
};

/*
 * WARNING: Do not directly modify it. You should call keyboard_raw_set_cols,
 * instead. It checks whether you're eligible or not.
 */
extern uint8_t keyboard_cols;

/**
 * Get the current keyboard column size.
 */
uint8_t keyboard_get_cols(void);

/**
 * Set keyboard scan matrix column size. The size can be safely changed only
 * before scanning starts.
 */
void keyboard_set_cols(uint8_t cols);

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
__override_proto extern struct keyboard_scan_config keyscan_config;

#define BOOT_KEY_NONE 0

/* Key held down at keyboard-controlled reset boot time. */
enum boot_key {
	/* No keys other than keyboard-controlled reset keys */
	BOOT_KEY_ESC = 0,
	BOOT_KEY_DOWN_ARROW = 1,
	BOOT_KEY_LEFT_SHIFT = 2,
	BOOT_KEY_REFRESH = 3,

	BOOT_KEY_COUNT,
	/* 31 is reserved for power button. */
	BOOT_KEY_POWER = 31,
};
BUILD_ASSERT(BOOT_KEY_COUNT < 31);

#if (defined(HAS_TASK_KEYSCAN) && defined(CONFIG_KEYBOARD_BOOT_KEYS)) || \
	defined(CONFIG_CROS_EC_BOOT_KEYS)
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
	KB_SCAN_DISABLE_LID_CLOSED = (1 << 0),
	KB_SCAN_DISABLE_POWER_BUTTON = (1 << 1),
	KB_SCAN_DISABLE_LID_ANGLE = (1 << 2),
	KB_SCAN_DISABLE_USB_SUSPENDED = (1 << 3),
};

#if defined(HAS_TASK_KEYSCAN) || defined(CONFIG_CROS_EC_KEYBOARD_INPUT)
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
					enum kb_scan_disable_masks mask)
{
}
#endif

#ifdef CONFIG_KEYBOARD_RUNTIME_KEYS
void set_vol_up_key(uint8_t row, uint8_t col);
#else
static inline void set_vol_up_key(uint8_t row, uint8_t col)
{
}
#endif

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 */
extern const int keyboard_factory_scan_pins[][2];
extern const int keyboard_factory_scan_pins_used;
#endif

#ifdef CONFIG_KEYBOARD_MULTIPLE
extern struct boot_key_entry boot_key_list[];

struct keyboard_type {
	int col_esc;
	int row_esc;
	int col_down;
	int row_down;
	int col_left_shift;
	int row_left_shift;
	int col_refresh;
	int row_refresh;
	int col_right_alt;
	int row_right_alt;
	int col_left_alt;
	int row_left_alt;
	int col_key_r;
	int row_key_r;
	int col_key_h;
	int row_key_h;
};

extern struct keyboard_type key_typ;
#endif

#ifdef TEST_BUILD
/**
 * @brief Get the value of print_state_changes
 *
 * @return non-zero if state change printing is enabled, zero if not.
 */
__test_only int keyboard_scan_get_print_state_changes(void);

/**
 * @brief Forcibly set the value of print_state_changes
 *
 * @param val Value to set
 */
__test_only void keyboard_scan_set_print_state_changes(int val);

/**
 * @brief Checks if keyboard scanning is currently enabled.
 *
 * @return int non-zero if enabled, zero otherwise.
 */
int keyboard_scan_is_enabled(void);

/**
 * @brief Reset key debouncing logic
 */
void test_keyboard_scan_debounce_reset(void);
#endif /* TEST_BUILD */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_KEYBOARD_SCAN_H */
