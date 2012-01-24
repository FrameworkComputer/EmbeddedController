/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "adc.h"
#include "board.h"
#include "console.h"
#include "i2c.h"
#include "temp_sensor.h"
#include "uart.h"
#include "util.h"


/* Address of temp sensors in system */
#define TEMP0_ADDR ((0x40 << 1) | I2C_FLAG_BIG_ENDIAN)
#ifdef BOARD_link
#define TEMP1_ADDR ((0x41 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP2_ADDR ((0x43 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP3_ADDR ((0x45 << 1) | I2C_FLAG_BIG_ENDIAN)
#endif

/* Address of battery charger */
#define CHARGER_ADDR 0x12

/* Address of battery */
#define BATTERY_ADDR 0x16


static const int i2c_addrs[] = {
	TEMP0_ADDR,
#ifdef BOARD_link
	TEMP1_ADDR, TEMP2_ADDR, TEMP3_ADDR,
#endif
};

int temp_sensor_read(enum temp_sensor_id id)
{
	int traw, t;
	int rv;

	switch(id) {
	case TEMP_SENSOR_CASE:
		/* TODO: fix temperature correction factor.  For now,
		 * just return the die temperature. */
		return temp_sensor_read(TEMP_SENSOR_CASE_DIE);

	case TEMP_SENSOR_CASE_DIE:
		rv = i2c_read16(I2C_PORT_THERMAL, TEMP0_ADDR, 0x01, &traw);
		if (rv)
			return -1;
		t = (int)(int16_t)traw / 128;
		return t + 273;

	case TEMP_SENSOR_EC_INTERNAL:
		return adc_read_ec_temperature();
	}

	/* If we're still here, we don't handle that sensor */
	return -1;
}


/*****************************************************************************/
/* Console commands */

static int command_temps(int argc, char **argv)
{
	int vraw, v;
	int traw, t;
	int rv;
	int d;
	int i;

	uart_puts("Reading temperature sensors...\n");

	for (i = 0; i < ARRAY_SIZE(i2c_addrs); i++) {
		int a = i2c_addrs[i];
		uart_printf("Sensor at 0x%02x:\n", a);

		rv = i2c_read16(I2C_PORT_THERMAL, a, 0xfe, &d);
		if (rv)
			return rv;
		uart_printf("  Manufacturer ID: 0x%04x\n", d);

		rv = i2c_read16(I2C_PORT_THERMAL, a, 0xff, &d);
		uart_printf("  Device ID:       0x%04x\n", d);

		rv = i2c_read16(I2C_PORT_THERMAL, a, 0x02, &d);
		uart_printf("  Config:          0x%04x\n", d);

		rv = i2c_read16(I2C_PORT_THERMAL, a, 0x01, &traw);
		t = ((int)(int16_t)traw * 100) / 128;
		uart_printf("  Die Temperature: 0x%04x = %d.%02d C\n",
			    traw, t / 100, t > 0 ? t % 100 : 100 - (t % 100));

		rv = i2c_read16(I2C_PORT_THERMAL, a, 0x00, &vraw);
		v = ((int)(int16_t)vraw * 15625) / 100;
		uart_printf("  Voltage:         0x%04x = %d nV\n", vraw, v);
		/* TODO: calculate remote temperature from voltage offset */

		uart_flush_output();
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(temps, command_temps);


/* TODO: the battery charger would normally be on a separate I2C bus.
 * For evaluation, it's on the same bus as the thermal sensor, so I
 * put the debug command here for now. */
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


/* TODO: the battery would normally be on a separate I2C bus.  For
 * evaluation, it's on the same bus as the thermal sensor so I put the
 * debug command here for now. */
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

int temp_sensor_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

#ifdef CONFIG_SENSOR
	/* TODO: not necessary since these are the power-on defaults,
	 * except for the DRDY pin.  It's unclear DRDY will be used
	 * anyway. */

	/* Configure the sensor:
	 * 0x7000 = bits 14:12 = continuous conversion
	 * 0x0400 = bits 11:9  = ADC conversion rate (1/sec)
	 * 0x0100 = bit 8      = DRDY pin enabled */
	/* TODO: support shutdown mode for power-saving? */
	i2c_write16(I2C_PORT_THERMAL, TEMP0_ADDR, 0x02, 0x7500);
#endif

	return EC_SUCCESS;
}
