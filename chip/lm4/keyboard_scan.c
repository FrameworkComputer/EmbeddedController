/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#include "board.h"
#include "console.h"
#include "eoption.h"
#include "keyboard.h"
#include "keyboard_scan.h"
#include "keyboard_scan_stub.h"
#include "power_button.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYSCAN, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYSCAN, format, ## args)

#define POLLING_MODE_TIMEOUT 1000000  /* 1 sec */
#define SCAN_LOOP_DELAY 10000         /* 10 ms */
#define COLUMN_CHARGE_US 40           /* Column charge time in usec */

#define KB_COLS 13


static uint8_t raw_state[KB_COLS];
static uint8_t raw_state_at_boot[KB_COLS];

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
#define MASK_INDEX_ESC     1
#define MASK_VALUE_ESC     0x02
#define MASK_INDEX_REFRESH 2
#define MASK_VALUE_REFRESH 0x04
#define MASK_INDEX_D       2
#define MASK_VALUE_D       0x10
#define MASK_INDEX_F       3
#define MASK_VALUE_F       0x10


static void wait_for_interrupt(void)
{
	CPUTS("[KB wait]\n");

	/* Assert all outputs would trigger un-wanted interrupts.
	 * Clear them before enable interrupt. */
	lm4_select_column(COLUMN_ASSERT_ALL);
	lm4_clear_matrix_interrupt_status();

	lm4_enable_matrix_interrupt();
}


static void enter_polling_mode(void)
{
	CPUTS("[KB poll]\n");
	lm4_disable_matrix_interrupt();
	lm4_select_column(COLUMN_TRI_STATE_ALL);
}


/* Update the raw key state without sending messages.  Used in pre-init, so
 * must not make task-switching-dependent calls; udelay() is ok because it's a
 * spin-loop. */
static void update_key_state(void)
{
	int c;
	uint8_t r;

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
		raw_state[c] = r & actual_key_mask[c];
	}
	lm4_select_column(COLUMN_TRI_STATE_ALL);
}


/* Print the raw keyboard state. */
static void print_raw_state(const char *msg)
{
	int c;

	CPRINTF("[KB %s:", msg);
	for (c = 0; c < KB_COLS; c++) {
		if (raw_state[c])
			CPRINTF(" %02x", raw_state[c]);
		else
			CPUTS(" --");
	}
	CPUTS("]\n");
}


/* Return 1 if any key is still pressed, 0 if no key is pressed. */
static int check_keys_changed(void)
{
	int c, c2;
	uint8_t r;
	int change = 0;
	uint8_t keys[KB_COLS];

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

#ifdef OR_WITH_CURRENT_STATE_FOR_TESTING
		/* KLUDGE - or current state in, so we can make sure
		 * all the lines are hooked up */
		r |= raw_state[c];
#endif

		keys[c] = r;
	}
	lm4_select_column(COLUMN_TRI_STATE_ALL);

	/* Ignore if a ghost key appears */
	for (c = 0; c < KB_COLS; c++) {
		if (!keys[c])
			continue;
		for (c2 = c + 1; c2 < KB_COLS; c2++) {
			/* A little bit of cleverness here.  Ghosting happens
			 * if 2 columns share at least 2 keys.  So we OR the
			 * columns together and then see if more than one bit
			 * is set.  x&(x-1) is non-zero only if x has more
			 * than one bit set. */
			uint8_t common = keys[c] & keys[c2];

			if (common & (common - 1))
				goto out;
		}
	}

	/* Check for changes */
	for (c = 0; c < KB_COLS; c++) {
		r = keys[c];
		if (r != raw_state[c]) {
			int i;
			for (i = 0; i < 8; i++) {
				uint8_t prev = (raw_state[c] >> i) & 1;
				uint8_t now = (r >> i) & 1;
				if (prev != now && lm4_get_scanning_enabled())
					keyboard_state_changed(i, c, now);
			}
			raw_state[c] = r;
			change = 1;
		}
	}

	if (change)
		print_raw_state("raw state");

out:
	/* Return non-zero if at least one key is pressed */
	for (c = 0; c < KB_COLS; c++) {
		if (raw_state[c])
			return 1;
	}
	return 0;
}


/* Returns non-zero if the specified key is pressed, and only other allowed
 * keys are pressed. */
static int check_boot_key(int index, int mask)
{
	uint8_t allowed_mask[KB_COLS] = {0};
	int c;

	/* Check for the key */
	if (!(raw_state_at_boot[index] & mask))
		return 0;

	/* Make sure only other allowed keys are pressed.  This protects
	 * against accidentally triggering the special key when a cat sits on
	 * your keyboard.  Currently, only the requested key and the keys used
	 * for the Silego reset are allowed. */
	allowed_mask[index] |= mask;
	allowed_mask[MASK_INDEX_REFRESH] |= MASK_VALUE_REFRESH;

	for (c = 0; c < KB_COLS; c++) {
		if (raw_state_at_boot[c] & ~allowed_mask[c])
			return 0;  /* Additional disallowed key pressed */
	}
	return 1;
}


int keyboard_scan_init(void)
{
	/* Configure GPIO */
	lm4_configure_keyboard_gpio();

	/* Tri-state the columns */
	lm4_select_column(COLUMN_TRI_STATE_ALL);

	/* TODO: method to set which keyboard we have, so we set the actual
	 * key mask properly */
	actual_key_mask = actual_key_masks[0];

	/* Initialize raw state */
	update_key_state();

	/* Copy to the state at boot */
	memcpy(raw_state_at_boot, raw_state, sizeof(raw_state_at_boot));

	/* If we're booting due to a reset-pin-caused reset, check if the
	 * recovery key is pressed. */
	if (system_get_reset_cause() == SYSTEM_RESET_RESET_PIN) {
		power_set_recovery_pressed(check_boot_key(MASK_INDEX_ESC,
							  MASK_VALUE_ESC));

#ifdef CONFIG_FAKE_DEV_SWITCH
		/* Turn fake dev switch on if D pressed, off if F pressed. */
		if (check_boot_key(MASK_INDEX_D, MASK_VALUE_D)) {
			eoption_set_bool(EOPTION_BOOL_FAKE_DEV, 1);
			CPUTS("[Enabling fake dev-mode]\n");
		} else if (check_boot_key(MASK_INDEX_F, MASK_VALUE_F)) {
			eoption_set_bool(EOPTION_BOOL_FAKE_DEV, 0);
			CPUTS("[Disabling fake dev-mode]\n");
		}
#endif
	}

	return EC_SUCCESS;
}


void keyboard_scan_task(void)
{
	int key_press_timer = 0;

	print_raw_state("init state");

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
			/* sleep for debounce. */
			usleep(SCAN_LOOP_DELAY);
			/* Check for keys down */
			if (check_keys_changed()) {
				key_press_timer = 0;
			} else {
				if (++key_press_timer >=
				    (POLLING_MODE_TIMEOUT / SCAN_LOOP_DELAY)) {
					key_press_timer = 0;
					break;  /* exit the while loop */
				}
			}
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


/* The actual implementation is controlling the enable_scanning variable, then
 * that controls whether lm4_select_column() can pull-down columns or not. */
void keyboard_enable_scanning(int enable)
{
	lm4_set_scanning_enabled(enable);
	if (enable) {
		/* A power button press had tri-stated all columns (see the
		 * 'else' statement below), we need a wake-up to unlock
		 * the task_wait_event() loop after wait_for_interrupt(). */
		task_wake(TASK_ID_KEYSCAN);
	} else {
		lm4_select_column(COLUMN_TRI_STATE_ALL);
		keyboard_clear_underlying_buffer();
	}
}
