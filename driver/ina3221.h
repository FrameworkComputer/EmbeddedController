/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI INA3221 Current/Power monitor driver.
 */

#ifndef __CROS_EC_INA3221_H
#define __CROS_EC_INA3221_H

#define INA3221_REG_CONFIG	0x00
#define INA3221_REG_MASK	0x0F

/*
 * Common bits are:
 * Reset
 * average = 1
 * conversion time = 1.1 ms
 * mode = shunt and bus, continuous.
 */
#define INA3221_CONFIG_BASE	0x8127

/* Bus voltage: lower 3 bits clear, LSB = 8 mV */
#define INA3221_BUS_MV(reg) (reg)
/* Shunt voltage: lower 3 bits clear, LSB = 40 uV */
#define INA3221_SHUNT_UV(reg) ((reg) * (40/8))

enum ina3221_channel {
	INA3221_CHAN_1 = 0,
	INA3221_CHAN_2 = 1,
	INA3221_CHAN_3 = 2,
	INA3221_CHAN_COUNT = 3
};

/* Registers for each channel */
enum ina3221_register {
	INA3221_SHUNT_VOLT = 0,
	INA3221_BUS_VOLT = 1,
	INA3221_CRITICAL = 2,
	INA3221_WARNING = 3,
	INA3221_MAX_REG = 4
};

/* Configuration table - defined in board file. */
struct ina3221_t {
	int port;             /* I2C port index */
	uint8_t address;      /* I2C address */
	const char *name[INA3221_CHAN_COUNT];  /* Channel names */
};

/* External config in board file */
extern const struct ina3221_t ina3221[];
extern const unsigned int ina3221_count;

#endif /* __CROS_EC_INA3221_H */
