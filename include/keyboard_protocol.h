/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Keyboard protocol interface
 */

#ifndef __CROS_EC_KEYBOARD_PROTOCOL_H
#define __CROS_EC_KEYBOARD_PROTOCOL_H

#include "common.h"
#include "button.h"

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
#endif

#endif  /* __CROS_EC_KEYBOARD_PROTOCOL_H */
