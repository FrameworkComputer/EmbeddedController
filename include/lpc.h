/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#ifndef __CROS_EC_LPC_H
#define __CROS_EC_LPC_H

#include "common.h"

/* Initializes the LPC module. */
int lpc_init(void);

/* Returns a pointer to the host command data buffer.  This buffer
 * must only be accessed between a notification to
 * host_command_received() and a subsequent call to
 * lpc_SendHostResponse().  <slot> is 0 for kernel-originated
 * commands, 1 for usermode-originated commands. */
uint8_t *lpc_get_host_range(int slot);

/* Sends a response to a host command.  The bottom 4 bits of <status>
 * are sent in the status byte.  <slot> is 0 for kernel-originated
 * commands, 1 for usermode-originated commands. */
void lpc_send_host_response(int slot, int status);

/* Returns non-zero if the COMx interface has received a character. */
int lpc_comx_has_char(void);

/* Returns the next character pending on the COMx interface. */
int lpc_comx_get_char(void);

#endif  /* __CROS_EC_LPC_H */
