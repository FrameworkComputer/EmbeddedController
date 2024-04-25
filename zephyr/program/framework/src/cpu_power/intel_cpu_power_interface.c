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
#include "intel_cpu_power_interface.h"
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
	union {
		struct __ec_todo_unpacked {
			uint8_t host_id;
			uint8_t index;
			uint16_t parameter;
			uint32_t data;
			uint8_t awfcs;
		} wrpkgconfig;
	};
} __ec_align1;

/**
 * calculate the Assured Write value based on the number of bytes in input
 * buffer
 *
 * @param   data_blk_ptr
 * @param   length
 */
uint8_t calc_AWFCS(uint8_t *data_blk_ptr, unsigned int length)
{
	uint8_t crc = 0;
	uint8_t temp1, data_byte, bit0;
	unsigned int i, j;

	for (i = 0; i < length; i++) {
		data_byte = *data_blk_ptr++;

		for (j = 0; j < 8; j++) {
			bit0 = (data_byte & 0x80) ? 0x80 : 0;
			data_byte <<= 1;
			crc ^= bit0;
			temp1 = crc & 0x80;
			crc <<= 1;
			if (temp1)
				crc ^= 0x07;
		}
	}
	crc ^= 0x80;

	return crc;
}

static int system_is_ready(void)
{
	static bool pre_system_state;
	uint8_t system_flags = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS);
	static uint64_t timeout;
	uint64_t now = get_time().val;
	int rv;

	if (pre_system_state != !!(system_flags & ACPI_DRIVER_READY)) {
		if (!!(system_flags & ACPI_DRIVER_READY)) {
			/* Delay 10 seconds to start PECI communication */
			timeout = get_time().val + 10 * SECOND;
		}
		pre_system_state = !!(system_flags & ACPI_DRIVER_READY);
	}

	if (!!(system_flags & ACPI_DRIVER_READY) && !chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		if (now >= timeout) {
			rv = 1;
		} else {
			rv = 0;
		}
	} else {
		rv = 0;
	}

	return rv;
}


__override int stop_read_peci_temp(void)
{
	static uint64_t t;
	uint64_t tnow;

	tnow = get_time().val;

	if (!system_is_ready())
		return EC_ERROR_NOT_POWERED;
	else if (chipset_in_state(CHIPSET_STATE_STANDBY)) {
		if (tnow - t < (7 * SECOND))
			return EC_ERROR_NOT_POWERED;
	}

	t = tnow;
	return EC_SUCCESS;
}

static int request_temp(void)
{
	struct peci_over_espi_buffer oob_buff = {0};
	struct espi_oob_packet req_pckt;
	int ret;

	oob_buff.dest_slave_addr = ESPI_OOB_SMB_SLAVE_DEST_ADDR_PMC_FW;
	oob_buff.oob_cmd_code = ESPI_OOB_PECI_CMD;
	oob_buff.byte_cnt = PECI_GET_TEMP_WR_LEN + 4;
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

int request_wrpkgconfig(int index, int param, uint32_t data)
{
	struct peci_over_espi_buffer oob_buff = {0};
	struct espi_oob_packet req_pckt;
	uint8_t peci_wrpkg[13] = {0};
	int ret;

	oob_buff.dest_slave_addr = ESPI_OOB_SMB_SLAVE_DEST_ADDR_PMC_FW;
	oob_buff.oob_cmd_code = ESPI_OOB_PECI_CMD;
	oob_buff.byte_cnt = PECI_WR_PKG_LEN_DWORD + 4;
	oob_buff.src_slave_addr = ESPI_OOB_SMB_SLAVE_SRC_ADDR_EC;
	oob_buff.addr = PECI_HOST_ADDR;
	oob_buff.cmd_code = PECI_CMD_WR_PKG_CFG0;
	oob_buff.tx_size = PECI_WR_PKG_LEN_DWORD;
	oob_buff.rx_size = PECI_WR_PKG_RD_LEN;

	oob_buff.wrpkgconfig.host_id = 0x00;
	oob_buff.wrpkgconfig.index = index;
	oob_buff.wrpkgconfig.parameter = param;
	oob_buff.wrpkgconfig.data = data;

	memcpy(peci_wrpkg, &oob_buff.addr, 13);

	oob_buff.wrpkgconfig.awfcs = calc_AWFCS(peci_wrpkg, sizeof(peci_wrpkg) - 1);

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

/* PECI GET TEMP */
static int peci_get_cpu_temp(int *cpu_temp)
{
	int ret;
	uint8_t get_temp_buf[7];

	if (!system_is_ready())
		return EC_ERROR_NOT_POWERED;

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
	int rv;
	int soc_temp = C_TO_K(0);

	rv = stop_read_peci_temp();

	if (rv == EC_ERROR_NOT_POWERED) {
		soc_temp = 0xfffe;
	} else {
		rv = peci_get_cpu_temp(&soc_temp);

		if (rv != EC_SUCCESS)
			soc_temp = 0xffff;
	}

	temps = soc_temp;
}

int peci_temp_sensor_get_val(int idx, int *temp_ptr)
{
	if (temps == 0xfffe)
		return EC_ERROR_NOT_POWERED;

	if (temps == 0xffff)
		return EC_ERROR_INVAL;

	*temp_ptr = temps;

	return EC_SUCCESS;
}

/* PECI update power limit */

int pl1_watt;
int pl2_watt;
int pl4_watt;
bool manual_ctl;

static int peci_update_power_limit_1(int watt)
{
	int ret;
	uint8_t read_buf[6];
	uint32_t data;

	if (!system_is_ready())
		return EC_ERROR_NOT_POWERED;

	data = PECI_PL1_CONTROL_TIME_WINDOWS(TIME_WINDOW_PL1) | PECI_PL1_POWER_LIMIT_ENABLE(1) |
		PECI_PL1_POWER_LIMIT(watt);

	ret = request_wrpkgconfig(PECI_INDEX_POWER_LIMITS_PL1, PECI_PARAMS_POWER_LIMITS_PL1,
		data);

	if (ret) {
		CPRINTS("OOB req failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	ret = retrieve_packet(read_buf);
	if (ret) {
		CPRINTS("OOB retrieve failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	if (read_buf[5] != 0x40) {
		CPRINTS("pl1 update fail, CC:0x%02x", read_buf[5]);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int peci_update_power_limit_2(int watt)
{
	int ret;
	uint8_t read_buf[6];
	uint32_t data;

	if (!system_is_ready())
		return EC_ERROR_NOT_POWERED;

	data = PECI_PL2_CONTROL_TIME_WINDOWS(TIME_WINDOW_PL2) | PECI_PL2_POWER_LIMIT_ENABLE(1) |
		PECI_PL2_POWER_LIMIT(watt);

	ret = request_wrpkgconfig(PECI_INDEX_POWER_LIMITS_PL2, PECI_PARAMS_POWER_LIMITS_PL2,
		data);

	if (ret) {
		CPRINTS("OOB req failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	ret = retrieve_packet(read_buf);
	if (ret) {
		CPRINTS("OOB retrieve failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	if (read_buf[5] != 0x40) {
		CPRINTS("pl2 update fail, CC:0x%02x", read_buf[5]);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

__maybe_unused static int peci_update_power_limit_3(int watt)
{
	int ret;
	uint8_t read_buf[6];
	uint32_t data;

	if (!system_is_ready())
		return EC_ERROR_NOT_POWERED;

	data = PECI_PL3_CONTROL_DUTY(DUTY_CYCLE_PL3) |
		PECI_PL3_CONTROL_TIME_WINDOWS(TIME_WINDOW_PL3) |
		PECI_PL3_POWER_LIMIT_ENABLE(1) |
		PECI_PL3_POWER_LIMIT(watt);

	ret = request_wrpkgconfig(PECI_INDEX_POWER_LIMITS_PL3, PECI_PARAMS_POWER_LIMITS_PL3,
		data);

	if (ret) {
		CPRINTS("OOB req failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	ret = retrieve_packet(read_buf);
	if (ret) {
		CPRINTS("OOB retrieve failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	if (read_buf[5] != 0x40) {
		CPRINTS("pl3 update fail, CC:0x%02x", read_buf[5]);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int peci_update_power_limit_4(int watt)
{
	int ret;
	uint8_t read_buf[6];
	uint32_t data;

	if (!system_is_ready())
		return EC_ERROR_NOT_POWERED;

	data = PECI_PL4_POWER_LIMIT(watt);

	ret = request_wrpkgconfig(PECI_INDEX_POWER_LIMITS_PL4, PECI_PARAMS_POWER_LIMITS_PL4,
		data);

	if (ret) {
		CPRINTS("OOB req failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	ret = retrieve_packet(read_buf);
	if (ret) {
		CPRINTS("OOB retrieve failed %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	if (read_buf[5] != 0x40) {
		CPRINTS("pl4 update fail, CC:0x%02x", read_buf[5]);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

int set_pl_limits(int pl1, int pl2, int pl4)
{
	RETURN_ERROR(peci_update_power_limit_1(pl1));
	RETURN_ERROR(peci_update_power_limit_2(pl2));
	RETURN_ERROR(peci_update_power_limit_4(pl4));

	return EC_SUCCESS;
}

void update_soc_power_limit_hook(void)
{
	if (!manual_ctl)
		update_soc_power_limit(false, false);
}

DECLARE_HOOK(HOOK_SECOND, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);

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
	uint32_t pl1, pl2, pl4;
	char *e;

	if (argc >= 2) {
		if (!strncmp(argv[1], "auto", 4)) {
			manual_ctl = false;
			CPRINTS("Auto update ");
			update_soc_power_limit(true, false);
		}
	}

	if (argc >= 4) {
		pl1 = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		pl2 = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;
		pl4 = strtoi(argv[4], &e, 0);
		if (*e)
			return EC_ERROR_PARAM4;

		manual_ctl = true;
		CPRINTF("Manual update ");

		pl1_watt = pl1;
		pl2_watt = pl2;
		pl4_watt = pl4;
		set_pl_limits(pl1_watt, pl2_watt, pl4_watt);

	}

	CPRINTS("Power Limit: PL1 %dW, PL2 %dW, PL4 %dW",
		pl1_watt, pl2_watt, pl4_watt);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cpupower, cmd_cpupower,
			"cpupower pl1 pl2 pl4 ",
			"Set/Get the cpupower limit");
