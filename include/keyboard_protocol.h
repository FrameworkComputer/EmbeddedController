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

/*
 * Process a single key for a press or release, can take either a row, col pair
 * or a raw key code. This is normally called by keyboard_state_changed().
 *
 * @param row the key row
 * @param col the key column
 * @param is_pressed true if the key has been pressed, false if it's been
 *                   released
 * @param override_code the 8042 key code or -1 to use row, col instead
 */
void keyboard_state_changed_process(int row, int col, int is_pressed,
				    int override_code);

/**
 * Returns true if keyboard backlight is present/detected.
 */
int board_has_keyboard_backlight(void);

#if defined(CONFIG_USB_HID_KEYBOARD_VIVALDI) || \
	defined(CONFIG_KEYBOARD_VIVALDI) ||     \
	defined(CONFIG_USB_DC_HID_VIVALDI) || defined(CONFIG_USBD_HID_VIVALDI)
/*
 * This function can help change the keyboard top row layout as presented to the
 * AP. If changing the position of the "Refresh" key from T3, you may also need
 * to change KEYBOARD_ROW_REFRESH accordingly so that recovery mode can work on
 * the EC side of things (also see related CONFIG_KEYBOARD_REFRESH_ROW3)
 */
__override_proto const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_KEYBOARD_PROTOCOL_H */
