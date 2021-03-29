/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PECI interface for Chrome EC */

#include "chipset.h"
#include "console.h"
#include "peci.h"
#include "util.h"

static int peci_get_cpu_temp(int *cpu_temp)
{
	int rv;
	uint8_t r_buf[PECI_GET_TEMP_READ_LENGTH] = {0};
	struct peci_data peci = {
		.cmd_code = PECI_CMD_GET_TEMP,
		.addr = PECI_TARGET_ADDRESS,
		.w_len = PECI_GET_TEMP_WRITE_LENGTH,
		.r_len = PECI_GET_TEMP_READ_LENGTH,
		.w_buf = NULL,
		.r_buf = r_buf,
		.timeout_us = PECI_GET_TEMP_TIMEOUT_US,
	};

	rv = peci_transaction(&peci);
	if (rv)
		return rv;

	/* Get relative raw data of temperature. */
	*cpu_temp = (r_buf[1] << 8) | r_buf[0];

	/* Convert relative raw data to degrees C. */
	*cpu_temp = ((*cpu_temp ^ 0xFFFF) + 1) >> 6;

	/*
	 * When the AP transitions into S0, it is possible, depending on the
	 * timing of the PECI sample, to read an invalid temperature. This is
	 * very rare, but when it does happen the temperature returned is
	 * greater than or equal to CONFIG_PECI_TJMAX.
	 */
	if (*cpu_temp >= CONFIG_PECI_TJMAX)
		return EC_ERROR_UNKNOWN;

	/* temperature in K */
	*cpu_temp = CONFIG_PECI_TJMAX - *cpu_temp + 273;

	return EC_SUCCESS;
}

__overridable int stop_read_peci_temp(void)
{
	if (!chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_STANDBY))
		return EC_ERROR_NOT_POWERED;
	else
		return EC_SUCCESS;
}

int peci_temp_sensor_get_val(int idx, int *temp_ptr)
{
	int i, rv;

	rv = stop_read_peci_temp();

	if (rv != EC_SUCCESS)
		return rv;

	/*
	 * Retry reading PECI CPU temperature if the first sample is
	 * invalid or failed to obtain.
	 */
	for (i = 0; i < 2; i++) {
		rv = peci_get_cpu_temp(temp_ptr);
		if (!rv)
			break;
	}

	return rv;
}

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_PECI
static int peci_cmd(int argc, char **argv)
{
	uint8_t r_buf[PECI_READ_DATA_FIFO_SIZE] = {0};
	uint8_t w_buf[PECI_WRITE_DATA_FIFO_SIZE] = {0};
	struct peci_data peci = {
		.w_buf = w_buf,
		.r_buf = r_buf,
	};

	int param;
	char *e;

	if ((argc < 6) || (argc > 8))
		return EC_ERROR_PARAM_COUNT;

	peci.addr = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	peci.w_len = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	peci.r_len = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	peci.cmd_code = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	peci.timeout_us = strtoi(argv[5], &e, 0);
	if (*e)
		return EC_ERROR_PARAM5;

	if (argc > 6) {
		param = strtoi(argv[6], &e, 0);
		if (*e)
			return EC_ERROR_PARAM6;

		/* MSB of parameter */
		w_buf[3] = (uint8_t)(param >> 24);
		/* LSB of parameter */
		w_buf[2] = (uint8_t)(param >> 16);
		/* Index */
		w_buf[1] = (uint8_t)(param >> 8);
		/* Host ID[7:1] & Retry[0] */
		w_buf[0] = (uint8_t)(param >> 0);

		if (argc > 7) {
			param = strtoi(argv[7], &e, 0);
			if (*e)
				return EC_ERROR_PARAM7;

			/* Data (1, 2 or 4 bytes) */
			w_buf[7] = (uint8_t)(param >> 24);
			w_buf[6] = (uint8_t)(param >> 16);
			w_buf[5] = (uint8_t)(param >> 8);
			w_buf[4] = (uint8_t)(param >> 0);
		}
	} else {
		peci.w_len = 0x00;
	}

	if (peci_transaction(&peci)) {
		ccprintf("PECI transaction error\n");
		return EC_ERROR_UNKNOWN;
	}
	ccprintf("PECI read data: %ph\n", HEX_BUF(r_buf, peci.r_len));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(peci, peci_cmd,
			"addr wlen rlen cmd timeout(us)",
			"PECI command");

static int command_peci_temp(int argc, char **argv)
{
	int t;

	if (peci_get_cpu_temp(&t) != EC_SUCCESS) {
		ccprintf("PECI get cpu temp error\n");
		return EC_ERROR_UNKNOWN;
	}

	ccprintf("CPU temp: %d K, %d C\n", t, K_TO_C(t));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pecitemp, command_peci_temp,
			NULL,
			"Print CPU temperature");
#endif /* CONFIG_CMD_PECI */
