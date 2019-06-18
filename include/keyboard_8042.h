/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The functions implemented by keyboard component of EC core.
 */

#ifndef __CROS_EC_KEYBOARD_8042_H
#define __CROS_EC_KEYBOARD_8042_H

#include "common.h"
#include "button.h"

/**
 * Called by power button handler and button interrupt handler.
 *
 * This function sends the corresponding make or break code to the host.
 */
void button_state_changed(enum keyboard_button_type button, int is_pressed);

/**
 * Notify the keyboard module when a byte is written by the host.
 *
 * Note: This is called in interrupt context by the LPC interrupt handler.
 *
 * @param data		Byte written by host
 * @param is_cmd        Is byte command (!=0) or data (0)
 */
void keyboard_host_write(int data, int is_cmd);

/*
 * Board specific callback function when a key state is changed.
 *
 * A board may watch key events and create some easter eggs, or apply dynamic
 * translation to the make code (i.e., remap keys).
 *
 * Returning EC_SUCCESS implies *make_code is still a valid make code to be
 * processed. Any other return value will abort processing of this make code.
 * If callback alters *make_code or aborts key processing when pressed=1, it is
 * responsible for also altering/aborting the matching pressed=0 call.
 *
 * @param make_code	Pointer to scan code (set 2) of key in action.
 * @param pressed	Is the key being pressed (1) or released (0).
 */
enum ec_error_list keyboard_scancode_callback(uint16_t *make_code,
					      int8_t pressed);

#endif  /* __CROS_EC_KEYBOARD_8042_H */
