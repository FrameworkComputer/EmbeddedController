/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "board.h"
#include "chip_temp_sensor.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "lpc.h"
#include "ec_commands.h"
#include "peci.h"
#include "task.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "timer.h"
#include "tmp006.h"
#include "util.h"

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
	return sensor->read(sensor->idx);
}


int temp_sensor_powered(enum temp_sensor_id id)
{
	int flag = temp_sensors[id].power_flags;

	if (flag & TEMP_SENSOR_POWER_VS &&
	    gpio_get_level(GPIO_PGOOD_1_8VS) == 0)
		return 0;

	if (flag & TEMP_SENSOR_POWER_CPU &&
	    !chipset_in_state(CHIPSET_STATE_ON))
		return 0;

	return 1;
}


void poll_all_sensors(void)
{
#ifdef CONFIG_TMP006
	tmp006_poll();
#endif
#ifdef CONFIG_PECI
	peci_temp_sensor_poll();
#endif
#ifdef CHIP_lm4
	chip_temp_sensor_poll();
#endif
}


static void update_lpc_mapped_memory(void)
{
	int i, t;
	uint8_t *mapped = lpc_get_memmap_range() + EC_MEMMAP_TEMP_SENSOR;

	memset(mapped, 0xff, 16);

	for (i = 0; i < TEMP_SENSOR_COUNT && i < 16; ++i) {
		if (!temp_sensor_powered(i)) {
			mapped[i] = 0xfd;
			continue;
		}
		t = temp_sensor_read(i);
		if (t != -1)
			mapped[i] = t - EC_TEMP_SENSOR_OFFSET;
		else
			mapped[i] = 0xfe;
	}
}


void temp_sensor_task(void)
{
	while (1) {
		poll_all_sensors();
		update_lpc_mapped_memory();
		usleep(1000000);
	}
}

/*****************************************************************************/
/* Console commands */

static int command_temps(int argc, char **argv)
{
	int i;
	int rv = EC_SUCCESS;
	int t;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		ccprintf("  %-20s: ", temp_sensors[i].name);
		t = temp_sensor_read(i);
		if (t < 0) {
			ccprintf("Error\n");
			rv = EC_ERROR_UNKNOWN;
		} else
			ccprintf("%d K = %d C\n", t, t - 273);
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(temps, command_temps);
