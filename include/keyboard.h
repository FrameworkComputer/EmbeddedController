/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The functions implemented by keyboard component of EC core.
 */

#ifndef __INCLUDE_KEYBOARD_H
#define __INCLUDE_KEYBOARD_H

#include "common.h"

/***************************************************************************/
/* Functions exported by common/keyboard.c.
 */

#define MAX_SCAN_CODE_LEN 4

#define MAX_KBLOG 512

/* Called by keyboard scan code once any key state change (after de-bounce),
 *
 * This function will look up matrix table and convert scancode host.
 */
void keyboard_state_changed(int row, int col, int is_pressed);


/* Handle the port 0x60 writes from host.
 *
 * This functions returns the number of bytes stored in *output buffer.
 */
int handle_keyboard_data(uint8_t data, uint8_t *output);

/* Handle the port 0x64 writes from host.
 *
 * This functions returns the number of bytes stored in *output buffer.
 * BUT theose bytes will appear at port 0x60.
 */
int handle_keyboard_command(uint8_t command, uint8_t *output);


/* Send make/break code of power button to host.
 */
void keyboard_set_power_button(int pressed);


/* Log the keyboard-related information */
void kblog_put(char type, uint8_t byte);

/***************************************************************************/
/* Below is the interface with the underlying chip-dependent code.
 */
#define MAX_KEYBOARD_MATRIX_ROWS 8
#define MAX_KEYBOARD_MATRIX_COLS 16

/* Clear the keyboard buffer to host. */
void keyboard_clear_underlying_buffer(void);

/* Asks the underlayer EC lib what keys are pressed right now.
 *
 * Sets bit_array to a debounced array of which keys are currently pressed,
 * where a 1-bit means the key is pressed. For example, if only row=2 col=3
 * is pressed, it would set bit_array to {0, 0, 0x08, 0, ...}
 *
 * bit_array must be at least MAX_KEYBOARD_MATRIX_COLS bytes long.
 */
enum ec_error_list keyboard_get_state(uint8_t *bit_array);

/* Returns true if the to-host-buffer is non-empty. */
int keyboard_has_char(void);

/* Sends a char to host and triggers IRQ if specified. */
void keyboard_put_char(uint8_t chr, int send_irq);

/* Clears the keyboard buffer to host. */
void keyboard_clear_buffer(void);

/* Host just resumes the interrupt. Sends an interrupt if buffer is non-empty.
 */
void keyboard_resume_interrupt(void);

#endif  /* __INCLUDE_KEYBOARD_H */
