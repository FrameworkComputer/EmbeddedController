/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NXP PCA9555 I/O Port expander driver header
 */

#ifndef __CROS_EC_IOEXPANDER_PCA9555_H
#define __CROS_EC_IOEXPANDER_PCA9555_H

#include "i2c.h"

#define PCA9555_CMD_INPUT_PORT_0		0
#define PCA9555_CMD_INPUT_PORT_1		1
#define PCA9555_CMD_OUTPUT_PORT_0		2
#define PCA9555_CMD_OUTPUT_PORT_1		3
#define PCA9555_CMD_POLARITY_INVERSION_PORT_0	4
#define PCA9555_CMD_POLARITY_INVERSION_PORT_1	5
#define PCA9555_CMD_CONFIGURATION_PORT_0	6
#define PCA9555_CMD_CONFIGURATION_PORT_1	7

#define PCA9555_IO_0	BIT(0)
#define PCA9555_IO_1	BIT(1)
#define PCA9555_IO_2	BIT(2)
#define PCA9555_IO_3	BIT(3)
#define PCA9555_IO_4	BIT(4)
#define PCA9555_IO_5	BIT(5)
#define PCA9555_IO_6	BIT(6)
#define PCA9555_IO_7	BIT(7)

static inline int pca9555_read(const int port,
			       const uint16_t i2c_addr_flags,
			       int reg, int *data_ptr)
{
	return i2c_read8(port, i2c_addr_flags, reg, data_ptr);
}

static inline int pca9555_write(const int port,
				const uint16_t i2c_addr_flags,
				int reg, int data)
{
	return i2c_write8(port, i2c_addr_flags, reg, data);
}

#endif /* __CROS_EC_IOEXPANDER_PCA9555_H */
