/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charger/battery debug command module for Chrome EC */

/* TODO: remove this or merge into charger/battery modules
 * once charger and battery modules are ready.
 */

#include "charger.h"
#include "board.h"
#include "i2c.h"
#include "console.h"
#include "uart.h"
#include "util.h"

/* Address of battery charger */
#define CHARGER_ADDR 0x12

/* Address of battery */
#define BATTERY_ADDR 0x16

/*****************************************************************************/
/* Console commands */

static int command_charger(int argc, char **argv)
{
	int rv;
	int d;

	uart_puts("Reading battery charger...\n");

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0xfe, &d);
	if (rv)
		return rv;
	uart_printf("  Manufacturer ID: 0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0xff, &d);
	uart_printf("  Device ID:       0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0x12, &d);
	uart_printf("  Option:          0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0x14, &d);
	uart_printf("  Charge current:  0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0x15, &d);
	uart_printf("  Charge voltage:  0x%04x\n", d);

	rv = i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, 0x3f, &d);
	uart_printf("  Input current:   0x%04x\n", d);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(charger, command_charger);

static int command_battery(int argc, char **argv)
{
	int rv;
	int d;

	uart_puts("Reading battery...\n");

	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x08, &d);
	if (rv)
		return rv;
	uart_printf("  Temperature:            0x%04x = %d C\n",
		    d, (d-2731)/10);

	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x09, &d);
	uart_printf("  Voltage:                0x%04x = %d mV\n", d, d);

	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x0f, &d);
	uart_printf("  Remaining capacity:     0x%04x = %d mAh\n", d, d);
	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x10, &d);
	uart_printf("  Full charge capacity:   0x%04x = %d mAh\n", d, d);

	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x14, &d);
	uart_printf("  Desired charge current: 0x%04x = %d mA\n", d, d);
	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, 0x15, &d);
	uart_printf("  Desired charge voltage: 0x%04x = %d mV\n", d, d);


	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battery, command_battery);

/*****************************************************************************/
/* Initialization */

int charger_init(void)
{
	return EC_SUCCESS;
}
