/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI INA219/231 Current/Power monitor driver.
 */

#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "timer.h"
#include "ina2xx.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* 8-bit I2C base address */
#define INA2XX_I2C_ADDR (0x40 << 1)

uint16_t ina2xx_read(uint8_t idx, uint8_t reg)
{
	int res;
	int val;
	uint8_t addr = INA2XX_I2C_ADDR | (idx << 1);

	res = i2c_read16(I2C_PORT_MASTER, addr, reg, &val);
	if (res) {
		CPRINTS("INA2XX I2C read failed");
		return 0x0bad;
	}
	return (val >> 8) | ((val & 0xff) << 8);
}

int ina2xx_write(uint8_t idx, uint8_t reg, uint16_t val)
{
	int res;
	uint8_t addr = INA2XX_I2C_ADDR | (idx << 1);
	uint16_t be_val = (val >> 8) | ((val & 0xff) << 8);

	res = i2c_write16(I2C_PORT_MASTER, addr, reg, be_val);
	if (res)
		CPRINTS("INA2XX I2C write failed");
	return res;
}

int ina2xx_init(uint8_t idx, uint16_t config, uint16_t calib)
{
	int res;

	res = ina2xx_write(idx, INA2XX_REG_CONFIG, config);
	/* TODO(crosbug.com/p/29730): assume 1mA/LSB, revisit later */
	res |= ina2xx_write(idx, INA2XX_REG_CALIB, calib);

	return res;
}

int ina2xx_get_voltage(uint8_t idx)
{
	uint16_t bv = ina2xx_read(idx, INA2XX_REG_BUS_VOLT);
	return INA2XX_BUS_MV((int)bv);
}

int ina2xx_get_current(uint8_t idx)
{
	int16_t curr = ina2xx_read(idx, INA2XX_REG_CURRENT);
	/* Current calibration: LSB = 1mA/bit */
	return (int)curr;
}

int ina2xx_get_power(uint8_t idx)
{
	uint16_t pow = ina2xx_read(idx, INA2XX_REG_POWER);
	return INA2XX_POW_MW((int)pow);
}

static void ina2xx_dump(uint8_t idx)
{
	uint16_t cfg = ina2xx_read(idx, INA2XX_REG_CONFIG);
	int16_t sv = ina2xx_read(idx, INA2XX_REG_SHUNT_VOLT);
	uint16_t bv = ina2xx_read(idx, INA2XX_REG_BUS_VOLT);
	uint16_t pow = ina2xx_read(idx, INA2XX_REG_POWER);
	int16_t curr = ina2xx_read(idx, INA2XX_REG_CURRENT);
	uint16_t calib = ina2xx_read(idx, INA2XX_REG_CALIB);
	uint16_t mask = ina2xx_read(idx, INA2XX_REG_MASK);
	uint16_t alert = ina2xx_read(idx, INA2XX_REG_ALERT);

	ccprintf("Configuration: %04x\n", cfg);
	ccprintf("Shunt voltage: %04x => %d uV\n", sv,
						   INA2XX_SHUNT_UV((int)sv));
	ccprintf("Bus voltage  : %04x => %d mV\n", bv,
						   INA2XX_BUS_MV((int)bv));
	ccprintf("Power        : %04x => %d mW\n", pow,
						   INA2XX_POW_MW((int)pow));
	ccprintf("Current      : %04x => %d mA\n", curr, curr);
	ccprintf("Calibration  : %04x\n", calib);
	ccprintf("Mask/Enable  : %04x\n", mask);
	ccprintf("Alert limit  : %04x\n", alert);
}

/*****************************************************************************/
/* Console commands */

static int command_ina(int argc, char **argv)
{
	char *e;
	int idx;
	uint16_t val;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	idx = strtoi(argv[1], &e, 10);
	if (*e)
		return EC_ERROR_PARAM1;

	if (2 == argc) { /* dump all registers */
		ina2xx_dump(idx);
		return EC_SUCCESS;
	} else if (4 == argc) {
		val = strtoi(argv[3], &e, 16);
		if (*e)
			return EC_ERROR_PARAM3;

		if (!strcasecmp(argv[2], "config")) {
			ina2xx_write(idx, INA2XX_REG_CONFIG, val);
		} else if (!strcasecmp(argv[2], "calib")) {
			ina2xx_write(idx, INA2XX_REG_CALIB, val);
		} else if (!strcasecmp(argv[2], "mask")) {
			ina2xx_write(idx, INA2XX_REG_MASK, val);
		} else if (!strcasecmp(argv[2], "alert")) {
			ina2xx_write(idx, INA2XX_REG_ALERT, val);
		} else { /* read one register */
			ccprintf("Invalid register: %s\n", argv[1]);
			return EC_ERROR_INVAL;
		}
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(ina, command_ina,
			"<index> [config|calib|mask|alert <val>]",
			"INA2XX power/current sensing",
			NULL);
