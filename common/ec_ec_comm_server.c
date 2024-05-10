/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * EC-EC communication, task and functions for server.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "crc8.h"
#include "ec_commands.h"
#include "ec_ec_comm_server.h"
#include "extpower.h"
#include "hooks.h"
#include "hwtimer.h"
#include "queue.h"
#include "queue_policies.h"
#include "system.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Print extra debugging information */
#undef EXTRA_DEBUG

/* Set if the client allows the server to charge the battery. */
static int charging_allowed;

/*
 * Our command parameter buffer must be big enough to fit any command
 * parameter, and crc byte.
 */
#define LARGEST_PARAMS_SIZE 8

BUILD_ASSERT(LARGEST_PARAMS_SIZE >=
	     sizeof(struct ec_params_battery_static_info));
BUILD_ASSERT(LARGEST_PARAMS_SIZE >=
	     sizeof(struct ec_params_battery_dynamic_info));
BUILD_ASSERT(LARGEST_PARAMS_SIZE >= sizeof(struct ec_params_charger_control));

#define COMMAND_BUFFER_PARAMS_SIZE (LARGEST_PARAMS_SIZE + 1)

/*
 * Maximum time needed to read a full command, commands are at most 17 bytes, so
 * should not take more than 2ms to be sent at 115200 bps.
 */
#define COMMAND_TIMEOUT_US (5 * MSEC)

void ec_ec_comm_server_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_ECCOMM);
}

/*
 * Discard all data from the input queue.
 *
 * Note that we always sleep for 1ms after clearing the queue, to make sure
 * that we give enough time for the next byte to arrive.
 */
static void discard_queue(void)
{
	do {
		queue_advance_head(&ec_ec_comm_server_input,
				   queue_count(&ec_ec_comm_server_input));
		crec_usleep(1 * MSEC);
	} while (queue_count(&ec_ec_comm_server_input) > 0);
}

/* Write response to client. */
static void write_response(uint16_t res, int seq, const void *data, int len)
{
	struct ec_host_response4 header;
	uint8_t crc;

	header.fields0 = 4 | /* version */
			 EC_PACKET4_0_IS_RESPONSE_MASK | /* is_response */
			 (seq << EC_PACKET4_0_SEQ_NUM_SHIFT); /* seq_num */
	/* Set data_crc_present if there is data */
	header.fields1 = (len > 0) ? EC_PACKET4_1_DATA_CRC_PRESENT_MASK : 0;
	header.result = res;
	header.data_len = len;
	header.reserved = 0;
	header.header_crc = cros_crc8((uint8_t *)&header, sizeof(header) - 1);
	QUEUE_ADD_UNITS(&ec_ec_comm_server_output, (uint8_t *)&header,
			sizeof(header));

	if (len > 0) {
		QUEUE_ADD_UNITS(&ec_ec_comm_server_output, data, len);
		crc = cros_crc8(data, len);
		QUEUE_ADD_UNITS(&ec_ec_comm_server_output, &crc, sizeof(crc));
	}
}

/*
 * Read len bytes into buffer. Waiting up to COMMAND_TIMEOUT_US after start.
 *
 * Returns EC_SUCCESS or EC_ERROR_TIMEOUT.
 */
static int read_data(void *buffer, size_t len, uint32_t start)
{
	uint32_t delta;

	while (queue_count(&ec_ec_comm_server_input) < len) {
		delta = __hw_clock_source_read() - start;
		if (delta >= COMMAND_TIMEOUT_US)
			return EC_ERROR_TIMEOUT;

		/* Every incoming byte wakes the task. */
		task_wait_event(COMMAND_TIMEOUT_US - delta);
	}

	/* Fetch header */
	QUEUE_REMOVE_UNITS(&ec_ec_comm_server_input, buffer, len);

	return EC_SUCCESS;
}

static void handle_cmd_reboot_ec(const struct ec_params_reboot_ec *params,
				 int data_len, int seq)
{
	int ret = EC_RES_SUCCESS;

	if (data_len != sizeof(*params)) {
		ret = EC_RES_INVALID_COMMAND;
		goto out;
	}

	/* Only handle hibernate */
	if (params->cmd != EC_REBOOT_HIBERNATE) {
		ret = EC_RES_INVALID_PARAM;
		goto out;
	}

	CPRINTS("Hibernating...");

	system_hibernate(0, 0);
	/* We should not be able to write back the response. */

out:
	write_response(ret, seq, NULL, 0);
}

#ifdef CONFIG_EC_EC_COMM_BATTERY
static void
handle_cmd_charger_control(const struct ec_params_charger_control *params,
			   int data_len, int seq)
{
	int ret = EC_RES_SUCCESS;
	int prev_charging_allowed = charging_allowed;

	if (data_len != sizeof(*params)) {
		ret = EC_RES_INVALID_COMMAND;
		goto out;
	}

	if (params->max_current >= 0) {
		charge_set_output_current_limit(CHARGER_SOLO, 0, 0);
		charge_set_input_current_limit(
			MIN(MAX_CURRENT_MA, params->max_current), 0);
		charging_allowed = params->allow_charging;
	} else {
		if (-params->max_current > MAX_OTG_CURRENT_MA ||
		    params->otg_voltage > MAX_OTG_VOLTAGE_MV) {
			ret = EC_RES_INVALID_PARAM;
			goto out;
		}

		/* Reset input current to default. */
		charge_set_input_current_limit(
			CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT, 0);
		/* Setup and enable "OTG". */
		charge_set_output_current_limit(CHARGER_SOLO,
						-params->max_current,
						params->otg_voltage);
		charging_allowed = 0;
	}

	if (prev_charging_allowed != charging_allowed)
		hook_notify(HOOK_AC_CHANGE);

out:
	write_response(ret, seq, NULL, 0);
}

/*
 * On dual-battery server, we use the charging allowed signal from client to
 * indicate whether external power is present.
 *
 * In most cases, this actually matches the external power status of the client
 * (server battery charging when AC is connected, or discharging when server
 * battery still has enough capacity), with one exception: when we do client to
 * server battery charging (in this case the "external" power is the client).
 */
int extpower_is_present(void)
{
	return charging_allowed;
}
#endif

void ec_ec_comm_server_task(void *u)
{
	struct ec_host_request4 header;
	/*
	 * If CONFIG_HOSTCMD_ALIGNED is set, it is important that params is
	 * aligned on a 32-bit boundary.
	 */
	uint8_t __aligned(4) params[COMMAND_BUFFER_PARAMS_SIZE];
	unsigned int len, seq = 0, hascrc, cmdver;
	uint32_t start;

	while (1) {
		task_wait_event(-1);

		if (queue_count(&ec_ec_comm_server_input) == 0)
			continue;

		/* We got some data, start timeout counter. */
		start = __hw_clock_source_read();

		/* Wait for whole header to be available and read it. */
		if (read_data(&header, sizeof(header), start)) {
			CPRINTS("%s timeout (header)", __func__);
			goto discard;
		}

#ifdef EXTRA_DEBUG
		CPRINTS("%s f0=%02x f1=%02x cmd=%02x, length=%d", __func__,
			header.fields0, header.fields1, header.command,
			header.data_len);
#endif

		/* Ignore response (we wrote that ourselves) */
		if (header.fields0 & EC_PACKET4_0_IS_RESPONSE_MASK)
			goto discard;

		/* Validate version and crc. */
		if ((header.fields0 & EC_PACKET4_0_STRUCT_VERSION_MASK) != 4 ||
		    header.header_crc !=
			    cros_crc8((uint8_t *)&header, sizeof(header) - 1)) {
			CPRINTS("%s header/crc error", __func__);
			goto discard;
		}

		len = header.data_len;
		hascrc = header.fields1 & EC_PACKET4_1_DATA_CRC_PRESENT_MASK;
		if (hascrc)
			len += 1;

		/*
		 * Ignore commands that are too long to fit in our buffer.
		 */
		if (len > sizeof(params)) {
			CPRINTS("%s len error (%d)", __func__, len);
			/* Discard the data first, then write error back. */
			discard_queue();
			write_response(EC_RES_OVERFLOW, seq, NULL, 0);
			goto discard;
		}

		seq = (header.fields0 & EC_PACKET4_0_SEQ_NUM_MASK) >>
		      EC_PACKET4_0_SEQ_NUM_SHIFT;

		cmdver = header.fields1 & EC_PACKET4_1_COMMAND_VERSION_MASK;

		/* Wait for the rest of the data to be available and read it. */
		if (read_data(params, len, start)) {
			CPRINTS("%s timeout (data)", __func__);
			goto discard;
		}

		/* Check data CRC */
		if (hascrc && params[len - 1] != cros_crc8(params, len - 1)) {
			CPRINTS("%s data crc error", __func__);
			write_response(EC_RES_INVALID_CHECKSUM, seq, NULL, 0);
			goto discard;
		}

		/* For now, all commands have version 0. */
		if (cmdver != 0) {
			CPRINTS("%s bad command version", __func__);
			write_response(EC_RES_INVALID_VERSION, seq, NULL, 0);
			continue;
		}

		switch (header.command) {
#ifdef CONFIG_EC_EC_COMM_BATTERY
		case EC_CMD_BATTERY_GET_STATIC:
			/* Note that we ignore the battery index parameter. */
			write_response(EC_RES_SUCCESS, seq,
				       &battery_static[BATT_IDX_MAIN],
				       sizeof(battery_static[BATT_IDX_MAIN]));
			break;
		case EC_CMD_BATTERY_GET_DYNAMIC:
			/* Note that we ignore the battery index parameter. */
			write_response(EC_RES_SUCCESS, seq,
				       &battery_dynamic[BATT_IDX_MAIN],
				       sizeof(battery_dynamic[BATT_IDX_MAIN]));
			break;
		case EC_CMD_CHARGER_CONTROL:
			handle_cmd_charger_control((void *)params,
						   header.data_len, seq);
			break;
#endif
		case EC_CMD_REBOOT_EC:
			handle_cmd_reboot_ec((void *)params, header.data_len,
					     seq);
			break;
		default:
			write_response(EC_RES_INVALID_COMMAND, seq, NULL, 0);
		}

		continue;
	discard:
		/*
		 * Some error occurred: discard all data in the queue.
		 */
		discard_queue();
	}
}
