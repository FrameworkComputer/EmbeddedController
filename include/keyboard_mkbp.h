/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MKBP keyboard protocol
 */

#ifndef __CROS_EC_KEYBOARD_MKBP_H
#define __CROS_EC_KEYBOARD_MKBP_H

#include "common.h"

/**
 * Add keyboard state into FIFO
 *
 * @return EC_SUCCESS if entry added, EC_ERROR_OVERFLOW if FIFO is full
 */
int keyboard_fifo_add(const uint8_t *buffp);

/**
 * Send KEY_BATTERY keystroke.
 */
#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
void keyboard_send_battery_key(void);
#else
static inline void keyboard_send_battery_key(void) { }
#endif

#endif  /* __CROS_EC_KEYBOARD_MKBP_H */
