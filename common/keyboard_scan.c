/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYSCAN, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYSCAN, format, ## args)

#define SCAN_TIME_COUNT 32  /* Number of last scan times to track */

/* If we're waiting for a scan to happen, we'll give it this long */
#define SCAN_TASK_TIMEOUT_US	(100 * MSEC)

#ifndef CONFIG_KEYBOARD_POST_SCAN_CLOCKS
/*
 * Default delay in clocks; this was experimentally determined to be long
 * enough to avoid watchdog warnings or I2C errors on a typical notebook
 * config on STM32.
 */
#define CONFIG_KEYBOARD_POST_SCAN_CLOCKS 16000
#endif

#ifndef CONFIG_KEYBOARD_BOARD_CONFIG
/* Use default keyboard scan config, because board didn't supply one */
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 50,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};
#endif

/* Boot key list.  Must be in same order as enum boot_key. */
struct boot_key_entry {
	uint8_t mask_index;
	uint8_t mask_value;
};
static const struct boot_key_entry boot_key_list[] = {
	{0, 0x00},  /* (none) */
	{KEYBOARD_COL_ESC, KEYBOARD_MASK_ESC},   /* Esc */
	{KEYBOARD_COL_DOWN, KEYBOARD_MASK_DOWN}, /* Down-arrow */
};
static enum boot_key boot_key_value = BOOT_KEY_OTHER;

static uint8_t debounced_state[KEYBOARD_COLS]; /* Debounced key matrix */
static uint8_t prev_state[KEYBOARD_COLS];    /* Matrix from previous scan */
static uint8_t debouncing[KEYBOARD_COLS];    /* Mask of keys being debounced */
static uint8_t simulated_key[KEYBOARD_COLS]; /* Keys simulated-pressed */

static uint32_t scan_time[SCAN_TIME_COUNT];  /* Times of last scans */
static int scan_time_index;                  /* Current scan_time[] index */

/* Index into scan_time[] when each key started debouncing */
static uint8_t scan_edge_index[KEYBOARD_COLS][KEYBOARD_ROWS];

/* Minimum delay between keyboard scans based on current clock frequency */
static uint32_t post_scan_clock_us;

/*
 * Print all keyboard scan state changes?  Off by default because it generates
 * a lot of debug output, which makes the saved EC console data less useful.
 */
static int print_state_changes;

static int enable_scanning = 1;  /* Must init to 1 for scanning at boot */

/* Constantly incrementing counter of the number of times we polled */
static volatile int kbd_polls;

static int is_scanning_enabled(void)
{
#ifdef CONFIG_LID_SWITCH
	/* Scanning is never enabled when lid is closed */
	if (!lid_is_open())
		return 0;
#endif

	return enable_scanning;
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
	for (c = 0; c < KEYBOARD_COLS; c++) {
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

	if ((simulated_key[col] & (1 << row)) == ((pressed ? 1 : 0) << row))
		return;  /* No change */

	simulated_key[col] ^= (1 << row);

	/* Keep track of polls now that we've got keys simulated */
	old_polls = kbd_polls;

	print_state(simulated_key, "simulated ");

	/* Wake the task to handle changes in simulated keys */
	task_wake(TASK_ID_KEYSCAN);

	/*
	 * Make sure that the keyboard task sees the key for long enough.
	 * That means it needs to have run and for enough time.
	 */
	ensure_keyboard_scanned(old_polls);
	usleep(pressed ?
	       keyscan_config.debounce_down_us : keyscan_config.debounce_up_us);
	ensure_keyboard_scanned(kbd_polls);
}

/**
 * Read the raw keyboard matrix state.
 *
 * Used in pre-init, so must not make task-switching-dependent calls; udelay()
 * is ok because it's a spin-loop.
 *
 * @param state		Destination for new state (must be KEYBOARD_COLS long).
 *
 * @return 1 if at least one key is pressed, else zero.
 */
static int read_matrix(uint8_t *state)
{
	int c;
	uint8_t r;
	int pressed = 0;

	for (c = 0; c < KEYBOARD_COLS; c++) {
		/*
		 * Stop if scanning becomes disabled.  Check enable_cscanning
		 * instead of is_scanning_enabled() so that we can scan the
		 * matrix at boot time before the lid switch is readable.
		 */
		if (!enable_scanning)
			break;

		/* Select column, then wait a bit for it to settle */
		keyboard_raw_drive_column(c);
		udelay(keyscan_config.output_settle_us);

		/* Read the row state */
		r = keyboard_raw_read_rows();

		/* Add in simulated keypresses */
		r |= simulated_key[c];

		/*
		 * Keep track of what keys appear to be pressed.  Even if they
		 * don't exist in the matrix, they'll keep triggering
		 * interrupts, so we can't leave scanning mode.
		 */
		pressed |= r;

		/* Mask off keys that don't exist on the actual keyboard */
		r &= keyscan_config.actual_key_mask[c];

#ifdef CONFIG_KEYBOARD_TEST
		/* Use simulated keyscan sequence instead if testing active */
		r = keyscan_seq_get_scan(c, r);
#endif

		/* Store the masked state */
		state[c] = r;
	}

	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

	return pressed ? 1 : 0;
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
	if (state[KEYBOARD_COL_VOL_UP] != KEYBOARD_MASK_VOL_UP)
		return 0;

	if (state[KEYBOARD_COL_RIGHT_ALT] != KEYBOARD_MASK_RIGHT_ALT &&
	    state[KEYBOARD_COL_LEFT_ALT] != KEYBOARD_MASK_LEFT_ALT)
		return 0;

	/*
	 * Count number of columns with keys pressed.  We know two columns are
	 * pressed for volume up and alt, so if only one more key is pressed
	 * there will be exactly 3 non-zero columns.
	 */
	for (c = 0; c < KEYBOARD_COLS; c++) {
		if (state[c])
			num_press++;
	}

	if (num_press != 3)
		return 0;

	/* Check individual keys */
	if (state[KEYBOARD_COL_KEY_R] == KEYBOARD_MASK_KEY_R) {
		/* R = reboot */
		CPRINTF("[%T KB warm reboot]\n");
		keyboard_clear_buffer();
		chipset_reset(0);
		return 1;
	} else if (state[KEYBOARD_COL_KEY_H] == KEYBOARD_MASK_KEY_H) {
		/* H = hibernate */
		CPRINTF("[%T KB hibernate]\n");
		system_hibernate(0, 0);
		return 1;
	}

	return 0;
}

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

	for (c = 0; c < KEYBOARD_COLS; c++) {
		if (!state[c])
			continue;

		for (c2 = c + 1; c2 < KEYBOARD_COLS; c2++) {
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
	static uint8_t new_state[KEYBOARD_COLS];
	uint32_t tnow = get_time().le.lo;

	/* Save the current scan time */
	if (++scan_time_index >= SCAN_TIME_COUNT)
		scan_time_index = 0;
	scan_time[scan_time_index] = tnow;

	/* Read the raw key state */
	any_pressed = read_matrix(new_state);

	/* Ignore if so many keys are pressed that we're ghosting. */
	if (has_ghosting(new_state))
		return any_pressed;

	/* Check for changes between previous scan and this one */
	for (c = 0; c < KEYBOARD_COLS; c++) {
		int diff = new_state[c] ^ prev_state[c];

		if (!diff)
			continue;

		for (i = 0; i < KEYBOARD_ROWS; i++) {
			if (diff & (1 << i))
				scan_edge_index[c][i] = scan_time_index;
		}

		debouncing[c] |= diff;
		prev_state[c] = new_state[c];
	}

	/* Check for keys which are done debouncing */
	for (c = 0; c < KEYBOARD_COLS; c++) {
		int debc = debouncing[c];

		if (!debc)
			continue;

		for (i = 0; i < KEYBOARD_ROWS; i++) {
			int mask = 1 << i;
			int new_mask = new_state[c] & mask;

			/* Are we done debouncing this key? */
			if (!(debc & mask))
				continue;  /* Not debouncing this key */
			if (tnow - scan_time[scan_edge_index[c][i]] <
			    (new_mask ? keyscan_config.debounce_down_us :
					keyscan_config.debounce_up_us))
				continue;  /* Not done debouncing */

			debouncing[c] &= ~mask;

			/* Did the key change from its previous state? */
			if ((state[c] & mask) == new_mask)
				continue;  /* No */

			state[c] ^= mask;
			any_change = 1;

#ifdef CONFIG_KEYBOARD_PROTOCOL_8042
			/* Inform keyboard module if scanning is enabled */
			if (is_scanning_enabled())
				keyboard_state_changed(i, c, new_mask ? 1 : 0);
#endif
		}
	}

	if (any_change) {

#ifdef CONFIG_KEYBOARD_SUPPRESS_NOISE
		/* Suppress keyboard noise */
		keyboard_suppress_noise();
#endif

		if (print_state_changes)
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

		/* Swallow special keys */
		if (check_runtime_keys(state))
			return 0;

#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
		keyboard_fifo_add(state);
#endif
	}

	kbd_polls++;

	return any_pressed;
}

/*
 * Return non-zero if the specified key is pressed, with at most the keys used
 * for keyboard-controlled reset also pressed.
 */
static int check_key(const uint8_t *state, int index, int mask)
{
	uint8_t allowed_mask[KEYBOARD_COLS] = {0};
	int c;

	/* Check for the key */
	if (mask && !(state[index] & mask))
		return 0;

	/* Check for other allowed keys */
	allowed_mask[index] |= mask;
	allowed_mask[KEYBOARD_COL_REFRESH] |= KEYBOARD_MASK_REFRESH;

	for (c = 0; c < KEYBOARD_COLS; c++) {
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
static enum boot_key check_boot_key(const uint8_t *state)
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
	    !(state[KEYBOARD_COL_REFRESH] & KEYBOARD_MASK_REFRESH))
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

enum boot_key keyboard_scan_get_boot_key(void)
{
	return boot_key_value;
}

const uint8_t *keyboard_scan_get_state(void)
{
	return debounced_state;
}

void keyboard_scan_init(void)
{
	/* Configure GPIO */
	keyboard_raw_init();

	/* Tri-state the columns */
	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

	/* Initialize raw state */
	read_matrix(debounced_state);
	memcpy(prev_state, debounced_state, sizeof(prev_state));

	/* Check for keys held down at boot */
	boot_key_value = check_boot_key(debounced_state);

	/* Trigger event if recovery key was pressed */
	if (boot_key_value == BOOT_KEY_ESC)
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);
}

void keyboard_scan_task(void)
{
	timestamp_t poll_deadline, start;
	int wait_time;

	print_state(debounced_state, "init state");

	keyboard_raw_task_start();

	/* Set initial clock frequency-based minimum delay between scans */
	keyboard_freq_change();

	while (1) {
		/* Enable all outputs */
		CPRINTF("[%T KB wait]\n");
		if (is_scanning_enabled())
			keyboard_raw_drive_column(KEYBOARD_COLUMN_ALL);
		keyboard_raw_enable_interrupt(1);

		/* Wait for scanning enabled and key pressed. */
		do {
			/*
			 * Don't wait if scanning is enabled and a key is
			 * already pressed.  This prevents a race between the
			 * user pressing a key and enable_interrupt()
			 * starting to pay attention to edges.
			 */
			if (!keyboard_raw_read_rows() || !is_scanning_enabled())
				task_wait_event(-1);
		} while (!is_scanning_enabled());

		/* Enter polling mode */
		CPRINTF("[%T KB poll]\n");
		keyboard_raw_enable_interrupt(0);
		keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

		/* Busy polling keyboard state. */
		while (is_scanning_enabled()) {
			start = get_time();

			/* Check for keys down */
			if (check_keys_changed(debounced_state)) {
				poll_deadline.val = start.val
					+ keyscan_config.poll_timeout_us;
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

void keyboard_scan_enable(int enable)
{
	enable_scanning = enable;

	if (enable) {
		/*
		 * A power button press had tri-stated all columns (see the
		 * 'else' statement below); we need a wake-up to unlock the
		 * task_wait_event() loop after enable_interrupt().
		 */
		task_wake(TASK_ID_KEYSCAN);
	} else {
		keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);
		keyboard_clear_buffer();
	}
}

#ifdef CONFIG_LID_SWITCH

static void keyboard_lid_change(void)
{
	/* If lid is open, wake the keyboard task */
	if (lid_is_open())
		task_wake(TASK_ID_KEYSCAN);
}
DECLARE_HOOK(HOOK_LID_CHANGE, keyboard_lid_change, HOOK_PRIO_DEFAULT);

#endif

/*****************************************************************************/
/* Host commands */

static int mkbp_command_simulate_key(struct host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_simulate_key *p = args->params;

	/* Only available on unlocked systems */
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (p->col >= KEYBOARD_COLS || p->row >= KEYBOARD_ROWS)
		return EC_RES_INVALID_PARAM;

	simulate_key(p->row, p->col, p->pressed);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_SIMULATE_KEY,
		     mkbp_command_simulate_key,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

static int command_ksstate(int argc, char **argv)
{
	if (argc > 1 && !parse_bool(argv[1], &print_state_changes))
		return EC_ERROR_PARAM1;

	print_state(debounced_state, "debounced ");
	print_state(prev_state, "prev      ");
	print_state(debouncing, "debouncing");

	ccprintf("Keyboard scan state printing %s\n",
		 print_state_changes ? "on" : "off");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ksstate, command_ksstate,
			"ksstate [on | off]",
			"Show or toggle printing keyboard scan state",
			NULL);

static int command_keyboard_press(int argc, char **argv)
{
	if (argc == 1) {
		int i, j;

		ccputs("Simulated keys:\n");
		for (i = 0; i < KEYBOARD_COLS; ++i) {
			if (simulated_key[i] == 0)
				continue;
			for (j = 0; j < KEYBOARD_ROWS; ++j)
				if (simulated_key[i] & (1 << j))
					ccprintf("\t%d %d\n", i, j);
		}

	} else if (argc == 3 || argc == 4) {
		int r, c, p;
		char *e;

		c = strtoi(argv[1], &e, 0);
		if (*e || c < 0 || c >= KEYBOARD_COLS)
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
DECLARE_CONSOLE_COMMAND(kbpress, command_keyboard_press,
			"[col row [0 | 1]]",
			"Simulate keypress",
			NULL);
