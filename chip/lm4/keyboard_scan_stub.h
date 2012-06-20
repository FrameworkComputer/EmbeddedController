/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Funcitons needed by keyboard scanner module.
 * Abstracted from keyboard_scan.c for stub.
 */

#ifndef __CROS_EC_KEYBOARD_SCAN_STUB_H
#define __CROS_EC_KEYBOARD_SCAN_STUB_H

/* used for select_column() */
enum COLUMN_INDEX {
	COLUMN_ASSERT_ALL = -2,
	COLUMN_TRI_STATE_ALL = -1,
	/* 0 ~ 12 for the corresponding column */
};

/* Set keyboard scanning to enabled/disabled */
void lm4_set_scanning_enabled(int enabled);

/* Get keyboard scanning enabled/disabled */
int lm4_get_scanning_enabled(void);

/* Drive the specified column low; other columns are tristated */
void lm4_select_column(int col);

/* Clear current interrupt status */
uint32_t lm4_clear_matrix_interrupt_status(void);

/* Enable interrupt from keyboard matrix */
void lm4_enable_matrix_interrupt(void);

/* Disable interrupt from keyboard matrix */
void lm4_disable_matrix_interrupt(void);

/* Read raw row state */
int lm4_read_raw_row_state(void);

/* Configure keyboard matrix GPIO */
void lm4_configure_keyboard_gpio(void);

#endif  /* __CROS_EC_KEYBOARD_SCAN_STUB_H */
