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
#include "lpc.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYSCAN, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYSCAN, format, ## args)


/* Notes:
 *
 * Link proto0 board:
 *
 *   Columns (outputs):
 *      KSO0 - KSO7  = PP0:7
 *      KSO8 - KSO12 = PQ0:4
 *
 *   Rows (inputs):
 *      KSI0 - KSI7  = PN0:7
 *
 *   Other:
 *      PWR_BTN#     = PK7 (handled by gpio module)
 */


/* used for select_column() */
enum COLUMN_INDEX {
	COLUMN_ASSERT_ALL = -2,
	COLUMN_TRI_STATE_ALL = -1,
	/* 0 ~ 12 for the corresponding column */
};

#define POLLING_MODE_TIMEOUT 1000000  /* 1 sec */
#define SCAN_LOOP_DELAY 10000         /* 10 ms */
#define COLUMN_CHARGE_US 40           /* Column charge time in usec */

#define KB_COLS 13


static int enable_scanning = 1;
static uint8_t raw_state[KB_COLS];
static uint8_t raw_state_at_boot[KB_COLS];
static int recovery_key_pressed;

/* Mask with 1 bits only for keys that actually exist */
static const uint8_t *actual_key_mask;

/* All actual key masks (todo: move to keyboard matrix definition) */
/* TODO: (crosbug.com/p/7485) fill in real key mask with 0-bits for coords that
   aren't keys */
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


/* Drive the specified column low; other columns are tri-stated */
static void select_column(int col)
{
	if (col == COLUMN_TRI_STATE_ALL || !enable_scanning) {
		/* Tri-state all outputs */
		LM4_GPIO_DIR(LM4_GPIO_P) = 0;
		LM4_GPIO_DIR(LM4_GPIO_Q) &= ~0x1f;
	} else if (col == COLUMN_ASSERT_ALL) {
		/* Assert all outputs */
		LM4_GPIO_DIR(LM4_GPIO_P) = 0xff;
		LM4_GPIO_DIR(LM4_GPIO_Q) |= 0x1f;
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = 0;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0x1f) = 0;
	} else {
		/* Assert a single output */
		LM4_GPIO_DIR(LM4_GPIO_P) = 0;
		LM4_GPIO_DIR(LM4_GPIO_Q) &= ~0x1f;
		if (col < 8) {
			LM4_GPIO_DIR(LM4_GPIO_P) |= 1 << col;
			LM4_GPIO_DATA(LM4_GPIO_P, 1 << col) = 0;
		} else {
			LM4_GPIO_DIR(LM4_GPIO_Q) |= 1 << (col - 8);
			LM4_GPIO_DATA(LM4_GPIO_Q, 1 << (col - 8)) = 0;
		}
	}
}


static uint32_t clear_matrix_interrupt_status(void) {
	uint32_t ris = LM4_GPIO_RIS(KB_SCAN_ROW_GPIO);
	LM4_GPIO_ICR(KB_SCAN_ROW_GPIO) = ris;

	return ris;
}


static void wait_for_interrupt(void)
{
	CPUTS("[KB wait]\n");

	/* Assert all outputs would trigger un-wanted interrupts.
	 * Clear them before enable interrupt. */
	select_column(COLUMN_ASSERT_ALL);
	clear_matrix_interrupt_status();

	LM4_GPIO_IS(KB_SCAN_ROW_GPIO) = 0;      /* 0: edge-sensitive */
	LM4_GPIO_IBE(KB_SCAN_ROW_GPIO) = 0xff;  /* 1: both edge */
	LM4_GPIO_IM(KB_SCAN_ROW_GPIO) = 0xff;   /* 1: enable interrupt */
}


static void enter_polling_mode(void)
{
	CPUTS("[KB poll]\n");
	LM4_GPIO_IM(KB_SCAN_ROW_GPIO) = 0;  /* 0: disable interrupt */
	select_column(COLUMN_TRI_STATE_ALL);
}


/* Update the raw key state without sending messages.  Used in pre-init, so
 * must not make task-switching-dependent calls like usleep(); udelay() is ok
 * because it's a spin-loop. */
static void update_key_state(void)
{
	int c;
	uint8_t r;

	for (c = 0; c < KB_COLS; c++) {
		/* Select column, then wait a bit for it to settle */
		select_column(c);
		udelay(COLUMN_CHARGE_US);
		/* Read the row state */
		r = LM4_GPIO_DATA(KB_SCAN_ROW_GPIO, 0xff);
		/* Invert it so 0=not pressed, 1=pressed */
		r ^= 0xff;
		/* Mask off keys that don't exist so they never show
		 * as pressed */
		raw_state[c] = r & actual_key_mask[c];
	}
	select_column(COLUMN_TRI_STATE_ALL);
}


/* Print the raw keyboard state */
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


/* Returns 1 if any key is still pressed. 0 if no key is pressed. */
static int check_keys_changed(void)
{
	int c, c2;
	uint8_t r;
	int change = 0;
	int num_press = 0;
	uint8_t keys[KB_COLS];

	for (c = 0; c < KB_COLS; c++) {
		/* Select column, then wait a bit for it to settle */
		select_column(c);
		udelay(COLUMN_CHARGE_US);
		/* Read the row state */
		r = LM4_GPIO_DATA(KB_SCAN_ROW_GPIO, 0xff);
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
	select_column(COLUMN_TRI_STATE_ALL);

	/* ignore if a ghost key appears. */
	for (c = 0; c < KB_COLS; c++) {
		if (!keys[c]) continue;
		for (c2 = c + 1; c2 < KB_COLS; c2++) {
			uint8_t common = keys[c] & keys[c2];
			if (common & (common - 1)) goto out;
		}
	}

	/* Check for changes */
	for (c = 0; c < KB_COLS; c++) {
		r = keys[c];
		if (r != raw_state[c]) {
			int i;
			for (i = 0; i < 8; ++i) {
				uint8_t prev = (raw_state[c] >> i) & 1;
				uint8_t now = (r >> i) & 1;
				if (prev != now && enable_scanning) {
					keyboard_state_changed(i, c, now);
				}
			}
			raw_state[c] = r;
			change = 1;
		}
	}

	if (change)
		print_raw_state("raw state");

out:
	/* Count number of key pressed */
	for (c = 0; c < KB_COLS; c++) {
		if (raw_state[c]) ++num_press;
	}

	return num_press ? 1 : 0;
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
	 * your keyboard.  Currently, only the requested key and ESC are
	 * allowed. */
	allowed_mask[index] |= mask;
	allowed_mask[MASK_INDEX_ESC] |= MASK_VALUE_ESC;
	for (c = 0; c < KB_COLS; c++) {
		if (raw_state_at_boot[c] & ~allowed_mask[c])
			return 0;  /* Additional disallowed key pressed */
	}
	return 1;
}


int keyboard_scan_recovery_pressed(void)
{
	return recovery_key_pressed;
}


int keyboard_scan_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable clock to GPIO modules N,P,Q */
	/* TODO: gpio_pre_init() enables all the GPIO modules, so can remove
	 * this entirely.  */
	LM4_SYSTEM_RCGCGPIO |= 0x7000;
	scratch = LM4_SYSTEM_RCGCGPIO;

	/* Clear GPIOAFSEL and enable digital function for rows */
	LM4_GPIO_AFSEL(LM4_GPIO_P) = 0;  /* KSO[7:0] */
	LM4_GPIO_DEN(LM4_GPIO_P) = 0xff;
	LM4_GPIO_AFSEL(LM4_GPIO_Q) &= ~0x1f;  /* KSO[12:8] */
	LM4_GPIO_DEN(LM4_GPIO_Q) |= 0x1f;

	/* Set row inputs with pull-up */
	LM4_GPIO_AFSEL(KB_SCAN_ROW_GPIO) &= 0xff;
	LM4_GPIO_DEN(KB_SCAN_ROW_GPIO) |= 0xff;
	LM4_GPIO_DIR(KB_SCAN_ROW_GPIO) = 0;
	LM4_GPIO_PUR(KB_SCAN_ROW_GPIO) = 0xff;

	/* Tri-state the columns */
	select_column(COLUMN_TRI_STATE_ALL);

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
		/* Proto1 used ESC key */
		/* TODO: (crosbug.com/p/9561) remove once proto1 obsolete */
		if (system_get_board_version() == BOARD_VERSION_PROTO1) {
			recovery_key_pressed =
				check_boot_key(MASK_INDEX_REFRESH,
					       MASK_VALUE_REFRESH);
		} else {
			recovery_key_pressed =
				check_boot_key(MASK_INDEX_ESC, MASK_VALUE_ESC);
		}

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
	if (recovery_key_pressed)
		CPUTS("[KB recovery key pressed at init!]\n");

	/* Enable interrupts */
	task_enable_irq(KB_SCAN_ROW_IRQ);
	enable_scanning = 1;

	while (1) {
		/* Enable all outputs */
		wait_for_interrupt();

		/* Wait for scanning enabled and key pressed. */
		do {
			task_wait_event(-1);
		} while (!enable_scanning);

		enter_polling_mode();
		/* Busy polling keyboard state. */
		while (enable_scanning) {
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
	uint32_t ris = clear_matrix_interrupt_status();

	if (ris)
		task_wake(TASK_ID_KEYSCAN);
}
DECLARE_IRQ(KB_SCAN_ROW_IRQ, matrix_interrupt, 3);


int keyboard_has_char()
{
#if defined(HOST_KB_BUS_LPC)
	return lpc_keyboard_has_char();
#else
#error "keyboard_scan needs to know what bus to use for keyboard interface"
#endif
}


void keyboard_put_char(uint8_t chr, int send_irq)
{
#if defined(HOST_KB_BUS_LPC)
	lpc_keyboard_put_char(chr, send_irq);
#else
#error "keyboard_scan needs to know what bus to use for keyboard interface"
#endif
}


void keyboard_clear_buffer(void)
{
#if defined(HOST_KB_BUS_LPC)
	lpc_keyboard_clear_buffer();
#else
#error "keyboard_scan needs to know what bus to use for keyboard interface"
#endif
}


void keyboard_resume_interrupt(void)
{
#if defined(HOST_KB_BUS_LPC)
	lpc_keyboard_resume_irq();
#else
#error "keyboard_scan needs to know what bus to use for keyboard interface"
#endif
}


int keyboard_get_scan(uint8_t **buffp, int max_bytes)
{
	/* We don't support this API yet; just return -1. */
	return -1;
}


/* The actual implementation is controlling the enable_scanning variable, then
 * that controls whether select_column() can pull-down columns or not. */
void keyboard_enable_scanning(int enable)
{
	enable_scanning = enable;
	if (enable) {
		/* A power button press had tri-stated all columns (see the
		 * 'else' statement below), we need a wake-up to unlock
		 * the task_wait_event() loop after wait_for_interrupt(). */
		task_wake(TASK_ID_KEYSCAN);
	} else {
		select_column(COLUMN_TRI_STATE_ALL);
		keyboard_clear_underlying_buffer();
	}
}
