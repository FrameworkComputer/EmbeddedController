/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Keyboard scanner module for Chrome EC STM32
 */

#include "atomic.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "keyboard.h"
#include "keyboard_config.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "keyboard_test.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYSCAN, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYSCAN, format, ## args)

#define SCAN_TIME_COUNT 32

static struct mutex scanning_enabled;

static uint8_t debounced_state[KEYBOARD_COLS];   /* Debounced key matrix */
static uint8_t prev_state[KEYBOARD_COLS];    /* Matrix from previous scan */
static uint8_t debouncing[KEYBOARD_COLS];    /* Mask of keys being debounced */
static uint32_t scan_time[SCAN_TIME_COUNT];  /* Times of last scans */
static int scan_time_index;                  /* Current scan_time[] index */
/* Index into scan_time[] when each key started debouncing */
static uint8_t scan_edge_index[KEYBOARD_COLS][KEYBOARD_ROWS];

/*****************************************************************************/

/* Provide a default function in case the board doesn't have one */
void __board_keyboard_suppress_noise(void)
{
}

void board_keyboard_suppress_noise(void)
		__attribute__((weak, alias("__board_keyboard_suppress_noise")));

#define KB_FIFO_DEPTH		16	/* FIXME: this is pretty huge */
static uint32_t kb_fifo_start;		/* first entry */
static uint32_t kb_fifo_end;			/* last entry */
static uint32_t kb_fifo_entries;	/* number of existing entries */
static uint8_t kb_fifo[KB_FIFO_DEPTH][KEYBOARD_COLS];

/*
 * Our configuration. The debounce parameters are not yet supported.
 */
static struct ec_mkbp_config config = {
	.valid_mask = EC_MKBP_VALID_SCAN_PERIOD | EC_MKBP_VALID_POLL_TIMEOUT |
		EC_MKBP_VALID_MIN_POST_SCAN_DELAY |
		EC_MKBP_VALID_OUTPUT_SETTLE | EC_MKBP_VALID_DEBOUNCE_DOWN |
		EC_MKBP_VALID_DEBOUNCE_UP | EC_MKBP_VALID_FIFO_MAX_DEPTH,
	.valid_flags = EC_MKBP_FLAGS_ENABLE,
	.flags = EC_MKBP_FLAGS_ENABLE,
	.scan_period_us = 3000,
	.poll_timeout_us = 100 * 1000,
	.min_post_scan_delay_us = 1000,
	.output_settle_us = 50,
	.debounce_down_us = 9000,
	.debounce_up_us = 30000,
	.fifo_max_depth = KB_FIFO_DEPTH,
};

/**
 * Clear keyboard state variables.
 */
void keyboard_clear_state(void)
{
	int i;

	CPRINTF("clearing keyboard fifo\n");
	kb_fifo_start = 0;
	kb_fifo_end = 0;
	kb_fifo_entries = 0;
	for (i = 0; i < KB_FIFO_DEPTH; i++)
		memset(kb_fifo[i], 0, KEYBOARD_COLS);
}

/**
  * Add keyboard state into FIFO
  *
  * @return EC_SUCCESS if entry added, EC_ERROR_OVERFLOW if FIFO is full
  */
static int kb_fifo_add(uint8_t *buffp)
{
	int ret = EC_SUCCESS;

	if (kb_fifo_entries >= config.fifo_max_depth) {
		CPRINTF("%s: FIFO depth %d reached\n", __func__,
			config.fifo_max_depth);
		ret = EC_ERROR_OVERFLOW;
		goto kb_fifo_push_done;
	}

	memcpy(kb_fifo[kb_fifo_end], buffp, KEYBOARD_COLS);

	kb_fifo_end = (kb_fifo_end + 1) % KB_FIFO_DEPTH;

	atomic_add(&kb_fifo_entries, 1);

kb_fifo_push_done:
	return ret;
}

/**
  * Pop keyboard state from FIFO
  *
  * @return EC_SUCCESS if entry popped, EC_ERROR_UNKNOWN if FIFO is empty
  */
static int kb_fifo_remove(uint8_t *buffp)
{
	if (!kb_fifo_entries) {
		/* no entry remaining in FIFO : return last known state */
		int last = (kb_fifo_start + KB_FIFO_DEPTH - 1) % KB_FIFO_DEPTH;
		memcpy(buffp, kb_fifo[last], KEYBOARD_COLS);

		/*
		 * Bail out without changing any FIFO indices and let the
		 * caller know something strange happened. The buffer will
		 * will contain the last known state of the keyboard.
		 */
		return EC_ERROR_UNKNOWN;
	}
	memcpy(buffp, kb_fifo[kb_fifo_start], KEYBOARD_COLS);

	kb_fifo_start = (kb_fifo_start + 1) % KB_FIFO_DEPTH;

	atomic_sub(&kb_fifo_entries, 1);

	return EC_SUCCESS;
}

/**
 * Assert host keyboard interrupt line.
 */
static void set_host_interrupt(int active)
{
	/* interrupt host by using active low EC_INT signal */
	gpio_set_level(GPIO_EC_INT, !active);
}

/**
 * Check special runtime key combinations.
 *
 * @param state		Keyboard state to use when checking keys.
 * @return 1 if a special key was pressed, 0 if not
 */
static int check_runtime_keys(const uint8_t *state)
{
	int num_press;
	int c;

	/* Count number of key pressed */
	for (c = num_press = 0; c < KEYBOARD_COLS; c++) {
		if (state[c])
			++num_press;
	}

	if (num_press != 3)
		return 0;

	if (state[KEYBOARD_COL_KEY_R] == KEYBOARD_MASK_KEY_R &&
	    state[KEYBOARD_COL_VOL_UP] == KEYBOARD_MASK_VOL_UP &&
	    (state[KEYBOARD_COL_RIGHT_ALT] == KEYBOARD_MASK_RIGHT_ALT ||
	     state[KEYBOARD_COL_LEFT_ALT] == KEYBOARD_MASK_LEFT_ALT)) {
		keyboard_clear_state();
		chipset_reset(0);
		return 1;
	}

	return 0;
}

/* Print the keyboard state. */
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
		/* Assert output, then wait a bit for it to settle */
		keyboard_raw_drive_column(c);
		udelay(config.output_settle_us);

		r = keyboard_raw_read_rows();

#ifdef CONFIG_KEYBOARD_TEST
		/* Use simulated keyscan sequence instead if testing active */
		r = keyscan_seq_get_scan(c, r);
#endif

#ifdef OR_WITH_CURRENT_STATE_FOR_TESTING
		/* KLUDGE - or current state in, so we can make sure
		 * all the lines are hooked up */
		r |= state[c];
#endif

		state[c] = r;
		pressed |= r;
	}
	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

	return pressed ? 1 : 0;
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
	uint8_t new_state[KEYBOARD_COLS];
	uint32_t tnow = get_time().le.lo;

	/* Save the current scan time */
	if (++scan_time_index >= SCAN_TIME_COUNT)
		scan_time_index = 0;
	scan_time[scan_time_index] = tnow;

	/* Read the raw key state */
	any_pressed = read_matrix(new_state);

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
			    (new_mask ? config.debounce_down_us :
					config.debounce_up_us))
				continue;  /* Not done debouncing */

			debouncing[c] &= ~mask;

			/* Did the key change from its previous state? */
			if ((state[c] & mask) == new_mask)
				continue;  /* No */

			state[c] ^= mask;
			any_change = 1;
		}
	}

	if (any_change) {
		board_keyboard_suppress_noise();
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
		else if (kb_fifo_add(state) == EC_SUCCESS)
			set_host_interrupt(1);
		else
			CPRINTF("dropped keystroke\n");
	}

	return any_pressed;
}

/*
 * Check if the user has triggered a recovery reset
 *
 * Pressing Power + Refresh + ESC. triggers a recovery reset. Here we check
 * for this.
 *
 * @param state		Keyboard state to check
 * @return 1 if there is a recovery reset, else 0
 */
static int check_recovery_key(const uint8_t *state)
{
	int c;

	/* check the recovery key only if we're booting due to a
	 * reset-pin-caused reset. */
	if (!(system_get_reset_flags() & RESET_FLAG_RESET_PIN))
		return 0;

	/* cold boot : Power + Refresh were pressed,
	 * check if ESC is also pressed for recovery. */
	if (!(state[KEYBOARD_COL_ESC] & KEYBOARD_MASK_ESC))
		return 0;

	/* Make sure only other allowed keys are pressed.  This protects
	 * against accidentally triggering the special key when a cat sits on
	 * your keyboard.  Currently, only the requested key and ESC are
	 * allowed. */
	for (c = 0; c < KEYBOARD_COLS; c++) {
		if (state[c] &&
		(c != KEYBOARD_COL_ESC || state[c] != KEYBOARD_MASK_ESC) &&
		(c != KEYBOARD_COL_REFRESH ||
		 state[c] != KEYBOARD_MASK_REFRESH))
			return 0;  /* Additional disallowed key pressed */
	}

	CPRINTF("Keyboard RECOVERY detected !\n");

	host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);

	return 1;
}

void keyboard_scan_init(void)
{
	keyboard_raw_init();

	/* Tri-state (put into Hi-Z) the outputs */
	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

	/* Initialize raw state */
	read_matrix(debounced_state);
	memcpy(prev_state, debounced_state, sizeof(prev_state));

	/* is recovery key pressed on cold startup ? */
	check_recovery_key(debounced_state);
}

/* Scan the keyboard until all keys are released */
static void scan_keyboard(void)
{
	timestamp_t poll_deadline, start;
	int keys_changed = 1;

	mutex_lock(&scanning_enabled);
	keyboard_raw_drive_column(KEYBOARD_COLUMN_ALL);
	keyboard_raw_enable_interrupt(1);
	mutex_unlock(&scanning_enabled);

	/*
	 * if a key was pressed after the last polling,
	 * re-start immediatly polling instead of waiting
	 * for the next interrupt.
	 */
	if (!keyboard_raw_read_rows()) {
#ifdef CONFIG_KEYBOARD_TEST
		task_wait_event(keyscan_seq_next_event_delay());
#else
		task_wait_event(-1);
#endif
	}

	keyboard_raw_enable_interrupt(0);
	keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);

	/* Busy polling keyboard state. */
	while (1) {
		int wait_time;

		if (!(config.flags & EC_MKBP_FLAGS_ENABLE))
			break;

		/* If we saw any keys pressed, reset deadline */
		start = get_time();
		if (keys_changed)
			poll_deadline.val = start.val + config.poll_timeout_us;
		else if (timestamp_expired(poll_deadline, &start))
			break;

		/* Scan immediately, with no delay */
		mutex_lock(&scanning_enabled);
		keys_changed = check_keys_changed(debounced_state);
		mutex_unlock(&scanning_enabled);

		/* Wait a bit before scanning again */
		wait_time = config.scan_period_us -
				(get_time().val - start.val);
		if (wait_time < config.min_post_scan_delay_us)
			wait_time = config.min_post_scan_delay_us;
		task_wait_event(wait_time);
	}
}

void keyboard_scan_task(void)
{
	print_state(debounced_state, "init state");

	keyboard_raw_task_start();

	while (1) {
		if (config.flags & EC_MKBP_FLAGS_ENABLE) {
			scan_keyboard();
		} else {
			keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);
			task_wait_event(-1);
		}
	}
}

int keyboard_has_char(void)
{
	/* TODO: needs to be implemented */
	return 0;
}

void keyboard_put_char(uint8_t chr, int send_irq)
{
	/* TODO: needs to be implemented */
}

int keyboard_scan_recovery_pressed(void)
{
	return host_get_events() &
	EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_RECOVERY);
}

static int keyboard_get_scan(struct host_cmd_handler_args *args)
{
	kb_fifo_remove(args->response);
	if (!kb_fifo_entries)
		set_host_interrupt(0);

	args->response_size = KEYBOARD_COLS;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_STATE,
		     keyboard_get_scan,
		     EC_VER_MASK(0));

static int keyboard_get_info(struct host_cmd_handler_args *args)
{
	struct ec_response_mkbp_info *r = args->response;

	r->rows = KEYBOARD_ROWS;
	r->cols = KEYBOARD_COLS;
	r->switches = 0;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_INFO,
		     keyboard_get_info,
		     EC_VER_MASK(0));

void keyboard_enable_scanning(int enable)
{
	if (enable) {
		mutex_unlock(&scanning_enabled);
		task_wake(TASK_ID_KEYSCAN);
	} else {
		/*
		 * TODO: using a mutex to control scanning isn't very
		 * responsive.  If we just started scanning the matrix, the
		 * mutex will already be locked, and we'll finish the entire
		 * matrix scan before we stop driving columns.  We should
		 * instead do something like link, where disabling scanning
		 * immediately stops driving the columns.
		 */
		mutex_lock(&scanning_enabled);
		keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);
	}
}

/* Changes to col,row here need to also be reflected in kernel.
 * drivers/input/mkbp.c ... see KEY_BATTERY.
 */
#define BATTERY_KEY_COL 0
#define BATTERY_KEY_ROW 7
#define BATTERY_KEY_ROW_MASK (1 << BATTERY_KEY_ROW)

void keyboard_send_battery_key()
{
	mutex_lock(&scanning_enabled);
	debounced_state[BATTERY_KEY_COL] ^= BATTERY_KEY_ROW_MASK;
	if (kb_fifo_add(debounced_state) == EC_SUCCESS)
		set_host_interrupt(1);
	else
		CPRINTF("dropped battery keystroke\n");
	mutex_unlock(&scanning_enabled);
}

static int command_keyboard_press(int argc, char **argv)
{
	int r, c, p;
	char *e;

	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	c = strtoi(argv[1], &e, 0);
	if (*e || c < 0 || c >= KEYBOARD_COLS)
		return EC_ERROR_PARAM1;

	r = strtoi(argv[2], &e, 0);
	if (*e || r < 0 || r >= KEYBOARD_ROWS)
		return EC_ERROR_PARAM2;

	p = strtoi(argv[3], &e, 0);
	if (*e || p < 0 || p > 1)
		return EC_ERROR_PARAM3;

	/*
	 * TODO(sjg@chromium.org): This ignores debouncing, so is a bit
	 * dodgy and might have strange side-effects on real key scans.
	 */
	if (p)
		debounced_state[c] |= (1 << r);
	else
		debounced_state[c] &= ~(1 << r);

	if (kb_fifo_add(debounced_state) == EC_SUCCESS)
		set_host_interrupt(1);
	else
		ccprintf("dropped keystroke\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbpress, command_keyboard_press,
			"[col] [row] [0 | 1]",
			"Simulate keypress",
			NULL);

/**
 * Copy keyscan configuration from one place to another according to flags
 *
 * This is like a structure copy, except that only selected fields are
 * copied.
 *
 * TODO(sjg@chromium.org): Consider making this table drive as ectool.
 *
 * @param src		Source config
 * @param dst		Destination config
 * @param valid_mask	Bits representing which fields to copy - each bit is
 *			from enum mkbp_config_valid
 * @param valid_flags	Bit mask controlling flags to copy. Any 1 bit means
 *			that the corresponding bit in src->flags is copied
 *			over to dst->flags
 */
static void keyscan_copy_config(const struct ec_mkbp_config *src,
				 struct ec_mkbp_config *dst,
				 uint32_t valid_mask, uint8_t valid_flags)
{
	uint8_t new_flags;

	if (valid_mask & EC_MKBP_VALID_SCAN_PERIOD)
		dst->scan_period_us = src->scan_period_us;
	if (valid_mask & EC_MKBP_VALID_POLL_TIMEOUT)
		dst->poll_timeout_us = src->poll_timeout_us;
	if (valid_mask & EC_MKBP_VALID_MIN_POST_SCAN_DELAY) {
		/*
		 * Key scanning is high priority, so we should require at
		 * least 100us min delay here. Setting this to 0 will cause
		 * watchdog events. Use 200 to be safe.
		 */
		dst->min_post_scan_delay_us =
			MAX(src->min_post_scan_delay_us, 200);
	}
	if (valid_mask & EC_MKBP_VALID_OUTPUT_SETTLE)
		dst->output_settle_us = src->output_settle_us;
	if (valid_mask & EC_MKBP_VALID_DEBOUNCE_DOWN)
		dst->debounce_down_us = src->debounce_down_us;
	if (valid_mask & EC_MKBP_VALID_DEBOUNCE_UP)
		dst->debounce_up_us = src->debounce_up_us;
	if (valid_mask & EC_MKBP_VALID_FIFO_MAX_DEPTH) {
		/* Sanity check for fifo depth */
		dst->fifo_max_depth = MIN(src->fifo_max_depth,
					  KB_FIFO_DEPTH);
	}
	new_flags = dst->flags & ~valid_flags;
	new_flags |= src->flags & valid_flags;
	dst->flags = new_flags;

	/*
	 * If we just enabled key scanning, kick the task so that it will
	 * fall out of the task_wait_event() in keyboard_scan_task().
	 */
	if ((new_flags & EC_MKBP_FLAGS_ENABLE) &&
			!(dst->flags & EC_MKBP_FLAGS_ENABLE))
		task_wake(TASK_ID_KEYSCAN);
}

static int host_command_mkbp_set_config(struct host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_set_config *req = args->params;

	keyscan_copy_config(&req->config, &config,
			    config.valid_mask & req->config.valid_mask,
			    config.valid_flags & req->config.valid_flags);

	return EC_RES_SUCCESS;
}

static int host_command_mkbp_get_config(struct host_cmd_handler_args *args)
{
	struct ec_response_mkbp_get_config *resp = args->response;

	memcpy(&resp->config, &config, sizeof(config));
	args->response_size = sizeof(*resp);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_MKBP_SET_CONFIG,
		     host_command_mkbp_set_config,
		     EC_VER_MASK(0));

DECLARE_HOST_COMMAND(EC_CMD_MKBP_GET_CONFIG,
		     host_command_mkbp_get_config,
		     EC_VER_MASK(0));
