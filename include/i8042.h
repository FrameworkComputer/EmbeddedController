/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * i8042 keyboard protocol
 */

#ifndef __CROS_EC_I8042_H
#define __CROS_EC_I8042_H

#include "common.h"

/**
 * Flush and reset all i8042 keyboard buffers.
 */
void i8042_flush_buffer(void);

/**
 * Notify the i8042 module when a byte is written by the host.
 *
 * Note: This is called in interrupt context by the LPC interrupt handler.
 *
 * @param data		Byte written by host
 * @param is_cmd        Is byte command (!=0) or data (0)
 */
void i8042_receive(int data, int is_cmd);

/**
 * Enable keyboard IRQ generation.
 *
 * @param enable	Enable (!=0) or disable (0) IRQ generation.
 */
void i8042_enable_keyboard_irq(int enable);

/**
 * Send a scan code to the host.
 *
 * The EC lib will push the scan code bytes to host via port 0x60 and assert
 * the IBF flag to trigger an interrupt.  The EC lib must queue them if the
 * host cannot read the previous byte away in time.
 *
 * @param len		Number of bytes to send to the host
 * @param to_host	Data to send
 */
void i8042_send_to_host(int len, const uint8_t *to_host);

#endif  /* __CROS_EC_I8042_H */
