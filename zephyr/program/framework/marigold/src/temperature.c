/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "chipset.h"
#include "charge_state.h"
#include "customized_shared_memory.h"
#include "temp_sensor.h"
#include "temp_sensor/f75303.h"
#include "temp_sensor/f75397.h"
#include "temp_sensor/temp_sensor.h"
#include "peci.h"

/* update the mk temperature to offset EC_MEMMAP_CUSTOM_TEMP */
__override void board_update_temperature_mk(enum temp_sensor_id id)
{
	int temp_mk_ptr = KELVIN_TO_MILLI_KELVIN(C_TO_K(0)) / 100;
	struct charge_state_data *curr;

	
	switch (id){
	case 0:
		/* QN3, local-f75397 */
		break;
	case 1:
		f75303_get_val_mk(F75303_SENSOR_ID(DT_NODELABEL(cpu_f75303)),
				 &temp_mk_ptr);
		temp_mk_ptr = temp_mk_ptr / 100;
		/* QN2, cpu-f75303 */
		break;
	case 2:
		/* battery temp */
		curr = charge_get_status();
		temp_mk_ptr = curr->batt.temperature;
		break;
	case 3:
		/* QN1, ddr_f75303 */
		f75303_get_val_mk(F75303_SENSOR_ID(DT_NODELABEL(ddr_f75303)),
				 &temp_mk_ptr);
		temp_mk_ptr = temp_mk_ptr / 100;
		break;
	case 4:
		/* PECI temp */
		peci_temp_sensor_get_val(0, &temp_mk_ptr);
		temp_mk_ptr = temp_mk_ptr * 10;
		break;
	default:
		break;
	}

	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_DTT_TEMP + id * 2) = (uint8_t)(temp_mk_ptr & 0xFF);
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_DTT_TEMP + id * 2 + 1) = (uint8_t)((temp_mk_ptr >> 8) & 0xFF);
}
