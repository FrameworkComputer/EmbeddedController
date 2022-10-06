/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#include "adc.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "keyboard_test.h"
#include "lid_switch.h"
#include "printf.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "usb_api.h"
#include "util.h"

#ifdef CONFIG_KEYBOARD_MULTIPLE
#include "keyboard_customization.h"
#endif

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYSCAN, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYSCAN, format, ##args)
#define CPRINTS(format, args...) cprints(CC_KEYSCAN, format, ##args)

#ifdef CONFIG_KEYBOARD_DEBUG
#define CPUTS5(outstr) cputs(CC_KEYSCAN, outstr)
#define CPRINTS5(format, args...) cprints(CC_KEYBOARD, format, ##args)
#else
#define CPUTS5(outstr)
#define CPRINTS5(format, args...)
#endif

#define SCAN_TIME_COUNT 32 /* Number of last scan times to track */

/* If we're waiting for a scan to happen, we'll give it this long */
#define SCAN_TASK_TIMEOUT_US (100 * MSEC)

#ifndef CONFIG_KEYBOARD_POST_SCAN_CLOCKS
/*
 * Default delay in clocks; this was experimentally determined to be long
 * enough to avoid watchdog warnings or I2C errors on a typical notebook
 * config on STM32.
 */
#define CONFIG_KEYBOARD_POST_SCAN_CLOCKS 16000
#endif

__overridable struct keyboard_scan_config keyscan_config = {
#ifdef CONFIG_KEYBOARD_COL2_INVERTED
	/*
	 * CONFIG_KEYBOARD_COL2_INVERTED is defined for passing the column 2
	 * to H1 which inverts the signal. The signal passing through H1
	 * adds more delay. Need a larger delay value. Otherwise, pressing
	 * Refresh key will also trigger T key, which is in the next scanning
	 * column line. See http://b/156007029.
	 */
	.output_settle_us = 80,
#else
	.output_settle_us = 50,
#endif /* CONFIG_KEYBOARD_COL2_INVERTED */
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

#ifdef CONFIG_KEYBOARD_BOOT_KEYS
#ifndef CONFIG_KEYBOARD_MULTIPLE
static const struct boot_key_entry boot_key_list[] = {
	{ KEYBOARD_COL_ESC, KEYBOARD_ROW_ESC }, /* Esc */
	{ KEYBOARD_COL_DOWN, KEYBOARD_ROW_DOWN }, /* Down-arrow */
	{ KEYBOARD_COL_LEFT_SHIFT, KEYBOARD_ROW_LEFT_SHIFT }, /* Left-Shift */
};
#else
struct boot_key_entry boot_key_list[] = {
	{ KEYBOARD_COL_ESC, KEYBOARD_ROW_ESC }, /* Esc */
	{ KEYBOARD_COL_DOWN, KEYBOARD_ROW_DOWN }, /* Down-arrow */
	{ KEYBOARD_COL_LEFT_SHIFT, KEYBOARD_ROW_LEFT_SHIFT }, /* Left-Shift */
};
#endif
static uint32_t boot_key_value = BOOT_KEY_NONE;
#endif

uint8_t keyboard_cols = KEYBOARD_COLS_MAX;

/* Debounced key matrix */
static uint8_t debounced_state[KEYBOARD_COLS_MAX];
/* Mask of keys being debounced */
static uint8_t debouncing[KEYBOARD_COLS_MAX];
/* Keys simulated-pressed */
static uint8_t simulated_key[KEYBOARD_COLS_MAX];
#ifdef CONFIG_KEYBOARD_LANGUAGE_ID
static uint8_t keyboard_id[KEYBOARD_IDS];
#endif

/* Times of last scans */
static uint32_t scan_time[SCAN_TIME_COUNT];
/* Current scan_time[] index */
static int scan_time_index;

/* Index into scan_time[] when each key started debouncing */
static uint8_t scan_edge_index[KEYBOARD_COLS_MAX][KEYBOARD_ROWS];

/* Minimum delay between keyboard scans based on current clock frequency */
static uint32_t post_scan_clock_us;

/*
 * Print all keyboard scan state changes?  Off by default because it generates
 * a lot of debug output, which makes the saved EC console data less useful.
 */
static int print_state_changes;

/* Must init to 0 for scanning at boot */
static volatile uint32_t disable_scanning_mask;

/* Constantly incrementing counter of the number of times we polled */
static volatile int kbd_polls;

/* If true, we'll force a keyboard poll */
static volatile int force_poll;

test_export_static int keyboard_scan_is_enabled(void)
{
	/* NOTE: this is just an instantaneous glimpse of the variable. */
	return !disable_scanning_mask;
}

void keyboard_scan_enable(int enable, enum kb_scan_disable_masks mask)
{
	/* Access atomically */
	if (enable) {
		atomic_clear_bits((atomic_t *)&disable_scanning_mask, mask);
	} else {
		atomic_or((atomic_t *)&disable_scanning_mask, mask);
		clear_typematic_key();
	}

	/* Let the task figure things out */
	task_wake(TASK_ID_KEYSCAN);
}

/**
 * Print the keyboard state.
 *
 * @param state		State array to print
 * @param msg		Description of state
 */
static void print_state(const uint8_t *state, const char *msg)
{
	int c;
	char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];

	snprintf_timestamp_now(ts_str, sizeof(ts_str));
	CPRINTF("[%s KB %s:", ts_str, msg);
	for (c = 0; c < keyboard_cols; c++) {
		if (state[c])
			CPRINTF(" %02x", state[c]);
		else
			CPUTS(" --");
	}
	CPUTS("]\n");
}

/**
 * Ensure that the keyboard has been scanned.
 *
 * Makes sure that we've fully gone through the keyboard scanning loop at
 * least once.
 */
static void ensure_keyboard_scanned(int old_polls)
{
	uint64_t start_time;

	start_time = get_time().val;

	/*
	 * Ensure we see the poll task run.
	 *
	 * Note that the poll task is higher priority than ours so we know that
	 * while we're running it's not partway through a poll.  That means that
	 * if kbd_polls changes we've gone through a whole cycle.
	 */
	while ((kbd_polls == old_polls) &&
	       (get_time().val - start_time < SCAN_TASK_TIMEOUT_US))
		usleep(keyscan_config.scan_period_us);
}

#ifdef CONFIG_KEYBOARD_SCAN_ADC
/**
 * Read KSI ADC rows
 *
 * Read each adc channel and look for voltage crossing threshold level
 */
static int keyboard_read_adc_rows(void)
{
	uint8_t kb_row = 0;

	/* Read each adc channel to build row byte */
	for (int i = 0; i < KEYBOARD_ROWS; i++) {
		if (adc_read_channel(ADC_KSI_00 + i) >
		    keyscan_config.ksi_threshold_mv)
			kb_row |= (1 << i);
	}

	return kb_row;
}

/**
 * Read refresh key
 *
 * Refresh key is detached from rest of the matrix in ADC based
 * keyboard, so needs to be read through GPIO
 *
 * @param state		Destination for new state (must be KEYBOARD_COLS_MAX
 *			long).
 */
static void keyboard_read_refresh_key(uint8_t *state)
{
#ifndef CONFIG_KEYBOARD_MULTIPLE
	if (!gpio_get_level(GPIO_RFR_KEY_L))
		state[KEYBOARD_COL_REFRESH] |= BIT(KEYBOARD_ROW_REFRESH);
	else
		state[KEYBOARD_COL_REFRESH] &= ~BIT(KEYBOARD_ROW_REFRESH);
#else
	if (!gpio_get_level(GPIO_RFR_KEY_L))
		state[key_typ.col_refresh] |= BIT(key_typ.row_refresh);
	else
		state[key_typ.col_refresh] &= ~BIT(key_typ.row_refresh);
#endif
}
#endif

/**
 * Simulate a keypress.
 *
 * @param row		Row of key
 * @param col		Column of key
 * @param pressed	Non-zero if pressed, zero if released
 */
static void simulate_key(int row, int col, int pressed)
{
	int old_polls;

	if ((simulated_key[col] & BIT(row)) == ((pressed ? 1 : 0) << row))
		return; /* No change */

	simulated_key[col] ^= BIT(row);

	/* Keep track of polls now that we've got keys simulated */
	old_polls = kbd_polls;

	print_state(simulated_key, "simulated ");

	/* Force a poll even though no keys are pressed */
	force_poll = 1;

	/* Wake the task to handle changes in simulated keys */
	task_wake(TASK_ID_KEYSCAN);

	/*
	 * Make sure that the keyboard task sees the key for long enough.
	 * That means it needs to have run and for enough time.
	 */
	ensure_keyboard_scanned(old_polls);
	usleep(pressed ? keyscan_config.debounce_down_us :
			 keyscan_config.debounce_up_us);
	ensure_keyboard_scanned(kbd_polls);
}

/**
 * Read the raw keyboard matrix state.
 *
 * Used in pre-init, so must not make task-switching-dependent calls; udelay()
 * is ok because it's a spin-loop.
 *
 * @param state		Destination for new state (must be KEYBOARD_COLS_MAX
 *			long).
 * @param at_boot	True if we are reading the boot key state.
 *
 * @return 1 if at least one key is pressed, else zero.
 */
static int read_matrix(uint8_t *state, bool at_boot)
{
	int c;
	int pressed = 0;

	/* 1. Read input pins */
	for (c = 0; c < keyboard_cols; c++) {
		/*
		 * Skip if scanning becomes disabled. Clear the state
		 * to make sure we don't mix new and old states in the
		 * same array.
		 *
		 * Note, scanning is enabled on boot by default.
		 */
		if (!keyboard_scan_is_enabled()) {
			state[c] = 0;
			continue;
		}

		/* Select column, then wait a bit for it to settle */
		keyboard_raw_drive_column(c);
		udelay(keyscan_config.output_settle_us);

		/* Read the row state */
#ifdef CONFIG_KEYBOARD_SCAN_ADC
		state[c] = keyboard_read_adc_rows();
#else
		state[c] = keyboard_raw_read_rows();
#endif

		/* Use simulated keyscan sequence instead if testing active */
		if (IS_ENABLED(CONFIG_KEYBOARD_TEST))
			state[c] = keyscan_seq_get_scan(c, state[c]);
	}

#ifdef KEYBOARD_MASK_PWRBTN
	/*
	 * 2. Boot key workaround.
	 *
	 * Check if KSI2 or KSI3 is asserted for all columns due to power
	 * button hold, and ignore it if so.
	 */
	if (at_boot) {
		for (c = 0; c < keyboard_cols; c++) {
			if (!(state[c] & KEYBOARD_MASK_PWRBTN))
				break;
		}

		if (c == keyboard_cols) {
			for (c = 0; c < keyboard_cols; c++)
				state[c] &= ~KEYBOARD_MASK_PWRBTN;
#ifndef CONFIG_KEYBOARD_MULTIPLE
			state[KEYBOARD_COL_REFRESH] |= KEYBOARD_MASK_PWRBTN;
#else
			state[key_typ.col_refresh] |= KEYBOARD_MASK_PWRBTN;
#endif
		}
	}
#endif

#ifdef CONFIG_KEYBOARD_SCAN_ADC
	/* Account for the refresh key */
	keyboard_read_refresh_key(state);

	/*
	 * KB with ADC support doesn't have transitional ghost,
	 * this check isn't required
	 */
#else
	/* 3. Detect transitional ghost */
	for (c = 0; c < keyboard_cols; c++) {
		int c2;

		for (c2 = 0; c2 < c; c2++) {
			/*
			 * If two columns shares at least one key but their
			 * states are different, maybe the state changed between
			 * two "keyboard_raw_read_rows"s. If this happened,
			 * update both columns to the union of them.
			 *
			 * Note that in theory we need to rescan from col 0 if
			 * anything is updated, to make sure the newly added
			 * bits does not introduce more inconsistency.
			 * Let's ignore this rare case for now.
			 */
			if ((state[c] & state[c2]) && (state[c] != state[c2])) {
				uint8_t merged = state[c] | state[c2];

				state[c] = state[c2] = merged;
			}
		}
	}
#endif

	/* 4. Fix result */
	for (c = 0; c < keyboard_cols; c++) {
		/* Add in simulated keypresses */
		state[c] |= simulated_key[c];

		/*
		 * Keep track of what keys appear to be pressed.  Even if they
		 * don't exist in the matrix, they'll keep triggering
		 * interrupts, so we can't leave scanning mode.
		 */
		pressed |= state[c];

		/* Mask off keys that don't exist on the actual keyboard */
		state[c] &= keyscan_config.actual_key_mask[c];
	}

	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

	return pressed ? 1 : 0;
}

#ifdef CONFIG_KEYBOARD_LANGUAGE_ID
/**
 * Read the raw keyboard IDs state.
 *
 * Used in pre-init, so must not make task-switching-dependent calls; udelay()
 * is ok because it's a spin-loop.
 *
 * @param id		Destination for keyboard id (must be KEYBOARD_IDS long).
 *
 */
static void read_matrix_id(uint8_t *id)
{
	int c;

	for (c = 0; c < KEYBOARD_IDS; c++) {
		/* Select the ID pin, then wait a bit for it to settle.
		 * Caveat: If a keyboard maker puts ID pins right after scan
		 * columns, we can't support variable column size with a single
		 * image. */
		keyboard_raw_drive_column(KEYBOARD_COLS_MAX + c);
		udelay(keyscan_config.output_settle_us);

		/* Read the row state */
		id[c] = keyboard_raw_read_rows();

		CPRINTS("Keyboard ID%u: 0x%02x", c, id[c]);
	}

	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);
}
#endif

#ifdef CONFIG_KEYBOARD_RUNTIME_KEYS

static uint8_t key_vol_up_row = KEYBOARD_DEFAULT_ROW_VOL_UP;
static uint8_t key_vol_up_col = KEYBOARD_DEFAULT_COL_VOL_UP;

void set_vol_up_key(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		key_vol_up_row = row;
		key_vol_up_col = col;
	}
}

/**
 * Check special runtime key combinations.
 *
 * @param state		Keyboard state to use when checking keys.
 *
 * @return 1 if a special key was pressed, 0 if not
 */
static int check_runtime_keys(const uint8_t *state)
{
	int num_press = 0;
	int c;

	/*
	 * All runtime key combos are (right or left ) alt + volume up + (some
	 * key NOT on the same col as alt or volume up )
	 */
	if (state[key_vol_up_col] != KEYBOARD_ROW_TO_MASK(key_vol_up_row))
		return 0;

#ifndef CONFIG_KEYBOARD_MULTIPLE
	if (state[KEYBOARD_COL_RIGHT_ALT] != KEYBOARD_MASK_RIGHT_ALT &&
	    state[KEYBOARD_COL_LEFT_ALT] != KEYBOARD_MASK_LEFT_ALT)
		return 0;
#else
	if (state[key_typ.col_right_alt] != KEYBOARD_MASK_RIGHT_ALT &&
	    state[key_typ.col_left_alt] != KEYBOARD_MASK_LEFT_ALT)
		return 0;
#endif

	/*
	 * Count number of columns with keys pressed.  We know two columns are
	 * pressed for volume up and alt, so if only one more key is pressed
	 * there will be exactly 3 non-zero columns.
	 */
	for (c = 0; c < keyboard_cols; c++) {
		if (state[c])
			num_press++;
	}

	if (num_press != 3)
		return 0;

#ifndef CONFIG_KEYBOARD_MULTIPLE
	/* Check individual keys */
	if (state[KEYBOARD_COL_KEY_R] == KEYBOARD_MASK_KEY_R) {
		/* R = reboot */
		CPRINTS("KB warm reboot");
		keyboard_clear_buffer();
		chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
		return 1;
	} else if (state[KEYBOARD_COL_KEY_H] == KEYBOARD_MASK_KEY_H) {
		/* H = hibernate */
		CPRINTS("KB hibernate");
		system_enter_hibernate(0, 0);
		return 1;
	}
#else
	/* Check individual keys */
	if (state[key_typ.col_key_r] == KEYBOARD_MASK_KEY_R) {
		/* R = reboot */
		CPRINTS("KB warm reboot");
		keyboard_clear_buffer();
		chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
		return 1;
	} else if (state[key_typ.col_key_h] == KEYBOARD_MASK_KEY_H) {
		/* H = hibernate */
		CPRINTS("KB hibernate");
		system_enter_hibernate(0, 0);
		return 1;
	}
#endif

	return 0;
}
#endif /* CONFIG_KEYBOARD_RUNTIME_KEYS */

/**
 * Check for ghosting in the keyboard state.
 *
 * Assumes that the state has already been masked with the actual key mask, so
 * that coords which don't correspond with actual keys don't trigger ghosting
 * detection.
 *
 * @param state		Keyboard state to check.
 *
 * @return 1 if ghosting detected, else 0.
 */
static int has_ghosting(const uint8_t *state)
{
	int c, c2;

	for (c = 0; c < keyboard_cols; c++) {
		if (!state[c])
			continue;

		for (c2 = c + 1; c2 < keyboard_cols; c2++) {
			/*
			 * A little bit of cleverness here.  Ghosting happens
			 * if 2 columns share at least 2 keys.  So we OR the
			 * columns together and then see if more than one bit
			 * is set.  x&(x-1) is non-zero only if x has more than
			 * one bit set.
			 */
			uint8_t common = state[c] & state[c2];

			if (common & (common - 1))
				return 1;
		}
	}

	return 0;
}

/* Inform keyboard module if scanning is enabled */
test_mockable_static void key_state_changed(int row, int col, uint8_t state)
{
	if (!keyboard_scan_is_enabled())
		return;

	/* No-op for protocols that require full keyboard matrix (e.g. MKBP). */
	keyboard_state_changed(row, col, !!(state & BIT(row)));
}

/**
 * Update keyboard state using low-level interface to read keyboard.
 *
 * @param state		Keyboard state to update.
 *
 * @return 1 if any key is still pressed, 0 if no key is pressed.
 */
static int check_keys_changed(uint8_t *state)
{
	int any_pressed = 0;
	int c, i;
	int any_change = 0;
	static uint8_t new_state[KEYBOARD_COLS_MAX];
	uint32_t tnow = get_time().le.lo;

	/* Save the current scan time */
	if (++scan_time_index >= SCAN_TIME_COUNT)
		scan_time_index = 0;
	scan_time[scan_time_index] = tnow;

	/* Read the raw key state */
	any_pressed = read_matrix(new_state, false);

	if (!IS_ENABLED(CONFIG_KEYBOARD_SCAN_ADC)) {
		/* Ignore if so many keys are pressed that we're ghosting. */
		if (has_ghosting(new_state))
			return any_pressed;
	}

	/* Check for changes between previous scan and this one */
	for (c = 0; c < keyboard_cols; c++) {
		int diff = new_state[c] ^ state[c];

		/* Clear debouncing flag, if sufficient time has elapsed. */
		for (i = 0; i < KEYBOARD_ROWS && debouncing[c]; i++) {
			if (!(debouncing[c] & BIT(i)))
				continue;
			if (tnow - scan_time[scan_edge_index[c][i]] <
			    (state[c] ? keyscan_config.debounce_down_us :
					keyscan_config.debounce_up_us))
				continue; /* Not done debouncing */
			debouncing[c] &= ~BIT(i);

			if (!IS_ENABLED(CONFIG_KEYBOARD_STRICT_DEBOUNCE))
				continue;
			if (!(diff & BIT(i)))
				/* Debounced but no difference. */
				continue;
			any_change = 1;
			key_state_changed(i, c, new_state[c]);
			/*
			 * This makes state[c] == new_state[c] for row i.
			 * Thus, when diff is calculated below, it won't
			 * be asserted (for row i).
			 */
			state[c] ^= diff & BIT(i);
		}

		/* Recognize change in state, unless debounce in effect. */
		diff = (new_state[c] ^ state[c]) & ~debouncing[c];
		if (!diff)
			continue;
		for (i = 0; i < KEYBOARD_ROWS; i++) {
			if (!(diff & BIT(i)))
				continue;
			scan_edge_index[c][i] = scan_time_index;

			if (!IS_ENABLED(CONFIG_KEYBOARD_STRICT_DEBOUNCE)) {
				any_change = 1;
				key_state_changed(i, c, new_state[c]);
			}
		}

		/* For any keyboard events just sent, turn on debouncing. */
		debouncing[c] |= diff;
		/*
		 * Note: In order to "remember" what was last reported
		 * (up or down), the state bits are only updated if the
		 * edge was not suppressed due to debouncing.
		 */
		if (!IS_ENABLED(CONFIG_KEYBOARD_STRICT_DEBOUNCE))
			state[c] ^= diff;
	}

	if (any_change) {
#ifdef CONFIG_KEYBOARD_SUPPRESS_NOISE
		/* Suppress keyboard noise */
		keyboard_suppress_noise();
#endif

		if (print_state_changes)
			print_state(state, "state");

#ifdef CONFIG_KEYBOARD_PRINT_SCAN_TIMES
		/* Print delta times from now back to each previous scan */
		char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];

		snprintf_timestamp_now(ts_str, sizeof(ts_str));
		CPRINTF("[%s kb deltaT", ts_str);
		for (i = 0; i < SCAN_TIME_COUNT; i++) {
			int tnew = scan_time[(SCAN_TIME_COUNT +
					      scan_time_index - i) %
					     SCAN_TIME_COUNT];
			CPRINTF(" %d", tnow - tnew);
		}
		CPRINTF("]\n");
#endif

#ifdef CONFIG_KEYBOARD_RUNTIME_KEYS
		/* Swallow special keys */
		if (check_runtime_keys(state))
			return 0;
#endif

#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
		mkbp_keyboard_add(state);
#endif
	}

	kbd_polls++;

	return any_pressed;
}

static uint8_t keyboard_mask_refresh;
__overridable uint8_t board_keyboard_row_refresh(void)
{
	if (IS_ENABLED(CONFIG_KEYBOARD_REFRESH_ROW3))
		return 3;
	else
		return 2;
}

#ifdef CONFIG_KEYBOARD_BOOT_KEYS
/*
 * Returns mask of the boot keys that are pressed, with at most the keys used
 * for keyboard-controlled reset also pressed.
 */
static uint32_t check_key_list(const uint8_t *state)
{
	uint8_t curr_state[KEYBOARD_COLS_MAX];
	int c;
	uint32_t boot_key_mask = BOOT_KEY_NONE;
	const struct boot_key_entry *k;

	/* Make copy of current debounced state. */
	memcpy(curr_state, state, sizeof(curr_state));

#ifndef CONFIG_KEYBOARD_MULTIPLE
	curr_state[KEYBOARD_COL_REFRESH] &= ~keyboard_mask_refresh;
#else
	curr_state[key_typ.col_refresh] &= ~keyboard_mask_refresh;
#endif

	/* Update mask with all boot keys that were pressed. */
	k = boot_key_list;
	for (c = 0; c < ARRAY_SIZE(boot_key_list); c++, k++) {
		if (curr_state[k->col] & BIT(k->row)) {
			boot_key_mask |= BIT(c);
			curr_state[k->col] &= ~BIT(k->row);
		}
	}

	/* If any other key was pressed, ignore all boot keys. */
	for (c = 0; c < keyboard_cols; c++) {
		if (curr_state[c])
			return BOOT_KEY_NONE;
	}

	CPRINTS("KB boot key mask %x", boot_key_mask);
	return boot_key_mask;
}

#ifdef CONFIG_KEYBOARD_SCAN_ADC
static void read_adc_boot_keys(uint8_t *state)
{
	int k;

	for (k = 0; k < ARRAY_SIZE(boot_key_list); k++) {
		int c = boot_key_list[k].col;
		int r = boot_key_list[k].row;

		/* Select column, then wait a bit for it to settle */
		keyboard_raw_drive_column(c);
		udelay(keyscan_config.output_settle_us);

		if (adc_read_channel(ADC_KSI_00 + r) >
		    keyscan_config.ksi_threshold_mv)
			state[c] |= BIT(r);
	}

	/* Read refresh key */
	keyboard_read_refresh_key(state);

	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);
}
#endif

/**
 * Check what boot key is down, if any.
 *
 * @param state		Keyboard state at boot.
 *
 * @return the key which is down, or BOOT_KEY_NONE if an unrecognized
 * key combination is down or this isn't the right type of boot to look at
 * boot keys.
 */
static uint32_t check_boot_key(const uint8_t *state)
{
	/*
	 * If we jumped to this image, ignore boot keys.  This prevents
	 * re-triggering events in RW firmware that were already processed by
	 * RO firmware.
	 */
	if (system_jumped_late())
		return BOOT_KEY_NONE;

/* If reset was not caused by reset pin, refresh must be held down */
#ifndef CONFIG_KEYBOARD_MULTIPLE
	if (!(system_get_reset_flags() & EC_RESET_FLAG_RESET_PIN) &&
	    !(state[KEYBOARD_COL_REFRESH] & keyboard_mask_refresh))
		return BOOT_KEY_NONE;
#else
	if (!(system_get_reset_flags() & EC_RESET_FLAG_RESET_PIN) &&
	    !(state[key_typ.col_refresh] & keyboard_mask_refresh))
		return BOOT_KEY_NONE;
#endif

	return check_key_list(state);
}
#endif

static void keyboard_freq_change(void)
{
	post_scan_clock_us = (CONFIG_KEYBOARD_POST_SCAN_CLOCKS * 1000) /
			     (clock_get_freq() / 1000);
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, keyboard_freq_change, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Interface */

struct keyboard_scan_config *keyboard_scan_get_config(void)
{
	return &keyscan_config;
}

#ifdef CONFIG_KEYBOARD_BOOT_KEYS
uint32_t keyboard_scan_get_boot_keys(void)
{
	return boot_key_value;
}
#endif

const uint8_t *keyboard_scan_get_state(void)
{
	return debounced_state;
}

void keyboard_scan_init(void)
{
	if (IS_ENABLED(CONFIG_KEYBOARD_STRICT_DEBOUNCE) &&
	    keyscan_config.debounce_down_us != keyscan_config.debounce_up_us) {
		/*
		 * Strict debouncer is prone to keypress reordering if debounce
		 * durations for down and up are not equal. crbug.com/547131
		 */
		CPRINTS("KB WARN: Debounce durations not equal");
	}

	/* Configure refresh key matrix */
	keyboard_mask_refresh =
		KEYBOARD_ROW_TO_MASK(board_keyboard_row_refresh());

	if (!IS_ENABLED(CONFIG_KEYBOARD_SCAN_ADC))
		/* Configure GPIO */
		keyboard_raw_init();

	/* Tri-state the columns */
	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

	/* Initialize raw state */
#ifndef CONFIG_KEYBOARD_SCAN_ADC
	read_matrix(debounced_state, true);
#else
	read_adc_boot_keys(debounced_state);
#endif

#ifdef CONFIG_KEYBOARD_LANGUAGE_ID
	/* Check keyboard ID state */
	read_matrix_id(keyboard_id);
#endif

#ifdef CONFIG_KEYBOARD_BOOT_KEYS
	/* Check for keys held down at boot */
	boot_key_value = check_boot_key(debounced_state);

	/*
	 * If any key other than Esc or Left_Shift was pressed, do not trigger
	 * recovery.
	 */
	if (boot_key_value & ~(BOOT_KEY_ESC | BOOT_KEY_LEFT_SHIFT))
		return;

#ifdef CONFIG_HOSTCMD_EVENTS
	if (boot_key_value & BOOT_KEY_ESC) {
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);
		/*
		 * In recovery mode, we should force clamshell mode in order to
		 * prevent the keyboard from being disabled unintentionally due
		 * to unstable accel readings.
		 *
		 * You get the same effect if motion sensors or a motion sense
		 * task are disabled in RO.
		 */
		if (IS_ENABLED(CONFIG_TABLET_MODE))
			tablet_disable();
		if (boot_key_value & BOOT_KEY_LEFT_SHIFT)
			host_set_single_event(
				EC_HOST_EVENT_KEYBOARD_RECOVERY_HW_REINIT);
	}
#endif
#endif /* CONFIG_KEYBOARD_BOOT_KEYS */
}

void keyboard_scan_task(void *u)
{
	timestamp_t poll_deadline, start;
	int wait_time;
	uint32_t local_disable_scanning = 0;

	print_state(debounced_state, "init state");

	keyboard_raw_task_start();

	/* Set initial clock frequency-based minimum delay between scans */
	keyboard_freq_change();

	while (1) {
		/* Enable all outputs */
		CPRINTS5("KB wait");

		keyboard_raw_enable_interrupt(1);

		/* Wait for scanning enabled and key pressed. */
		while (1) {
			uint32_t new_disable_scanning;

			/* Read it once to get consistent glimpse */
			new_disable_scanning = disable_scanning_mask;

			if (local_disable_scanning != new_disable_scanning)
				CPRINTS("KB disable_scanning_mask changed: "
					"0x%08x",
					new_disable_scanning);

			if (!new_disable_scanning) {
				/* Enabled now */
				keyboard_raw_drive_column(KEYBOARD_COLUMN_ALL);
			} else if (!local_disable_scanning) {
				/*
				 * Scanning isn't enabled but it was last time
				 * we looked.
				 *
				 * No race here even though we're basing on a
				 * glimpse of disable_scanning_mask since if
				 * someone changes disable_scanning_mask they
				 * are guaranteed to call task_wake() on us
				 * afterward so we'll run the loop again.
				 */
				keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);
				keyboard_clear_buffer();
			}

			local_disable_scanning = new_disable_scanning;

			/*
			 * Done waiting if scanning is enabled and a key is
			 * already pressed.  This prevents a race between the
			 * user pressing a key and enable_interrupt()
			 * starting to pay attention to edges.
			 */
#ifndef CONFIG_KEYBOARD_SCAN_ADC
			if (!local_disable_scanning &&
			    (keyboard_raw_read_rows() || force_poll))
				break;
#else
			if (!local_disable_scanning &&
			    (keyboard_read_adc_rows() || force_poll ||
			     !gpio_get_level(GPIO_RFR_KEY_L)))
				break;
#endif
			else
				task_wait_event(-1);
		}

		/* We're about to poll, so any existing forces are fulfilled */
		force_poll = 0;

		/* Enter polling mode */
		CPRINTS5("KB poll");
		keyboard_raw_enable_interrupt(0);
		keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

		/* Busy polling keyboard state. */
		while (keyboard_scan_is_enabled()) {
			start = get_time();

			/* Check for keys down */
			if (check_keys_changed(debounced_state)) {
				poll_deadline.val =
					start.val +
					keyscan_config.poll_timeout_us;
			} else if (timestamp_expired(poll_deadline, &start)) {
				break;
			}

			/* Delay between scans */
			wait_time = keyscan_config.scan_period_us -
				    (get_time().val - start.val);

			if (wait_time < keyscan_config.min_post_scan_delay_us)
				wait_time =
					keyscan_config.min_post_scan_delay_us;

			if (wait_time < post_scan_clock_us)
				wait_time = post_scan_clock_us;

			usleep(wait_time);
		}
	}
}

#ifdef CONFIG_LID_SWITCH

static void keyboard_lid_change(void)
{
	if (lid_is_open())
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_CLOSED);
	else
		keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_CLOSED);
}
DECLARE_HOOK(HOOK_LID_CHANGE, keyboard_lid_change, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, keyboard_lid_change, HOOK_PRIO_POST_LID);

#endif

#ifdef CONFIG_USB_SUSPEND
static void keyboard_usb_pm_change(void)
{
	/*
	 * If USB interface is suspended, and host is not asking us to do remote
	 * wakeup, we can turn off the key scanning.
	 */
	if (usb_is_suspended() && !usb_is_remote_wakeup_enabled())
		keyboard_scan_enable(0, KB_SCAN_DISABLE_USB_SUSPENDED);
	else
		keyboard_scan_enable(1, KB_SCAN_DISABLE_USB_SUSPENDED);
}
DECLARE_HOOK(HOOK_USB_PM_CHANGE, keyboard_usb_pm_change, HOOK_PRIO_DEFAULT);
#endif

/*****************************************************************************/
/* Host commands */

static enum ec_status
mkbp_command_simulate_key(struct host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_simulate_key *p = args->params;

	/* Only available on unlocked systems */
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (p->col >= keyboard_cols || p->row >= KEYBOARD_ROWS)
		return EC_RES_INVALID_PARAM;

	simulate_key(p->row, p->col, p->pressed);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_SIMULATE_KEY, mkbp_command_simulate_key,
		     EC_VER_MASK(0));

#ifdef CONFIG_KEYBOARD_FACTORY_TEST

/* Run keyboard factory testing, scan out KSO/KSI if any shorted. */
int keyboard_factory_test_scan(void)
{
	int i, j, flags;
	uint16_t shorted = 0;
	int port, id;

	/* Disable keyboard scan while testing */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_CLOSED);
	flags = gpio_get_default_flags(GPIO_KBD_KSO2);

	/* Set all of KSO/KSI pins to internal pull-up and input */
	for (i = 0; i < keyboard_factory_scan_pins_used; i++) {
		if (keyboard_factory_scan_pins[i][0] < 0)
			continue;

		port = keyboard_factory_scan_pins[i][0];
		id = keyboard_factory_scan_pins[i][1];

		gpio_set_alternate_function(port, 1 << id, GPIO_ALT_FUNC_NONE);
		gpio_set_flags_by_mask(port, 1 << id,
				       GPIO_INPUT | GPIO_PULL_UP);
	}

	/*
	 * Set start pin to output low, then check other pins
	 * going to low level, it indicate the two pins are shorted.
	 */
	for (i = 0; i < keyboard_factory_scan_pins_used; i++) {
		if (keyboard_factory_scan_pins[i][0] < 0)
			continue;

		port = keyboard_factory_scan_pins[i][0];
		id = keyboard_factory_scan_pins[i][1];

		gpio_set_flags_by_mask(port, 1 << id, GPIO_OUT_LOW);

		for (j = 0; j < keyboard_factory_scan_pins_used; j++) {
			if (keyboard_factory_scan_pins[j][0] < 0 || i == j)
				continue;

			if (keyboard_raw_is_input_low(
				    keyboard_factory_scan_pins[j][0],
				    keyboard_factory_scan_pins[j][1])) {
				shorted = i << 8 | j;
				goto done;
			}
		}
		gpio_set_flags_by_mask(port, 1 << id,
				       GPIO_INPUT | GPIO_PULL_UP);
	}
done:
	gpio_config_module(MODULE_KEYBOARD_SCAN, 1);
	gpio_set_flags(GPIO_KBD_KSO2, flags);
	keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_CLOSED);

	return shorted;
}

static enum ec_status keyboard_factory_test(struct host_cmd_handler_args *args)
{
	struct ec_response_keyboard_factory_test *r = args->response;

	/* Only available on unlocked systems */
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (keyboard_factory_scan_pins_used == 0)
		return EC_RES_INVALID_COMMAND;

	r->shorted = keyboard_factory_test_scan();

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_KEYBOARD_FACTORY_TEST, keyboard_factory_test,
		     EC_VER_MASK(0));
#endif

#ifdef CONFIG_KEYBOARD_LANGUAGE_ID
int keyboard_get_keyboard_id(void)
{
	int c;
	uint32_t id = 0;

	BUILD_ASSERT(sizeof(id) >= KEYBOARD_IDS);

	for (c = 0; c < KEYBOARD_IDS; c++) {
		/* Check ID ghosting if more than one bit in any KSIs was set */
		if (keyboard_id[c] & (keyboard_id[c] - 1))
			/* ID ghosting is found */
			return KEYBOARD_ID_UNREADABLE;
		else
			id |= keyboard_id[c] << (c * 8);
	}
	return id;
}
#endif

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_KEYBOARD
static int command_ksstate(int argc, const char **argv)
{
	if (argc > 1) {
		if (!strcasecmp(argv[1], "force")) {
			print_state_changes = 1;
			keyboard_scan_enable(1, -1);
		} else if (!parse_bool(argv[1], &print_state_changes)) {
			return EC_ERROR_PARAM1;
		}
	}

	print_state(debounced_state, "debounced ");
	print_state(debouncing, "debouncing");

	ccprintf("Keyboard scan disable mask: 0x%08x\n", disable_scanning_mask);
	ccprintf("Keyboard scan state printing %s\n",
		 print_state_changes ? "on" : "off");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ksstate, command_ksstate, "ksstate [on | off | force]",
			"Show or toggle printing keyboard scan state");

static int command_keyboard_press(int argc, const char **argv)
{
	if (argc == 1) {
		int i, j;

		ccputs("Simulated keys:\n");
		for (i = 0; i < keyboard_cols; ++i) {
			if (simulated_key[i] == 0)
				continue;
			for (j = 0; j < KEYBOARD_ROWS; ++j)
				if (simulated_key[i] & BIT(j))
					ccprintf("\t%d %d\n", i, j);
		}

	} else if (argc == 3 || argc == 4) {
		int r, c, p;
		char *e;

		c = strtoi(argv[1], &e, 0);
		if (*e || c < 0 || c >= keyboard_cols)
			return EC_ERROR_PARAM1;

		r = strtoi(argv[2], &e, 0);
		if (*e || r < 0 || r >= KEYBOARD_ROWS)
			return EC_ERROR_PARAM2;

		if (argc == 3) {
			/* Simulate a press and release */
			simulate_key(r, c, 1);
			simulate_key(r, c, 0);
		} else {
			p = strtoi(argv[3], &e, 0);
			if (*e || p < 0 || p > 1)
				return EC_ERROR_PARAM3;

			simulate_key(r, c, p);
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbpress, command_keyboard_press, "[col row [0 | 1]]",
			"Simulate keypress");
#endif

#ifdef TEST_BUILD
__test_only int keyboard_scan_get_print_state_changes(void)
{
	return print_state_changes;
}

__test_only void keyboard_scan_set_print_state_changes(int val)
{
	print_state_changes = val;
}
#endif /* TEST_BUILD */
