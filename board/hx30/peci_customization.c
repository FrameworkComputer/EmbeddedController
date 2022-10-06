/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "board.h"
#include "hooks.h"
#include "host_command.h"
#include "peci.h"
#include "peci_customization.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

static int peci_temp;
static int peci_select_count;
static int peci_select_flags;

/*****************************************************************************/
/* Internal functions */

int peci_Rd_Pkg_Config(uint8_t index, uint16_t parameter, int rlen, uint8_t *in)
{
	int rv;
	uint8_t out[PECI_RD_PKG_CONFIG_WRITE_LENGTH];

	struct peci_data peci = {
		.cmd_code = PECI_CMD_RD_PKG_CFG,
		.addr = PECI_TARGET_ADDRESS,
		.w_len = PECI_RD_PKG_CONFIG_WRITE_LENGTH,
		.r_len = rlen,
		.w_buf = out,
		.r_buf = in,
		.timeout_us = PECI_RD_PKG_CONFIG_TIMEOUT_US,
	};

	out[0] = 0x00;	/* host ID */
	out[1] = index;
	out[2] = (parameter & 0xff);
	out[3] = ((parameter >> 8) & 0xff);

	rv = peci_transaction(&peci);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

int peci_Wr_Pkg_Config(uint8_t index, uint16_t parameter, uint32_t data, int wlen)
{
	int rv;
	int clen;
	uint8_t in[PECI_WR_PKG_CONFIG_READ_LENGTH] = {0};
	uint8_t out[PECI_WR_PKG_CONFIG_WRITE_LENGTH_DWORD] = {0};

	struct peci_data peci = {
		.cmd_code = PECI_CMD_WR_PKG_CFG,
		.addr = PECI_TARGET_ADDRESS,
		.w_len = wlen,
		.r_len = PECI_WR_PKG_CONFIG_READ_LENGTH,
		.w_buf = out,
		.r_buf = in,
		.timeout_us = PECI_WR_PKG_CONFIG_TIMEOUT_US,
	};

	out[0] = 0x00; /* host ID */
	out[1] = index;
	out[2] = (parameter & 0xff);
	out[3] = ((parameter >> 8) & 0xff);

	for (clen = 4; clen < wlen - 1; clen++)
		out[clen] = ((data >> ((clen - 4) * 8)) & 0xFF);


	if (peci_select_count < 10 || peci_select_flags) {
		rv = peci_transaction(&peci);

		if (rv != 0)
			peci_select_count++;
		else if (!peci_select_flags) {
			CPRINTS("FORCE GPIO PECI!");
			peci_select_count = 0;
			peci_select_flags = 1;
		}
	} else {
		rv = espi_oob_peci_transaction(&peci);
	}

	if (rv)
		return rv;

	return EC_SUCCESS;
}

static int peci_over_espi_get_cpu_temp(int *cpu_temp)
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

	if (peci_select_count < 10 || peci_select_flags) {
		rv = peci_transaction(&peci);

		if (rv != 0)
			peci_select_count++;
		else if (!peci_select_flags && peci_select_count > 3) {
			CPRINTS("FORCE GPIO PECI!");
			peci_select_count = 0;
			peci_select_flags = 1;
		}
	} else {
		rv = espi_oob_peci_transaction(&peci);

		if (rv == EC_ERROR_TIMEOUT) {
			CPRINTS("ESPI GET VALUE TIMEOUT!");
			espi_oob_retry_receive_date(r_buf);
		}
	}

	if (rv)
		return rv;

	/* Get relative raw data of temperature. */
	*cpu_temp = (r_buf[1] << 8) | r_buf[0];

	/* Convert relative raw data to degrees C. */
	*cpu_temp = ((*cpu_temp ^ 0xFFFF) + 1) >> 6;

	if (*cpu_temp >= CONFIG_PECI_TJMAX)
		return EC_ERROR_INVAL;

	/* temperature in K */
	*cpu_temp = CONFIG_PECI_TJMAX - *cpu_temp + 273;

	return EC_SUCCESS;
}

int check_system_power(void)
{
	uint8_t host_power_state = *host_get_customer_memmap(EC_EMEMAP_ER1_POWER_STATE);

	if (host_power_state & (EC_PS_ENTER_S5 | EC_PS_ENTER_S4) ||
		(!pos_get_state() && !is_non_acpi_mode()))
		return EC_ERROR_NOT_POWERED;
	else
		return EC_SUCCESS;
}

/*****************************************************************************/
/* External functions */

int peci_update_PL1(int watt)
{
	int rv;
	uint32_t data;

	if (!chipset_in_state(CHIPSET_STATE_ON) || check_system_power())
		return EC_ERROR_NOT_POWERED;

	data = PECI_PL1_CONTROL_TIME_WINDOWS | PECI_PL1_POWER_LIMIT_ENABLE |
		PECI_PL1_POWER_LIMIT(watt);

	rv = peci_Wr_Pkg_Config(PECI_INDEX_POWER_LIMITS_PL1, PECI_PARAMS_POWER_LIMITS_PL1,
		data, PECI_WR_PKG_CONFIG_WRITE_LENGTH_DWORD);

	if (rv)
		return rv;

	return EC_SUCCESS;
}

int peci_update_PL2(int watt)
{
	int rv;
	uint32_t data;

	if (!chipset_in_state(CHIPSET_STATE_ON) || check_system_power())
		return EC_ERROR_NOT_POWERED;

	data = PECI_PL2_CONTROL_TIME_WINDOWS | PECI_PL2_POWER_LIMIT_ENABLE |
		PECI_PL2_POWER_LIMIT(watt);

	rv = peci_Wr_Pkg_Config(PECI_INDEX_POWER_LIMITS_PL2, PECI_PARAMS_POWER_LIMITS_PL2,
		data, PECI_WR_PKG_CONFIG_WRITE_LENGTH_DWORD);

	if (rv)
		return rv;

	return EC_SUCCESS;
}

int peci_update_PL4(int watt)
{
	int rv;
	uint32_t data;

	if (!chipset_in_state(CHIPSET_STATE_ON) || check_system_power())
		return EC_ERROR_NOT_POWERED;

	data = PECI_PL4_POWER_LIMIT(watt);

	rv = peci_Wr_Pkg_Config(PECI_INDEX_POWER_LIMITS_PL4, PECI_PARAMS_POWER_LIMITS_PL4,
		data, PECI_WR_PKG_CONFIG_WRITE_LENGTH_DWORD);

	if (rv)
		return rv;

	return EC_SUCCESS;
}

int peci_update_PsysPL2(int watt)
{
	int rv;
	uint32_t data;

	if (!chipset_in_state(CHIPSET_STATE_ON) || check_system_power())
		return EC_ERROR_NOT_POWERED;

	data = PECI_PSYS_PL2_CONTROL_TIME_WINDOWS | PECI_PSYS_PL2_POWER_LIMIT_ENABLE |
		PECI_PSYS_PL2_POWER_LIMIT(watt);

	rv = peci_Wr_Pkg_Config(PECI_INDEX_POWER_LIMITS_PSYS_PL2,
		PECI_PARAMS_POWER_LIMITS_PSYS_PL2, data, PECI_WR_PKG_CONFIG_WRITE_LENGTH_DWORD);

	if (rv)
		return rv;

	return EC_SUCCESS;
}

__override int stop_read_peci_temp(void)
{
	static uint64_t t;
	static int read_count;
	uint64_t tnow;

	tnow = get_time().val;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) || check_system_power())
		return EC_ERROR_NOT_POWERED;
	else if (chipset_in_state(CHIPSET_STATE_STANDBY)) {
		if (tnow - t < (7 * SECOND))
			return EC_ERROR_NOT_POWERED;
		else {
			/**
			 * PECI read tempurature three times per second
			 * dptf.c thermal.c temp_sensor.c
			 */
			if (++read_count > 3) {
				read_count = 0;
				t = tnow;
				return EC_ERROR_NOT_POWERED;
			}
		}
	} else {
		read_count = 0;
		t = tnow;
	}
	
	return EC_SUCCESS;
}

int peci_over_espi_temp_sensor_get_val(int idx, int *temp_ptr)
{
	if (peci_temp == 0xfffe)
		return EC_ERROR_NOT_POWERED;

	if (peci_temp == 0xffff)
		return EC_ERROR_INVAL;

	*temp_ptr = peci_temp;

	return EC_SUCCESS;
}


void read_peci_over_espi_gettemp(void)
{
	int rv;
	int i;

	rv = stop_read_peci_temp();

	if (rv != EC_SUCCESS) {
		peci_temp = 0xfffe;
	} else {
		for (i = 0; i < 2; i++) {
			rv = peci_over_espi_get_cpu_temp(&peci_temp);
			if (!rv)
				break;
			msleep(10);
		}

		if (rv != EC_SUCCESS)
			peci_temp = 0xffff;
	}
}
DECLARE_HOOK(HOOK_SECOND, read_peci_over_espi_gettemp, HOOK_PRIO_DEFAULT);
