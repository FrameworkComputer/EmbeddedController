/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "charger.h"
#include "console.h"
#include "i2c.h"
#include "util.h"

/* Address of battery charger */
#define CHARGER_ADDR 0x12

/* Address of battery */
#define BATTERY_ADDR 0x16

int charger_init(void)
{
	return EC_SUCCESS;
}

/*****************************************************************************/
/* Console commands */

static int command_charger(int argc, char **argv)
{
	int rv;
	int d;

	ccputs("Reading battery charger...\n");

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0xfe, &d);
	if (rv)
		return rv;
	ccprintf("  Manufacturer ID: 0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0xff, &d);
	ccprintf("  Device ID:       0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0x12, &d);
	ccprintf("  Option:          0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0x14, &d);
	ccprintf("  Charge current:  0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0x15, &d);
	ccprintf("  Charge voltage:  0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0x3f, &d);
	ccprintf("  Input current:   0x%04x\n", d);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(charger, command_charger);


static int command_battery(int argc, char **argv)
{
	int rv;
	int d;

	ccputs("Reading battery...\n");

	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x08, &d);
	if (rv)
		return rv;
	ccprintf("  Temperature:            0x%04x = %d C\n",
		 d, (d-2731)/10);

	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x09, &d);
	ccprintf("  Voltage:                0x%04x = %d mV\n", d, d);

	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x0f, &d);
	ccprintf("  Remaining capacity:     0x%04x = %d mAh\n", d, d);
	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x10, &d);
	ccprintf("  Full charge capacity:   0x%04x = %d mAh\n", d, d);

	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x14, &d);
	ccprintf("  Desired charge current: 0x%04x = %d mA\n", d, d);
	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x15, &d);
	ccprintf("  Desired charge voltage: 0x%04x = %d mV\n", d, d);


	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battery, command_battery);

