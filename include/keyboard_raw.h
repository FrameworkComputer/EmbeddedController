/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Raw access to keyboard GPIOs.
 *
 * The keyboard matrix is read by driving output signals on the column lines
 * and reading the row lines.
 */

#ifndef __CROS_EC_KEYBOARD_RAW_H
#define __CROS_EC_KEYBOARD_RAW_H

#include "builtin/assert.h"
#include "common.h"
#include "gpio_signal.h"
#include "keyboard_config.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Column values for keyboard_raw_drive_column() */
enum keyboard_column_index {
	KEYBOARD_COLUMN_ALL = -2, /* Drive all columns */
	KEYBOARD_COLUMN_NONE = -1, /* Drive no columns (tri-state all) */
	/* 0 ~ KEYBOARD_COLS_MAX-1 for the corresponding column */
};

/**
 * Initialize the raw keyboard interface.
 *
 * Must be called before any other functions in this interface.
 */
void keyboard_raw_init(void);

/**
 * Finish intitialization after task scheduling has started.
 *
 * Call from the keyboard scan task.
 */
void keyboard_raw_task_start(void);

/**
 * Drive the specified column low.
 *
 * Other columns are tristated.  See enum keyboard_column_index for special
 * values for <col>.
 */
void keyboard_raw_drive_column(int col);

/**
 * Read raw row state.
 *
 * Bits are 1 if signal is present, 0 if not present.
 */
int keyboard_raw_read_rows(void);

/**
 * Enable or disable keyboard interrupts.
 *
 * Enabling interrupts will clear any pending interrupt bits.  To avoid missing
 * any interrupts that occur between the end of scanning and then, you should
 * call keyboard_raw_read_rows() after this.  If it returns non-zero, disable
 * interrupts and go back to polling mode instead of waiting for an interrupt.
 */
void keyboard_raw_enable_interrupt(int enable);

/**
 * Enable or disable keyboard alternative function.
 *
 * @param enable 1 enable KSI/KSO alternative function,
 *               0 set all KSI/KSO pins to normal GPIO.
 */
void keybaord_raw_config_alt(bool enable);

#ifdef HAS_TASK_KEYSCAN

/**
 * GPIO interrupt for raw keyboard input
 */
void keyboard_raw_gpio_interrupt(enum gpio_signal signal);

#else
static inline void keyboard_raw_gpio_interrupt(enum gpio_signal signal)
{
}
#endif /* !HAS_TASK_KEYSCAN */

/**
 * Run keyboard factory test scanning.
 *
 * @return non-zero if keyboard pins are shorted.
 */
int keyboard_factory_test_scan(void);

/**
 * Return true if the current value of the given input GPIO port is zero
 *
 * @param port: GPIO port/bank number
 * @param id:   GPIO index in <port>
 * @return true:input is zero, false:otherwise
 */
int keyboard_raw_is_input_low(int port, int id);

static inline int keyboard_raw_get_cols(void)
{
	return keyboard_cols;
}

static inline void keyboard_raw_set_cols(int cols)
{
	/* We can only decrease the column size. You have to assume a larger
	 * grid (and reduce scanning size if the keyboard has no keypad). */
	assert(cols <= KEYBOARD_COLS_MAX);

	keyboard_cols = cols;
}

#ifdef CONFIG_KEYBOARD_CUSTOMIZATION
/* The board implements this function to control the of the keyboard column.
 * For example, use the gpio to drive 0 or 1 for the refresh key column.
 * @param col: If the value is greater than or equal to 0, the function drive
 *             the specific column.
 *             If the value is KEYBOARD_COLUMN_NONE, drive nothing.
 *             If the value is KEYBOARD_COLUMN_ALL, drive all columns.
 *             Otherwise, do nothing.
 */
void board_keyboard_drive_col(int col);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_KEYBOARD_RAW_H */
