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

#define PCA9555_IO_0	(1 << 0)
#define PCA9555_IO_1	(1 << 1)
#define PCA9555_IO_2	(1 << 2)
#define PCA9555_IO_3	(1 << 3)
#define PCA9555_IO_4	(1 << 4)
#define PCA9555_IO_5	(1 << 5)
#define PCA9555_IO_6	(1 << 6)
#define PCA9555_IO_7	(1 << 7)

static inline int pca9555_read(int port, int addr, int reg, int *data_ptr)
{
	return i2c_read8(port, addr, reg, data_ptr);
}

static inline int pca9555_write(int port, int addr, int reg, int data)
{
	return i2c_write8(port, addr, reg, data);
}

#endif /* __CROS_EC_IOEXPANDER_PCA9555_H */
