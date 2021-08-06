/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "amd_stt.h"
#include "common.h"
#include "console.h"
#include "driver/sb_rmi.h"
#include "hooks.h"
#include "math_util.h"
#include "temp_sensor.h"
#include "util.h"

/* Debug flag can be toggled with console command: stt debug */
static bool amd_stt_debug;

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

static const char * const amd_stt_sensor_name[] = {
	[AMD_STT_PCB_SENSOR_APU] = "APU",
	[AMD_STT_PCB_SENSOR_REMOTE] = "Ambient",
	[AMD_STT_PCB_SENSOR_GPU] = "GPU",
};

/**
 * Write temperature sensor value to AP via SB-RMI
 *
 * sensor:
 *	AMD_STT_PCB_SENSOR_APU
 *	AMD_STT_PCB_SENSOR_REMOTE
 *	AMD_STT_PCB_SENSOR_GPU
 *
 * temp_mk:
 *	Temperature in degrees milli kelvin
 */
static int write_stt_sensor_val(enum amd_stt_pcb_sensor sensor, int temp_mk)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;
	int temp_mc;
	int temp_c_fp_msb;
	int temp_c_fp_lsb;

	temp_mc = MILLI_KELVIN_TO_MILLI_CELSIUS(temp_mk);

	if (amd_stt_debug)
		CPRINTS("STT: %s = %d.%d °C", amd_stt_sensor_name[sensor],
			temp_mc / 1000, temp_mc % 1000);

	/* Divide by 1000 to get MSB of fixed point temp */
	temp_c_fp_msb = temp_mc / 1000;
	/* Modulus 1000 and multiply by 256/1000 to get LSB of fixed point*/
	temp_c_fp_lsb = ((temp_mc % 1000) << 8) / 1000;

	/*
	 * [15:0]: temperature as signed integer with 8 fractional bits.
	 * [23:16]: sensor index
	 * [31:24]: unused
	 */
	msgIn |= (temp_c_fp_lsb & 0xff);
	msgIn |= (temp_c_fp_msb & 0xff) << 8;
	msgIn |= (sensor & 0xff) << 16;
	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_STT_SENSOR_CMD, msgIn, &msgOut);
}

static void amd_stt_handler(void)
{
	int rv;
	int soc_temp_mk;
	int ambient_temp_mk;

	/*
	 * TODO(b/192391025): Replace with temp_sensor_read_mk(TEMP_SENSOR_SOC)
	 */
	rv = board_get_soc_temp_mk(&soc_temp_mk);
	if (rv) {
		CPRINTS("STT: Failed to read SOC temp rv:%d", rv);
		return;
	}

	rv = write_stt_sensor_val(AMD_STT_PCB_SENSOR_APU, soc_temp_mk);
	if (rv) {
		CPRINTS("STT: Failed to write SOC temp rv:%d", rv);
		return;
	}

	/*
	 * TODO(b/192391025): Replace with
	 *	temp_sensor_read_mk(TEMP_SENSOR_AMBIENT)
	 */
	rv = board_get_ambient_temp_mk(&ambient_temp_mk);
	if (rv) {
		CPRINTS("STT: Failed to read AMBIENT temp rv:%d", rv);
		return;
	}

	rv = write_stt_sensor_val(AMD_STT_PCB_SENSOR_REMOTE, ambient_temp_mk);
	if (rv) {
		CPRINTS("STT: Failed to write AMBIENT temp rv:%d", rv);
		return;
	}
}
DECLARE_HOOK(HOOK_SECOND, amd_stt_handler, HOOK_PRIO_TEMP_SENSOR+1);

static int command_stt(int argc, char **argv)
{
	int sensor_id;
	int temp;

	if (argc < 2)
		return EC_ERROR_PARAM1;
	else if (!strcasecmp(argv[1], "debug")) {
		amd_stt_debug = !amd_stt_debug;
		return EC_SUCCESS;
	} else if (argc != 3)
		return EC_ERROR_PARAM2;
	else if (!strcasecmp(argv[1],
			     amd_stt_sensor_name[AMD_STT_PCB_SENSOR_APU]))
		sensor_id = AMD_STT_PCB_SENSOR_APU;
	else if (!strcasecmp(argv[1],
			     amd_stt_sensor_name[AMD_STT_PCB_SENSOR_REMOTE]))
		sensor_id = AMD_STT_PCB_SENSOR_REMOTE;
	else if (!strcasecmp(argv[1],
			     amd_stt_sensor_name[AMD_STT_PCB_SENSOR_GPU]))
		sensor_id = AMD_STT_PCB_SENSOR_GPU;
	else
		return EC_ERROR_PARAM2;

	temp = atoi(argv[2]);

	return write_stt_sensor_val(sensor_id, temp);
}
DECLARE_CONSOLE_COMMAND(stt, command_stt,
			"<apu|ambient|gpu|debug> <temp in mK>",
			"Write an STT mK temperature to AP");
