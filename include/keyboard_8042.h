/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The functions implemented by keyboard component of EC core.
 */

#ifndef __CROS_EC_KEYBOARD_8042_H
#define __CROS_EC_KEYBOARD_8042_H

#include "common.h"

#define MAX_SCAN_CODE_LEN 4

/**
 * Handle the port 0x60 writes from host.
 *
 * This functions returns the number of bytes stored in *output buffer.
 */
int handle_keyboard_data(uint8_t data, uint8_t *output);

/**
 * Handle the port 0x64 writes from host.
 *
 * This functions returns the number of bytes stored in *output buffer.
 * BUT theose bytes will appear at port 0x60.
 */
int handle_keyboard_command(uint8_t command, uint8_t *output);

/**
 * Called by keyboard scan code once any key state change (after de-bounce),
 *
 * This function will look up matrix table and convert scancode host.
 */
void keyboard_state_changed(int row, int col, int is_pressed);

/**
 * Send make/break code of power button to host.
 */
void keyboard_set_power_button(int pressed);

/**
 * Log the keyboard-related information
 */
void kblog_put(char type, uint8_t byte);

#endif  /* __CROS_EC_KEYBOARD_8042_H */
