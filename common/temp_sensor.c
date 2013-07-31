/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "chip_temp_sensor.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "hooks.h"
#include "host_command.h"
#include "peci.h"
#include "task.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "timer.h"
#include "tmp006.h"
#include "util.h"

/* Default temperature to report in mapped memory */
#define MAPPED_TEMP_DEFAULT (296 - EC_TEMP_SENSOR_OFFSET)

test_mockable int temp_sensor_read(enum temp_sensor_id id, int *temp_ptr)
{
	const struct temp_sensor_t *sensor;

	if (id < 0 || id >= TEMP_SENSOR_COUNT)
		return EC_ERROR_INVAL;
	sensor = temp_sensors + id;

	return sensor->read(sensor->idx, temp_ptr);
}

static void update_mapped_memory(void)
{
	int i, t;
	uint8_t *mptr = host_get_memmap(EC_MEMMAP_TEMP_SENSOR);

	for (i = 0; i < TEMP_SENSOR_COUNT; i++, mptr++) {
		/*
		 * Switch to second range if first one is full, or stop if
		 * second range is also full.
		 */
		if (i == EC_TEMP_SENSOR_ENTRIES)
			mptr = host_get_memmap(EC_MEMMAP_TEMP_SENSOR_B);
		else if (i >= EC_TEMP_SENSOR_ENTRIES +
			 EC_TEMP_SENSOR_B_ENTRIES)
			break;

		switch (temp_sensor_read(i, &t)) {
		case EC_ERROR_NOT_POWERED:
			*mptr = EC_TEMP_SENSOR_NOT_POWERED;
			break;
		case EC_ERROR_NOT_CALIBRATED:
			*mptr = EC_TEMP_SENSOR_NOT_CALIBRATED;
			break;
		case EC_SUCCESS:
			*mptr = t - EC_TEMP_SENSOR_OFFSET;
			break;
		default:
			*mptr = EC_TEMP_SENSOR_ERROR;
		}
	}
}
/* Run after other tick tasks, so sensors will have updated first. */
DECLARE_HOOK(HOOK_SECOND, update_mapped_memory, HOOK_PRIO_DEFAULT + 1);

static void temp_sensor_init(void)
{
	int i;
	uint8_t *base, *base_b;

	/*
	 * Initialize memory-mapped data so that if a temperature value is read
	 * before we actually poll the sensors, we don't end up with an insane
	 * value.
	 */
	base = host_get_memmap(EC_MEMMAP_TEMP_SENSOR);
	base_b = host_get_memmap(EC_MEMMAP_TEMP_SENSOR_B);
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		if (i < EC_TEMP_SENSOR_ENTRIES)
			base[i] = MAPPED_TEMP_DEFAULT;
		else
			base_b[i - EC_TEMP_SENSOR_ENTRIES] =
				MAPPED_TEMP_DEFAULT;
	}

	/* Set the rest of memory region to SENSOR_NOT_PRESENT */
	for (; i < EC_TEMP_SENSOR_ENTRIES + EC_TEMP_SENSOR_B_ENTRIES; ++i) {
		if (i < EC_TEMP_SENSOR_ENTRIES)
			base[i] = EC_TEMP_SENSOR_NOT_PRESENT;
		else
			base_b[i - EC_TEMP_SENSOR_ENTRIES] =
				EC_TEMP_SENSOR_NOT_PRESENT;
	}

	/* Temp sensor data is present, with B range supported. */
	*host_get_memmap(EC_MEMMAP_THERMAL_VERSION) = 2;
}
DECLARE_HOOK(HOOK_INIT, temp_sensor_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */

static int command_temps(int argc, char **argv)
{
	int t, i;
	int rv, rv1 = EC_SUCCESS;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		ccprintf("  %-20s: ", temp_sensors[i].name);
		rv = temp_sensor_read(i, &t);
		if (rv)
			rv1 = rv;

		switch (rv) {
		case EC_SUCCESS:
			ccprintf("%d K = %d C\n", t, K_TO_C(t));
			break;
		case EC_ERROR_NOT_POWERED:
			ccprintf("Not powered\n");
			break;
		case EC_ERROR_NOT_CALIBRATED:
			ccprintf("Not calibrated\n");
			break;
		default:
			ccprintf("Error %d\n", rv);
		}
	}

	return rv1;
}
DECLARE_CONSOLE_COMMAND(temps, command_temps,
			NULL,
			"Print temp sensors",
			NULL);

/*****************************************************************************/
/* Host commands */

int temp_sensor_command_get_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_temp_sensor_get_info *p = args->params;
	struct ec_response_temp_sensor_get_info *r = args->response;
	int id = p->id;

	if (id >= TEMP_SENSOR_COUNT)
		return EC_RES_ERROR;

	strzcpy(r->sensor_name, temp_sensors[id].name, sizeof(r->sensor_name));
	r->sensor_type = temp_sensors[id].type;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TEMP_SENSOR_GET_INFO,
		     temp_sensor_command_get_info,
		     EC_VER_MASK(0));
