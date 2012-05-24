/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Keyboard scanner module for Chrome EC
 *
 * TODO: Finish cleaning up nomenclature (cols/rows/inputs/outputs),
 */

#include "atomic.h"
#include "board.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "keyboard.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYSCAN, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYSCAN, format, ## args)

/* used for select_column() */
enum COL_INDEX {
	COL_ASSERT_ALL = -2,
	COL_TRI_STATE_ALL = -1,
	/* 0 ~ 12 for the corresponding column */
};

#define POLLING_MODE_TIMEOUT 100000   /* 100 ms */
#define SCAN_LOOP_DELAY 10000         /*  10 ms */

/* 15:14, 12:8, 2 */
#define IRQ_MASK 0xdf04

/* The keyboard state from the last read */
static uint8_t raw_state[KB_OUTPUTS];

/* status of keyboard related switches */
static uint8_t switches;

/* Mask with 1 bits only for keys that actually exist */
static const uint8_t *actual_key_mask;

/* All actual key masks (todo: move to keyboard matrix definition) */
/* TODO: (crosbug.com/p/7485) fill in real key mask with 0-bits for coords that
   aren't keys */
static const uint8_t actual_key_masks[4][KB_OUTPUTS] = {
	{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	{0},
	{0},
	{0},
	};

/* Key masks for special boot keys */
#define MASK_INDEX_ESC     1
#define MASK_VALUE_ESC     0x02
#define MASK_INDEX_REFRESH 2
#define MASK_VALUE_REFRESH 0x04

struct kbc_gpio {
	int num;		/* logical row or column number */
	uint32_t port;
	int pin;
};

#if defined(BOARD_daisy) || defined(BOARD_snow)
static const uint32_t ports[] = { GPIO_B, GPIO_C, GPIO_D };
#else
#error "Need to specify GPIO ports used by keyboard"
#endif

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
static uint8_t kb_fifo[KB_FIFO_DEPTH][KB_OUTPUTS];

/* clear keyboard state variables */
void keyboard_clear_state(void)
{
	int i;

	CPRINTF("clearing keyboard fifo\n");
	kb_fifo_start = 0;
	kb_fifo_end = 0;
	kb_fifo_entries = 0;
	for (i = 0; i < KB_FIFO_DEPTH; i++)
		memset(kb_fifo[i], 0, KB_OUTPUTS);
}

/**
  * Add keyboard state into FIFO
  *
  * @return EC_SUCCESS if entry added, EC_ERROR_OVERFLOW if FIFO is full
  */
static int kb_fifo_add(uint8_t *buffp)
{
	int ret = EC_SUCCESS;

	if (kb_fifo_entries == KB_FIFO_DEPTH) {
		CPRINTF("%s: FIFO depth reached\n", __func__);
		ret = EC_ERROR_OVERFLOW;
		goto kb_fifo_push_done;
	}

	memcpy(kb_fifo[kb_fifo_end], buffp, KB_OUTPUTS);

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
		memcpy(buffp, kb_fifo[(kb_fifo_start - 1) % KB_FIFO_DEPTH],
		       KB_OUTPUTS);

		/*
		 * Bail out without changing any FIFO indices and let the
		 * caller know something strange happened. The buffer will
		 * will contain the last known state of the keyboard.
		 */
		return EC_ERROR_UNKNOWN;
	}
	memcpy(buffp, kb_fifo[kb_fifo_start], KB_OUTPUTS);

	kb_fifo_start = (kb_fifo_start + 1) % KB_FIFO_DEPTH;

	atomic_sub(&kb_fifo_entries, 1);

	return EC_SUCCESS;
}

static void select_column(int col)
{
	int i, done = 0;

	for (i = 0; i < ARRAY_SIZE(ports); i++) {
		uint32_t bsrr = 0;
		int j;

		for (j = GPIO_KB_OUT00; j <= GPIO_KB_OUT12; j++) {
			if (gpio_list[j].port != ports[i])
				continue;

			if (col == COL_ASSERT_ALL) {
				/* drive low (clear output data) */
				bsrr |= gpio_list[j].mask << 16;
			} else if (col == COL_TRI_STATE_ALL) {
				/* put column in hi-Z state (set output data) */
				bsrr |= gpio_list[j].mask;
			} else {
				/* drive specified column low, others => hi-Z */
				if (j - GPIO_KB_OUT00 == col) {
					/* to avoid conflict, tri-state all
					 * columns first, then assert column */
					select_column(COL_TRI_STATE_ALL);
					bsrr |= gpio_list[j].mask << 16;
					done = 1;
					break;
				}
			}
		}

		if (bsrr)
			STM32_GPIO_BSRR_OFF(ports[i]) = bsrr;

		if (done)
			break;
	}
}


void wait_for_interrupt(void)
{
	uint32_t pr_before, pr_after;

	/* Assert all outputs would trigger un-wanted interrupts.
	 * Clear them before enable interrupt. */
	pr_before = STM32_EXTI_PR;
	select_column(COL_ASSERT_ALL);
	pr_after = STM32_EXTI_PR;
	STM32_EXTI_PR |= ((pr_after & ~pr_before) & IRQ_MASK);

	STM32_EXTI_IMR |= IRQ_MASK;	/* 1: unmask interrupt */
}


void enter_polling_mode(void)
{
	STM32_EXTI_IMR &= ~IRQ_MASK;	/* 0: mask interrupts */
	select_column(COL_TRI_STATE_ALL);
}


/* Returns 1 if any key is still pressed. 0 if no key is pressed. */
static int check_keys_changed(void)
{
	int c;
	uint8_t r;
	int change = 0;
	int num_press = 0;

	for (c = 0; c < KB_OUTPUTS; c++) {
		uint16_t tmp;

		/* Select column, then wait a bit for it to settle */
		select_column(c);
		udelay(50);

		r = 0;
		tmp = STM32_GPIO_IDR(C);
		/* KB_COL00:04 = PC8:12 */
		if (tmp & (1 << 8))
			r |= 1 << 0;
		if (tmp & (1 << 9))
			r |= 1 << 1;
		if (tmp & (1 << 10))
			r |= 1 << 2;
		if (tmp & (1 << 11))
			r |= 1 << 3;
		if (tmp & (1 << 12))
			r |= 1 << 4;
		/* KB_COL05:06 = PC14:15 */
		if (tmp & (1 << 14))
			r |= 1 << 5;
		if (tmp & (1 << 15))
			r |= 1 << 6;

		tmp = STM32_GPIO_IDR(D);
		/* KB_COL07 = PD2 */
		if (tmp & (1 << 2))
			r |= 1 << 7;

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

		/* Check for changes */
		if (r != raw_state[c]) {
			raw_state[c] = r;
			change = 1;
		}
	}
	select_column(COL_TRI_STATE_ALL);

	/* Count number of key pressed */
	for (c = 0; c < KB_OUTPUTS; c++) {
		if (raw_state[c])
			++num_press;
	}

	if (change) {
		board_keyboard_suppress_noise();

		CPRINTF("[%d keys pressed: ", num_press);
		for (c = 0; c < KB_OUTPUTS; c++) {
			if (raw_state[c])
				CPRINTF(" %02x", raw_state[c]);
			else
				CPUTS(" --");
		}
		CPUTS("]\n");

		if (kb_fifo_add(raw_state) == EC_SUCCESS)
			board_interrupt_host(1);
		else
			CPRINTF("dropped keystroke\n");
	}

	return num_press ? 1 : 0;
}


/* Returns non-zero if the user has triggered a recovery reset by pushing
 * Power + Refresh + ESC. */
static int check_recovery_key(void)
{
	int c;

	/* check the recovery key only if we're booting due to a
	 * reset-pin-caused reset. */
	if (system_get_reset_cause() != SYSTEM_RESET_RESET_PIN)
		return 0;

	/* cold boot : Power + Refresh were pressed,
	 * check if ESC is also pressed for recovery. */
	if (!(raw_state[MASK_INDEX_ESC] & MASK_VALUE_ESC))
		return 0;

	/* Make sure only other allowed keys are pressed.  This protects
	 * against accidentally triggering the special key when a cat sits on
	 * your keyboard.  Currently, only the requested key and ESC are
	 * allowed. */
	for (c = 0; c < KB_OUTPUTS; c++) {
		if (raw_state[c] &&
		(c != MASK_INDEX_ESC || raw_state[c] != MASK_VALUE_ESC) &&
		(c != MASK_INDEX_REFRESH || raw_state[c] != MASK_VALUE_REFRESH))
			return 0;  /* Additional disallowed key pressed */
	}

	CPRINTF("Keyboard RECOVERY detected !\n");
	return 1;
}


int keyboard_scan_init(void)
{
	/* Tri-state (put into Hi-Z) the outputs */
	select_column(COL_TRI_STATE_ALL);

	/* TODO: method to set which keyboard we have, so we set the actual
	 * key mask properly */
	actual_key_mask = actual_key_masks[0];

	/* Initialize raw state */
	check_keys_changed();

	/* is recovery key pressed on cold startup ? */
	switches |= check_recovery_key() ?
			EC_SWITCH_KEYBOARD_RECOVERY : 0;

	return EC_SUCCESS;
}


void keyboard_scan_task(void)
{
	int key_press_timer = 0;

	/* Enable interrupts for keyboard matrix inputs */
	gpio_enable_interrupt(GPIO_KB_IN00);
	gpio_enable_interrupt(GPIO_KB_IN01);
	gpio_enable_interrupt(GPIO_KB_IN02);
	gpio_enable_interrupt(GPIO_KB_IN03);
	gpio_enable_interrupt(GPIO_KB_IN04);
	gpio_enable_interrupt(GPIO_KB_IN05);
	gpio_enable_interrupt(GPIO_KB_IN06);
	gpio_enable_interrupt(GPIO_KB_IN07);

	while (1) {
		wait_for_interrupt();
		task_wait_event(-1);

		enter_polling_mode();
		/* Busy polling keyboard state. */
		while (1) {
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
		/* TODO: (crosbug.com/p/7484) A race condition here.
		 *       If a key state is changed here (before interrupt is
		 *       enabled), it will be lost.
		 */
	}
}


void matrix_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_KEYSCAN);
}

int keyboard_has_char()
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
	return switches & EC_SWITCH_KEYBOARD_RECOVERY;
}

static int keyboard_get_scan(uint8_t *data, int *resp_size)
{
	kb_fifo_remove(data);
	if (!kb_fifo_entries)
		board_interrupt_host(0);
	*resp_size = KB_OUTPUTS;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_STATE, keyboard_get_scan);

static int keyboard_get_info(uint8_t *data, int *resp_size)
{
	struct ec_response_mkbp_info *r = (struct ec_response_mkbp_info *)data;

	r->rows = 8;
	r->cols = KB_OUTPUTS;
	r->switches = switches;

	*resp_size = sizeof(struct ec_response_mkbp_info);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_INFO, keyboard_get_info);
