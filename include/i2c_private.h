/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Private chipset-specific implementations that only accessible by
 * i2c_controller.c. Don't include this directly unless you are implementing
 * these functions.
 */
#ifndef __CROS_EC_I2C_PRIVATE_H
#define __CROS_EC_I2C_PRIVATE_H

#include "i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Chip-level function to transmit one block of raw data, then receive one
 * block of raw data.
 *
 * This is a low-level chip-dependent function and should only be called by
 * i2c_xfer().
 *
 * @param port		Port to access
 * @param addr_flags	Peripheral device address
 * @param out		Data to send
 * @param out_size	Number of bytes to send
 * @param in		Destination buffer for received data
 * @param in_size	Number of bytes to receive
 * @param flags		Flags (see I2C_XFER_* above)
 * @return EC_SUCCESS, or non-zero if error.
 */
int chip_i2c_xfer(const int port, const uint16_t addr_flags, const uint8_t *out,
		  int out_size, uint8_t *in, int in_size, int flags);

/**
 * Chip level function to set bus speed.
 *
 * @param port:		Port to access
 * @param kbps:		Bus speed in kbps.
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int chip_i2c_set_freq(int port, enum i2c_freq freq);

/**
 * Chip level function to get bus speed.
 *
 * @param port:		Port to access
 *
 * @return Bus speed
 */
enum i2c_freq chip_i2c_get_freq(int port);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_I2C_PRIVATE_H */
