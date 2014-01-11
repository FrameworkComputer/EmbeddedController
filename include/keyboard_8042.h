/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
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

/**
 * Called by keyboard scan code once any key state change (after de-bounce),
 *
 * This function will look up matrix table and convert scancode host.
 */
void keyboard_state_changed(int row, int col, int is_pressed);

#endif  /* __CROS_EC_KEYBOARD_8042_H */
