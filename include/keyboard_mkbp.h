/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MKBP keyboard protocol
 */

#ifndef __CROS_EC_KEYBOARD_MKBP_H
#define __CROS_EC_KEYBOARD_MKBP_H

#include "common.h"
#include "keyboard_config.h"

/**
 * Add keyboard state into FIFO
 *
 * @return EC_SUCCESS if entry added, EC_ERROR_OVERFLOW if FIFO is full
 */
int keyboard_fifo_add(const uint8_t *buffp);

/**
 * Add an element to the common MKBP FIFO.
 *
 * @param event_type	The MKBP event type.
 * @param buffp		Pointer to the event data to enqueue.
 * @return EC_SUCCESS if entry added, EC_ERROR_OVERFLOW if FIFO is full.
 */
int mkbp_fifo_add(uint8_t event_type, const uint8_t *buffp);

/**
 * Clear the MKBP common FIFO.
 */
void mkbp_clear_fifo(void);

/**
 * Send KEY_BATTERY keystroke.
 */
#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
void keyboard_send_battery_key(void);
#else
static inline void keyboard_send_battery_key(void) { }
#endif

/**
 * Update the state of the switches.
 *
 * @param sw		The switch that changed.
 * @param state		The state of the switch.
 */
void mkbp_update_switches(uint32_t sw, int state);

#endif  /* __CROS_EC_KEYBOARD_MKBP_H */
