/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "math_util.h"
#include "util.h"

#include <zephyr/drivers/peci.h>
#include <zephyr/drivers/espi.h>

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ## args)

#define PECI_HOST_ADDR		0x30	/* PECI Host address */
#define CONFIG_PECI_TJMAX	110	/* PECI TJMAX value */

#define ESPI_OOB_SMB_SLAVE_SRC_ADDR_EC 0x0F
#define ESPI_OOB_SMB_SLAVE_DEST_ADDR_PMC_FW 0x20
#define ESPI_OOB_PECI_CMD 0x01
#define MAX_ESPI_BUF_LEN 80

static const struct device *const espi_dev = DEVICE_DT_GET(DT_NODELABEL(espi0));

static bool espi_verbose;
static int temps;


struct peci_over_espi_buffer {
	uint8_t dest_slave_addr;
	uint8_t oob_cmd_code;
	uint8_t byte_cnt;
	uint8_t src_slave_addr;
	/* PECI data format */
	uint8_t addr;
	uint8_t tx_size;
	uint8_t rx_size;
	enum peci_command_code cmd_code;
} __ec_align1;

static int request_temp(void)
{
	struct peci_over_espi_buffer oob_buff = {0};
	struct espi_oob_packet req_pckt;
	int ret;

	oob_buff.dest_slave_addr = ESPI_OOB_SMB_SLAVE_DEST_ADDR_PMC_FW;
	oob_buff.oob_cmd_code = ESPI_OOB_PECI_CMD;
	oob_buff.byte_cnt = 5;
	oob_buff.src_slave_addr = ESPI_OOB_SMB_SLAVE_SRC_ADDR_EC;
	oob_buff.addr = PECI_HOST_ADDR;
	oob_buff.cmd_code = PECI_CMD_GET_TEMP0;
	oob_buff.tx_size = PECI_GET_TEMP_WR_LEN;
	oob_buff.rx_size = PECI_GET_TEMP_RD_LEN;

	/* Packetize OOB request */
	req_pckt.buf = (uint8_t *)&oob_buff;
	req_pckt.len = sizeof(struct peci_over_espi_buffer);

	ret = espi_send_oob(espi_dev, &req_pckt);

	if (ret) {
		CPRINTS("OOB Tx failed %d", ret);
		return ret;
	}

	return EC_SUCCESS;
}

static int retrieve_packet(uint8_t *buf)
{
	int ret;
	struct espi_oob_packet resp_pckt;

	resp_pckt.buf = buf;
	resp_pckt.len = MAX_ESPI_BUF_LEN;

	ret = espi_receive_oob(espi_dev, &resp_pckt);

	if (ret) {
		CPRINTS("OOB Rx failed %d", ret);
		return ret;
	}

	if (espi_verbose) {
		CPRINTS("OOB transaction completed rcvd: %d bytes", resp_pckt.len);
		for (int i = 0; i < resp_pckt.len; i++) {
			CPRINTS("data[%d]: 0x%02x", i, buf[i]);
		}
	}

	return EC_SUCCESS;
}

static int peci_get_cpu_temp(int *cpu_temp)
{
	int ret;
	uint8_t get_temp_buf[7];

	ret = request_temp();
	if (ret) {
		CPRINTS("OOB req failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	ret = retrieve_packet(get_temp_buf);
	if (ret) {
		CPRINTS("OOB retrieve failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	/* Get relative raw data of temperature. */
	*cpu_temp = (get_temp_buf[6] << 8) | get_temp_buf[5];

	/* 2's complement convert relative raw data to degrees C. */
	*cpu_temp = ((*cpu_temp ^ 0xFFFF) + 1) >> 6;

	/* TODO: calculate the fractional value (PECI spec figure 5.1)*/

	if (*cpu_temp >= CONFIG_PECI_TJMAX)
		return EC_ERROR_UNKNOWN;

	*cpu_temp = CONFIG_PECI_TJMAX - *cpu_temp + 273;

	return EC_SUCCESS;
}

void soc_update_temperature(int idx)
{
	int i, rv;
	int soc_temp = C_TO_K(0);

	for (i = 0; i < 2; i++) {
		rv = peci_get_cpu_temp(&soc_temp);
		if (!rv)
			break;
	}

	temps = soc_temp;
}

int peci_temp_sensor_get_val(int idx, int *temp_ptr)
{
	*temp_ptr = temps;
	return EC_SUCCESS;
}

/* */

int pl1_watt;
int pl2_watt;
int pl4_watt;
int psys_watt;
bool manual_ctl;


/* TODO implement peci interface*/
void set_pl_limits(int pl1, int pl2, int pl4, int psys)
{
	/* peci_update_PL1(pl1); */
	/* peci_update_PL2(pl2); */
	/* peci_update_PL4(pl4); */
	/* peci_update_PsysPL2(psys); */
}

void update_soc_power_limit_hook(void)
{
	if (!manual_ctl)
		update_soc_power_limit(false, false);
}

DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);

void update_soc_power_on_boot_deferred(void)
{
	if (!manual_ctl)
		update_soc_power_limit(true, false);
}
DECLARE_DEFERRED(update_soc_power_on_boot_deferred);

void update_soc_power_limit_boot(void)
{
	hook_call_deferred(&update_soc_power_on_boot_deferred_data, MSEC*1000);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, update_soc_power_limit_boot, HOOK_PRIO_DEFAULT);

static int cmd_cpupower(int argc, const char **argv)
{
	uint32_t pl1, pl2, pl4, psys;
	char *e;

	CPRINTF("SOC Power Limit: PL1 %d, PL2 %d, PL4 %d, Psys %d\n",
		pl1_watt, pl2_watt, pl4_watt, psys_watt);
	if (argc >= 2) {
		if (!strncmp(argv[1], "auto", 4)) {
			manual_ctl = false;
			CPRINTF("Auto Control");
			update_soc_power_limit(false, false);
		}
		if (!strncmp(argv[1], "manual", 6)) {
			manual_ctl = true;
			CPRINTF("Manual Control");
			set_pl_limits(pl1_watt, pl2_watt, pl4_watt, psys_watt);
		}
	}

	if (argc >= 5) {
		pl1 = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		pl2 = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		pl4 = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;
		psys = strtoi(argv[4], &e, 0);
		if (*e)
			return EC_ERROR_PARAM4;
		pl1_watt = pl1;
		pl2_watt = pl2;
		pl4_watt = pl4;
		psys_watt = psys;
		set_pl_limits(pl1_watt, pl2_watt, pl4_watt, psys_watt);

	}
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(cpupower, cmd_cpupower,
			"cpupower pl1 pl2 pl4 psys ",
			"Set/Get the cpupower limit");
