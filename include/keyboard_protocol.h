/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Keyboard protocol interface
 */

#ifndef __CROS_EC_KEYBOARD_PROTOCOL_H
#define __CROS_EC_KEYBOARD_PROTOCOL_H

#include "button.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Routines common to all protocols */

/**
 * Clear the keyboard buffer to host.
 */
void keyboard_clear_buffer(void);

/*
 * Respond to button changes. Implemented by a host-specific
 * handler.
 *
 * @param button	The button that changed.
 * @param is_pressed	Whether the button is now pressed.
 */
void keyboard_update_button(enum keyboard_button_type button, int is_pressed);

/* Protocol-specific includes */

#ifdef CONFIG_KEYBOARD_PROTOCOL_8042
#include "keyboard_8042.h"
#endif

#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
#include "keyboard_mkbp.h"

/* MKBP protocol takes the whole keyboard matrix, and does not care about
 * individual key presses.
 */
static inline void keyboard_state_changed(int row, int col, int is_pressed)
{
}
#else
/**
 * Called by keyboard scan code once any key state change (after de-bounce),
 *
 * This function will look up matrix table and convert scancode host.
 */
void keyboard_state_changed(int row, int col, int is_pressed);
#endif

/**
 * Returns true if keyboard backlight is present/detected.
 */
int board_has_keyboard_backlight(void);

/*
 * This function can help change the keyboard top row layout as presented to the
 * AP. If changing the position of the "Refresh" key from T3, you may also need
 * to change KEYBOARD_ROW_REFRESH accordingly so that recovery mode can work on
 * the EC side of things (also see related CONFIG_KEYBOARD_REFRESH_ROW3)
 */
__override_proto const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_KEYBOARD_PROTOCOL_H */
