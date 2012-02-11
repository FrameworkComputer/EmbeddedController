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

int temp_sensor_tmp006_read_die_temp(const struct temp_sensor_t* sensor)
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

/* Calculate the remote object temperature.
 * Parameters:
 *     Tdie: Die temperature in 1/100 K.
 *     Vobj: Voltage read from register 0. In nV.
 *     S0:   Sensitivity factor in 1/1000.
 * Return:
 *     Object temperature in 1/100 K.
 */
int temp_sensor_tmp006_calculate_object_temp(int Tdie, int Vobj, int S0)
{
	int32_t Tx, S19, Vos, Vx, fv9, ub, lb;

	/* Calculate according to TMP006 users guide.
	 * Division is delayed when possible to preserve precision, but should
	 * not cause overflow.
	 * Assuming Tdie is between 200K and 400K, and S0 between 3e-14 and
	 * 9e-14, the maximum value during the calculation should be less than
	 * (1 << 30), which fits in int32_t.
	 */
	Tx = Tdie - 29815;
	/* S19 is the sensitivity multipled by 1e19 */
	S19 = S0 * (100000 + 175 * Tx / 100 -
		1678 * Tx / 100 * Tx / 100000) / 1000;
	/* Vos is the offset voltage in nV */
	Vos = -29400 - 570 * Tx / 100 + 463 * Tx / 100 * Tx / 10000;
	Vx = Vobj - Vos;
	/* fv9 is Seebeck coefficient f(Vobj) multipled by 1e9 */
	fv9 = Vx + 134 * Vx / 100000 * Vx / 100000;

	/* The last step in the calculation involves square root, so we use
	 * binary search.
	 * Assuming the object temperature is between 200K and 400K, the search
	 * should take at most 14 iterations.
	 */
	ub = 40000;
	lb = 20000;
	while (lb != ub) {
		int32_t t, rhs, lhs;

		t = (ub + lb) / 2;
		lhs = t / 100 * t / 10000 * t / 10000 * (S19/100) / 1000 * t;
		rhs = Tdie / 100 * Tdie / 10000 * Tdie / 10000 * (S19/100) / 1000 *
			Tdie + fv9 * 1000;
		if (lhs > rhs)
			ub = t;
		else
			lb = t + 1;
	}

	return ub;
}

int temp_sensor_tmp006_read_object_temp(const struct temp_sensor_t* sensor)
{
	int traw, t;
	int vraw, v;
	int rv;
	int addr = sensor->addr;

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x01, &traw);
	if (rv)
		return -1;
	t = (int)(int16_t)traw / 128 + 273;

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x00, &vraw);
	if (rv)
		return -1;
	v = ((int)(int16_t)vraw * 15625) / 100;

	return temp_sensor_tmp006_calculate_object_temp(t * 100, v, 6400);
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
		t = temp_sensor_read(i);
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

static int command_sensor_info(int argc, char **argv)
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

/* TMP006 object temperature calculation command.
 * TODO: This command is only for debugging. Remove it when temporal correciton
 *       is done.
 */
static int command_sensor_remote(int argc, char **argv)
{
	char *e;
	int32_t Td2, Vobj9, Sm03;

	if (argc != 4) {
		uart_puts("Usage: tempcorrect <Tdie*100> <Vobj*10^9> <S0*10^11>\n");
		return EC_ERROR_UNKNOWN;
	}

	Td2 = strtoi(argv[1], &e, 0);
	if (e && *e) {
		uart_puts("Bad Tdie.\n");
		return EC_ERROR_UNKNOWN;
	}

	Vobj9 = strtoi(argv[2], &e, 0);
	if (e && *e) {
		uart_puts("Bad Vobj.\n");
		return EC_ERROR_UNKNOWN;
	}

	Sm03 = strtoi(argv[3], &e, 0);
	if (e && *e) {
		uart_puts("Bad S0.\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_printf("%d\n",
		temp_sensor_tmp006_calculate_object_temp(Td2, Vobj9, Sm03));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tempremote, command_sensor_remote);

/*****************************************************************************/
/* Initialization */

int temp_sensor_init(void)
{
	return EC_SUCCESS;
}
