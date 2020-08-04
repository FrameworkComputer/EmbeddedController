/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TUSB1064_H
#define __CROS_EC_TUSB1064_H

#include <stdint.h>

#define TUSB1064_ADDR_FLAGS		0x12

#define TUSB1064_REG_GENERAL		0x0a
#define REG_GENERAL_CTLSEL_DISABLE      0x00
#define REG_GENERAL_CTLSEL_USB3         0x01
#define REG_GENERAL_CTLSEL_4DP_LANES    0x02
#define REG_GENERAL_CTLSEL_2DP_AND_USB3 0x03
#define REG_GENERAL_FLIPSEL		BIT(2)
#define REG_GENERAL_DP_ENABLE_CTRL	BIT(3)
#define REG_GENERAL_EQ_OVERRIDE		BIT(4)

/*
 * Initialize the TUSB1064
 *
 * @param port	The I2C port of TUSB1064
 * @return EC_SUCCESS or EC_ERROR_*
 */
int init_tusb1064(int port);

/*
 * Write a byte to the TUSB1064
 *
 * @param port	The I2C port of TUSB1064.
 * @param reg	Register to write byte to.
 * @param val	Value to write to TUSB1064.
 *
 * @return EC_SUCCESS, or EC_ERROR_* on error.
 */
int tusb1064_write_byte(int port, uint8_t reg, uint8_t val);

/*
 * Read a byte from TUSB1064
 *
 * @param port	The I2C port of TUSB1064.
 * @param reg	Register to read byte from.
 *
 * @return	byte value, or -1 on error.
 */
int tusb1064_read_byte(int port, uint8_t reg);

#endif /* __CROS_EC_TUSB1064_H */
