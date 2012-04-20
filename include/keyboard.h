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

enum scancode_set_list {
  SCANCODE_GET_SET = 0,
  SCANCODE_SET_1,
  SCANCODE_SET_2,
  SCANCODE_SET_3,
  SCANCODE_MAX = SCANCODE_SET_3,
};


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


/* Register the board-specific keyboard matrix translation function.
 * The callback function accepts col/row and returns the scan code.
 *
 * Note that *scan_code must be at least 4 bytes long to store maximum
 * possible sequence.
 */
typedef enum ec_error_list (*keyboard_matrix_callback)(
    int8_t row, int8_t col, int8_t pressed,
    enum scancode_set_list code_set, uint8_t *scan_code, int32_t* len);

enum ec_error_list keyboard_matrix_register_callback(
    int8_t row_num, int8_t col_num,
    keyboard_matrix_callback callback);


/***************************************************************************/
/* Below is the interface with the underlying chip-dependent code.
 */
#define MAX_KEYBOARD_MATRIX_ROWS 8
#define MAX_KEYBOARD_MATRIX_COLS 16

typedef void (*keyboard_callback)(int row, int col, int is_pressed);

/* Registers a callback function to underlayer EC lib. So that any key state
 * change would notify the upper EC main code.
 *
 * Note that passing NULL removes any previously registered callback.
 */
enum ec_error_list keyboard_register_callback(keyboard_callback cb);

/* Asks the underlayer EC lib what keys are pressed right now.
 *
 * Sets bit_array to a debounced array of which keys are currently pressed,
 * where a 1-bit means the key is pressed. For example, if only row=2 col=3
 * is pressed, it would set bit_array to {0, 0, 0x08, 0, ...}
 *
 * bit_array must be at least MAX_KEYBOARD_MATRIX_COLS bytes long.
 */
enum ec_error_list keyboard_get_state(uint8_t *bit_array);

/* Return true if the TOH is still set */
int keyboard_has_char(void);

void keyboard_put_char(uint8_t chr, int send_irq);


#endif  /* __INCLUDE_KEYBOARD_H */
