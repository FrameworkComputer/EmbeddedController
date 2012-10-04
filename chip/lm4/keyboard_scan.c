/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "keyboard.h"
#include "keyboard_scan.h"
#include "keyboard_scan_stub.h"
#include "power_button.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "x86_power.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYSCAN, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYSCAN, format, ## args)

/* Time constants */
#define POLLING_MODE_TIMEOUT 1000000  /* Max time to poll if no keys are down */
#define DEBOUNCE_UP_US         30000  /* Debounce time for key-up */
#define DEBOUNCE_DOWN_US        6000  /* Debounce time for key-down */
#define SCAN_LOOP_DELAY         1000  /* Delay in scan loop */
#define COLUMN_CHARGE_US          40  /* Column charge time in usec */

#define KB_COLS 13

#define SCAN_TIME_COUNT 32

/* Boot key list.  Must be in same order as enum boot_key. */
struct boot_key_entry {
	uint8_t mask_index;
	uint8_t mask_value;
};
const struct boot_key_entry boot_key_list[] = {
	{0, 0x00},  /* (none) */
	{1, 0x02},  /* Esc */
	{11, 0x40}, /* Down-arrow */
};

static uint8_t debounced_state[KB_COLS];     /* Debounced key matrix */
static uint8_t prev_state[KB_COLS];          /* Matrix from previous scan */
static uint8_t debouncing[KB_COLS];          /* Mask of keys being debounced */
static uint32_t scan_time[SCAN_TIME_COUNT];  /* Times of last scans */
static int scan_time_index;                  /* Current scan_time[] index */
/* Index into scan_time[] when each key started debouncing */
static uint8_t scan_edge_index[KB_COLS][8];

enum boot_key boot_key_value = BOOT_KEY_OTHER;

/* Mask with 1 bits only for keys that actually exist */
static const uint8_t *actual_key_mask;

/* All actual key masks (todo: move to keyboard matrix definition) */
static const uint8_t actual_key_masks[4][KB_COLS] = {
	{0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
	 0xa4, 0xff, 0xf6, 0x55, 0xfa, 0xc8},  /* full set */
	{0},
	{0},
	{0},
	};

/* Key masks for special boot keys */
#define MASK_INDEX_REFRESH 2
#define MASK_VALUE_REFRESH 0x04

/* Key masks for special runtime keys */
#define MASK_INDEX_VOL_UP	4
#define MASK_VALUE_VOL_UP	0x01
#define MASK_INDEX_RIGHT_ALT	10
#define MASK_VALUE_RIGHT_ALT	0x01
#define MASK_INDEX_LEFT_ALT	10
#define MASK_VALUE_LEFT_ALT	0x40
#define MASK_INDEX_KEY_R	3
#define MASK_VALUE_KEY_R	0x80
#define MASK_INDEX_KEY_H	6
#define MASK_VALUE_KEY_H	0x02

static void wait_for_interrupt(void)
{
	CPRINTF("[%T KB wait]\n");

	/* Assert all outputs would trigger un-wanted interrupts.
	 * Clear them before enable interrupt. */
	lm4_select_column(COLUMN_ASSERT_ALL);
	lm4_clear_matrix_interrupt_status();

	lm4_enable_matrix_interrupt();
}

static void enter_polling_mode(void)
{
	CPRINTF("[%T KB poll]\n");
	lm4_disable_matrix_interrupt();
	lm4_select_column(COLUMN_TRI_STATE_ALL);
}

/**
 * Read the raw keyboard matrix state.
 *
 * Used in pre-init, so must not make task-switching-dependent calls; udelay()
 * is ok because it's a spin-loop.
 *
 * @param state		Destination for new state (must be KB_COLS long).
 *
 * @return 1 if at least one key is pressed, else zero.
 */
static int read_matrix(uint8_t *state)
{
	int c;
	uint8_t r;
	int pressed = 0;

	for (c = 0; c < KB_COLS; c++) {
		/* Select column, then wait a bit for it to settle */
		lm4_select_column(c);
		udelay(COLUMN_CHARGE_US);

		/* Read the row state */
		r = lm4_read_raw_row_state();
		/* Invert it so 0=not pressed, 1=pressed */
		r ^= 0xff;
		/* Mask off keys that don't exist so they never show
		 * as pressed */
		r &= actual_key_mask[c];

		state[c] = r;
		pressed |= r;
	}

	lm4_select_column(COLUMN_TRI_STATE_ALL);

	return pressed ? 1 : 0;
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

	CPRINTF("[%T KB %s:", msg);
	for (c = 0; c < KB_COLS; c++) {
		if (state[c])
			CPRINTF(" %02x", state[c]);
		else
			CPUTS(" --");
	}
	CPUTS("]\n");
}

/**
 * Check special runtime key combinations.
 *
 * @param state		Keyboard state to use when checking keys.
 */
static void check_runtime_keys(const uint8_t *state)
{
	int num_press = 0;
	int c;

	/*
	 * All runtime key combos are (right or left ) alt + volume up + (some
	 * key NOT on the same col as alt or volume up )
	 */
	if (state[MASK_INDEX_VOL_UP] != MASK_VALUE_VOL_UP)
		return;
	if (state[MASK_INDEX_RIGHT_ALT] != MASK_VALUE_RIGHT_ALT &&
	    state[MASK_INDEX_LEFT_ALT] != MASK_VALUE_LEFT_ALT)
		return;

	/*
	 * Count number of columns with keys pressed.  We know two columns are
	 * pressed for volume up and alt, so if only one more key is pressed
	 * there will be exactly 3 non-zero columns.
	 */
	for (c = 0; c < KB_COLS; c++) {
		if (state[c])
			num_press++;
	}
	if (num_press != 3)
		return;

	/* Check individual keys */
	if (state[MASK_INDEX_KEY_R] == MASK_VALUE_KEY_R) {
		/* R = reboot */
		CPRINTF("[%T KB warm reboot]\n");
		x86_power_reset(0);
	} else if (state[MASK_INDEX_KEY_H] == MASK_VALUE_KEY_H) {
		/* H = hibernate */
		CPRINTF("[%T KB hibernate]\n");
		system_hibernate(0, 0);
	}
}

/**
 * Check for ghosting in the keyboard state.
 *
 * @param state		Keyboard state to check.
 *
 * @return 1 if ghosting detected, else 0.
 */
static int has_ghosting(const uint8_t *state)
{
	int c, c2;

	for (c = 0; c < KB_COLS; c++) {
		if (!state[c])
			continue;

		for (c2 = c + 1; c2 < KB_COLS; c2++) {
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
	uint8_t new_state[KB_COLS];
	uint32_t tnow = get_time().le.lo;

	/* Save the current scan time */
	if (++scan_time_index >= SCAN_TIME_COUNT)
		scan_time_index = 0;
	scan_time[scan_time_index] = tnow;

	/* Read the raw key state */
	any_pressed = read_matrix(new_state);

	/* Ignore if so many keys are pressed that we're ghosting */
	/*
	 * TODO: maybe in this case we should reset all the debounce times,
	 * because in the ghosting case we're not paying attention to any of
	 * the keys which aren't ghosting.
	 */
	if (has_ghosting(new_state))
		return any_pressed;

	/* Check for changes between previous scan and this one */
	for (c = 0; c < KB_COLS; c++) {
		int diff = new_state[c] ^ prev_state[c];
		if (!diff)
			continue;

		for (i = 0; i < 8; i++) {
			if (diff & (1 << i))
				scan_edge_index[c][i] = scan_time_index;
		}

		debouncing[c] |= diff;
		prev_state[c] = new_state[c];
	}

	/* Check for keys which are done debouncing */
	for (c = 0; c < KB_COLS; c++) {
		int debc = debouncing[c];
		if (!debc)
			continue;

		for (i = 0; i < 8; i++) {
			int mask = 1 << i;
			int new_mask = new_state[c] & mask;

			/* Are we done debouncing this key? */
			if (!(debc & mask))
				continue;  /* Not debouncing this key */
			if (tnow - scan_time[scan_edge_index[c][i]] <
			    (new_mask ? DEBOUNCE_DOWN_US : DEBOUNCE_UP_US))
				continue;  /* Not done debouncing */

			debouncing[c] &= ~mask;

			/* Did the key change from its previous state? */
			if ((state[c] & mask) == new_mask)
				continue;  /* No */

			state[c] ^= mask;
			any_change = 1;

			/* Inform keyboard module if scanning is enabled */
			if (lm4_get_scanning_enabled())
				keyboard_state_changed(i, c, new_mask ? 1 : 0);
		}
	}

	if (any_change) {
		print_state(state, "state");

#ifdef PRINT_SCAN_TIMES
		/* Print delta times from now back to each previous scan */
		for (i = 0; i < SCAN_TIME_COUNT; i++) {
			int tnew = scan_time[
				(SCAN_TIME_COUNT + scan_time_index - i) %
				SCAN_TIME_COUNT];
			CPRINTF(" %d", tnow - tnew);
		}
		CPRINTF("\n");
#endif

		check_runtime_keys(state);
	}

	return any_pressed;
}

/*
 * Return non-zero if the specified key is pressed, with at most the keys used
 * for keyboard-controlled reset also pressed.
 */
static int check_key(const uint8_t *state, int index, int mask)
{
	uint8_t allowed_mask[KB_COLS] = {0};
	int c;

	/* Check for the key */
	if (mask && !(state[index] & mask))
		return 0;

	/* Check for other allowed keys */
	allowed_mask[index] |= mask;
	allowed_mask[MASK_INDEX_REFRESH] |= MASK_VALUE_REFRESH;

	for (c = 0; c < KB_COLS; c++) {
		if (state[c] & ~allowed_mask[c])
			return 0;  /* Disallowed key pressed */
	}
	return 1;
}

/**
 * Check what boot key is down, if any.
 *
 * @param state		Keyboard state at boot.
 *
 * @return the key which is down, or BOOT_KEY_OTHER if an unrecognized
 * key combination is down or this isn't the right type of boot to look at
 * boot keys.
 */
static enum boot_key keyboard_scan_check_boot_key(const uint8_t *state)
{
	const struct boot_key_entry *k = boot_key_list;
	int i;

	/*
	 * If we jumped to this image, ignore boot keys.  This prevents
	 * re-triggering events in RW firmware that were already processed by
	 * RO firmware.
	 */
	if (system_jumped_to_this_image())
		return BOOT_KEY_OTHER;

	/* If reset was not caused by reset pin, refresh must be held down */
	if (!(system_get_reset_flags() & RESET_FLAG_RESET_PIN) &&
	    !(state[MASK_INDEX_REFRESH] & MASK_VALUE_REFRESH))
		return BOOT_KEY_OTHER;

	/* Check what single key is down */
	for (i = 0; i < ARRAY_SIZE(boot_key_list); i++, k++) {
		if (check_key(state, k->mask_index, k->mask_value)) {
			CPRINTF("[%T KB boot key %d]\n", i);
			return i;
		}
	}

	return BOOT_KEY_OTHER;
}

enum boot_key keyboard_scan_get_boot_key(void)
{
	return boot_key_value;
}

int keyboard_scan_init(void)
{
	/* Configure GPIO */
	lm4_configure_keyboard_gpio();

	/* Tri-state the columns */
	lm4_select_column(COLUMN_TRI_STATE_ALL);

	/*
	 * TODO: method to set which keyboard we have, so we set the actual
	 * key mask properly.
	 */
	actual_key_mask = actual_key_masks[0];

	/* Initialize raw state */
	read_matrix(debounced_state);
	memcpy(prev_state, debounced_state, sizeof(prev_state));

	/* Check for keys held down at boot */
	boot_key_value = keyboard_scan_check_boot_key(debounced_state);

	/* Trigger event if recovery key was pressed */
	if (boot_key_value == BOOT_KEY_ESC)
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);

	return EC_SUCCESS;
}

void keyboard_scan_task(void)
{
	int key_press_timer = 0;

	print_state(debounced_state, "init state");

	/* Enable interrupts */
	task_enable_irq(KB_SCAN_ROW_IRQ);

	while (1) {
		/* Enable all outputs */
		wait_for_interrupt();

		/* Wait for scanning enabled and key pressed. */
		do {
			task_wait_event(-1);
		} while (!lm4_get_scanning_enabled());

		enter_polling_mode();
		/* Busy polling keyboard state. */
		while (lm4_get_scanning_enabled()) {
			/* Check for keys down */
			if (check_keys_changed(debounced_state)) {
				key_press_timer = 0;
			} else if (++key_press_timer >=
				   (POLLING_MODE_TIMEOUT / SCAN_LOOP_DELAY)) {
				/* Stop polling */
				key_press_timer = 0;
				break;
			}

			/* Delay between scans */
			usleep(SCAN_LOOP_DELAY);
		}
	}
}

static void matrix_interrupt(void)
{
	uint32_t ris = lm4_clear_matrix_interrupt_status();

	if (ris)
		task_wake(TASK_ID_KEYSCAN);
}
DECLARE_IRQ(KB_SCAN_ROW_IRQ, matrix_interrupt, 3);

/*
 * The actual implementation is controlling the enable_scanning variable, then
 * that controls whether lm4_select_column() can pull-down columns or not.
 */
void keyboard_enable_scanning(int enable)
{
	lm4_set_scanning_enabled(enable);
	if (enable) {
		/*
		 * A power button press had tri-stated all columns (see the
		 * 'else' statement below), we need a wake-up to unlock the
		 * task_wait_event() loop after wait_for_interrupt().
		 */
		task_wake(TASK_ID_KEYSCAN);
	} else {
		lm4_select_column(COLUMN_TRI_STATE_ALL);
		keyboard_clear_underlying_buffer();
	}
}
