/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_VIRTUAL_BATTERY_H
#define __CROS_EC_VIRTUAL_BATTERY_H

#if defined(CONFIG_I2C_VIRTUAL_BATTERY) && defined(CONFIG_BATTERY_SMART)
#define VIRTUAL_BATTERY_ADDR_FLAGS BATTERY_ADDR_FLAGS
#endif

/**
 * Read/write value of battery parameter from charge state.
 *
 * @param batt_cmd_head	The beginning of the smart battery command
 * @param dest		Destination buffer for data
 * @param read_len	Number of bytes to read to the buffer
 * @param write_len	Number of bytes to write
 * @return EC_SUCCESS if successful, non-zero if error.
 *
 */
int virtual_battery_operation(const uint8_t *batt_cmd_head,
			      uint8_t *dest,
			      int read_len,
			      int write_len);

/**
 * Parse a command for virtual battery function.
 *
 * @param resp		Pointer to the data structure to store the i2c messages
 * @param in_len	Accumulative number of bytes read
 * @param err_code	Pointer to the return value of i2c_xfer() or
 *			virtual_battery_operation()
 * @param xferflags	Flags
 * @param read_len	Number of bytes to read
 * @param write_len	Number of bytes to write
 * @param out		Data to send
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int virtual_battery_handler(struct ec_response_i2c_passthru *resp,
				   int in_len, int *err_code, int xferflags,
				   int read_len, int write_len,
				   const uint8_t *out);

/* Reset the state machine and static variables. */
void reset_parse_state(void);

#endif /* __CROS_EC_VIRTUAL_BATTERY_H */
