/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "i2c.h"
#include "temp_sensor.h"
#include "uart.h"
#include "util.h"
#include "console.h"
#include "board.h"

/* Defined in board_temp_sensor.c. Must be in the same order as
 * in enum temp_sensor_id.
 */
extern const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT];

int temp_sensor_read(enum temp_sensor_id id)
{
	const struct temp_sensor_t *sensor;

	if (id < 0 || id >= TEMP_SENSOR_COUNT)
		return -1;
	sensor = temp_sensors + id;
	return sensor->read(sensor);
}

int temp_sensor_tmp006_read(const struct temp_sensor_t* sensor)
{
	int traw, t;
	int rv;
	int addr = sensor->addr;

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x01, &traw);
	if (rv)
		return -1;
	t = (int)(int16_t)traw / 128;
	return t + 273;
}

void temp_sensor_tmp006_config(const struct temp_sensor_t* sensor)
{
	int addr = sensor->addr;

	/* Configure the sensor:
	 * 0x7000 = bits 14:12 = continuous conversion
	 * 0x0400 = bits 11:9  = ADC conversion rate (1/sec)
	 * 0x0100 = bit 8      = DRDY pin enabled */

	/* TODO: support shutdown mode for power-saving? */
	i2c_write16(TMP006_PORT(addr), TMP006_REG(addr), 0x02, 0x7500);
}

int temp_sensor_tmp006_print(const struct temp_sensor_t* sensor)
{
	int vraw, v;
	int traw, t;
	int rv;
	int d;
	int addr = sensor->addr;

	uart_printf("Debug data from %s:\n", sensor->name);
	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0xfe, &d);
	if (rv)
		return rv;
	uart_printf("  Manufacturer ID: 0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0xff, &d);
	uart_printf("  Device ID:       0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x02, &d);
	uart_printf("  Config:          0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x00, &vraw);
	v = ((int)(int16_t)vraw * 15625) / 100;
	uart_printf("  Voltage:         0x%04x = %d nV\n", vraw, v);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x01, &traw);
	t = ((int)(int16_t)traw * 100) / 128;
	uart_printf("  Temperature:     0x%04x = %d.%02d C\n",
		    traw, t / 100, t > 0 ? t % 100 : 100 - (t % 100));

	return EC_SUCCESS;
}
/*****************************************************************************/
/* Console commands */

static int command_temps(int argc, char **argv)
{
	int i;
	int rv = 0;
	int t;

	uart_puts("Reading temperature sensors...\n");

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		uart_printf("  Temp from %s:  ", temp_sensors[i].name);
		t = temp_sensor_read(temp_sensors[i].id);
		if (t < 0) {
			uart_printf("Error.\n\n");
			rv = -1;
		}
		else
			uart_printf("%d K\n\n", t);
	}

	if (rv == -1)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(temps, command_temps);

static int command_sensor_info(int argc, char ** argv)
{
	int i;
	int rv;
	const struct temp_sensor_t* sensor;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		sensor = temp_sensors + i;
		if (sensor->print == TEMP_SENSOR_NO_PRINT)
			continue;
		rv = sensor->print(sensor);
		if (rv != EC_SUCCESS)
			return rv;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tempsinfo, command_sensor_info);


/*****************************************************************************/
/* Initialization */

int temp_sensor_init(void)
{
	return EC_SUCCESS;
}
