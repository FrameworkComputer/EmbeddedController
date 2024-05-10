/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * EC-EC communication, functions and definitions for client.
 */

#include "battery.h"
#include "common.h"
#include "console.h"
#include "crc8.h"
#include "ec_commands.h"
#include "ec_ec_comm_client.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

/*
 * TODO(b:65697962): The packed structures below do not play well if we force EC
 * host commands structures to be aligned on 32-bit boundary. There are ways to
 * fix that, possibly requiring copying data around, or modifying
 * uart_alt_pad_write_read API to write the actual server response to a separate
 * buffer.
 */
#ifdef CONFIG_HOSTCMD_ALIGNED
#error "Cannot define CONFIG_HOSTCMD_ALIGNED with EC-EC communication client."
#endif

#define EC_EC_HOSTCMD_VERSION 4

/* Print extra debugging information */
#undef EXTRA_DEBUG

/*
 * During early debugging, we would like to check that the error rate does
 * grow out of control.
 */
#define DEBUG_EC_COMM_STATS
#ifdef DEBUG_EC_COMM_STATS
struct {
	int total;
	int errtimeout;
	int errbusy;
	int errunknown;
	int errdatacrc;
	int errcrc;
	int errinval;
} comm_stats;

#define INCR_COMM_STATS(var) (comm_stats.var++)
#else
#define INCR_COMM_STATS(var)
#endif

/**
 * Write a command on the EC-EC communication UART channel.
 *
 * @param command	One of EC_CMD_*.
 * @param data		Packed structure with this layout:
 * struct {
 *	struct {
 *		struct ec_host_request4 head;
 *		struct ec_params_* param;
 *		uint8_t crc8;
 *	} req;
 *	struct {
 *		struct ec_host_response4 head;
 *		struct ec_response_* info;
 *		uint8_t crc8;
 *	} resp;
 * } __packed data;
 *
 * Where req is the request to be transmitted (head and crc8 are computed by
 * this function), and resp is the response to be received (head integrity and
 * crc8 are verified by this function).
 *
 * This format is required as the EC-EC UART is half-duplex, and all the
 * transmitted data is received back, i.e. the client writes req, then reads
 * req, followed by resp.
 *
 * When a command does not take parameters, param/crc8 must be omitted in
 * tx structure. The same applies to rx structure if the response does not
 * include a payload: info/crc8 must be omitted.
 *
 * @param req_len	size of req.param (0 if no parameter is passed).
 * @param resp_len	size of resp.info (0 if no information is returned).
 * @param timeout_us	timeout in microseconds for the transaction to complete.
 *
 * @return
 *  - EC_SUCCESS on success.
 *  - EC_ERROR_TIMEOUT when remote end times out replying.
 *  - EC_ERROR_BUSY when UART is busy and cannot transmit currently.
 *  - EC_ERROR_CRC when the header or data CRC is invalid.
 *  - EC_ERROR_INVAL when the received header is invalid.
 *  - EC_ERROR_UNKNOWN on other error.
 */
static int write_command(uint16_t command, uint8_t *data, int req_len,
			 int resp_len, int timeout_us)
{
	/* Sequence number. */
	static uint8_t cur_seq;
	int ret;
	int hascrc, response_seq;

	struct ec_host_request4 *request_header = (void *)data;
	/* Request (TX) length is header + (data + crc8), response follows. */
	int tx_length =
		sizeof(*request_header) + ((req_len > 0) ? (req_len + 1) : 0);

	struct ec_host_response4 *response_header = (void *)&data[tx_length];
	/* RX length is TX length + response from server. */
	int rx_length = tx_length + sizeof(*request_header) +
			((resp_len > 0) ? (resp_len + 1) : 0);

	/*
	 * Make sure there is a gap between each command, so that the server
	 * can recover its state machine after each command.
	 *
	 * TODO(b:65697962): We can be much smarter than this, and record the
	 * last transaction time instead of just sleeping blindly.
	 */
	crec_usleep(10 * MSEC);

#ifdef DEBUG_EC_COMM_STATS
	if ((comm_stats.total % 128) == 0) {
		CPRINTF("UART %d (T%dB%d,U%dC%dD%dI%d)\n", comm_stats.total,
			comm_stats.errtimeout, comm_stats.errbusy,
			comm_stats.errunknown, comm_stats.errcrc,
			comm_stats.errdatacrc, comm_stats.errinval);
	}
#endif

	cur_seq = (cur_seq + 1) &
		  (EC_PACKET4_0_SEQ_NUM_MASK >> EC_PACKET4_0_SEQ_NUM_SHIFT);

	memset(request_header, 0, sizeof(*request_header));
	/* fields0: leave seq_dup and is_response as 0. */
	request_header->fields0 =
		EC_EC_HOSTCMD_VERSION | /* version */
		(cur_seq << EC_PACKET4_0_SEQ_NUM_SHIFT); /* seq_num
							  */
	/* fields1: leave command_version as 0. */
	if (req_len > 0)
		request_header->fields1 |= EC_PACKET4_1_DATA_CRC_PRESENT_MASK;
	request_header->command = command;
	request_header->data_len = req_len;
	request_header->header_crc = cros_crc8((uint8_t *)request_header,
					       sizeof(*request_header) - 1);
	if (req_len > 0)
		data[sizeof(*request_header) + req_len] =
			cros_crc8(&data[sizeof(*request_header)], req_len);

	ret = uart_alt_pad_write_read((void *)data, tx_length, (void *)data,
				      rx_length, timeout_us);

	INCR_COMM_STATS(total);

#ifdef EXTRA_DEBUG
	CPRINTF("EC-EC ret=%d/%d\n", ret, rx_length);
#endif

	if (ret != rx_length) {
		if (ret == -EC_ERROR_TIMEOUT) {
			INCR_COMM_STATS(errtimeout);
			return EC_ERROR_TIMEOUT;
		}

		if (ret == -EC_ERROR_BUSY) {
			INCR_COMM_STATS(errbusy);
			return EC_ERROR_BUSY;
		}

		INCR_COMM_STATS(errunknown);
		return EC_ERROR_UNKNOWN;
	}

	if (response_header->header_crc !=
	    cros_crc8((uint8_t *)response_header,
		      sizeof(*response_header) - 1)) {
		INCR_COMM_STATS(errcrc);
		return EC_ERROR_CRC;
	}

	hascrc = response_header->fields1 & EC_PACKET4_1_DATA_CRC_PRESENT_MASK;
	response_seq = (response_header->fields0 & EC_PACKET4_0_SEQ_NUM_MASK) >>
		       EC_PACKET4_0_SEQ_NUM_SHIFT;

	/*
	 * Validate received header.
	 * Note that we _require_ data crc to be present if there is data to be
	 * read back, else we would not know how many bytes to read exactly.
	 */
	if ((response_header->fields0 & EC_PACKET4_0_STRUCT_VERSION_MASK) !=
		    EC_EC_HOSTCMD_VERSION ||
	    !(response_header->fields0 & EC_PACKET4_0_IS_RESPONSE_MASK) ||
	    response_seq != cur_seq ||
	    (response_header->data_len > 0 && !hascrc) ||
	    response_header->data_len != resp_len) {
		INCR_COMM_STATS(errinval);
		return EC_ERROR_INVAL;
	}

	/* Check data CRC. */
	if (hascrc &&
	    data[rx_length - 1] !=
		    cros_crc8(&data[tx_length + sizeof(*request_header)],
			      resp_len)) {
		INCR_COMM_STATS(errdatacrc);
		return EC_ERROR_CRC;
	}

	return EC_SUCCESS;
}

/**
 * handle error from write_command
 *
 * @param ret is return value from write_command
 * @param request_result is data.resp.head.result (response result value)
 *
 * @return EC_RES_ERROR if ret is not EC_SUCCESS, else request_result.
 */
static int handle_error(const char *func, int ret, int request_result)
{
	if (ret != EC_SUCCESS) {
		/* Do not print busy errors as they just spam the console. */
		if (ret != EC_ERROR_BUSY)
			CPRINTF("%s: tx error %d\n", func, ret);
		return EC_RES_ERROR;
	}

	if (request_result != EC_RES_SUCCESS)
		CPRINTF("%s: cmd error %d\n", func, ret);

	return request_result;
}

#ifdef CONFIG_EC_EC_COMM_BATTERY
int ec_ec_client_base_get_dynamic_info(void)
{
	int ret;
	struct {
		struct {
			struct ec_host_request4 head;
			struct ec_params_battery_dynamic_info param;
			uint8_t crc8;
		} req;
		struct {
			struct ec_host_response4 head;
			struct ec_response_battery_dynamic_info info;
			uint8_t crc8;
		} resp;
	} __packed data;

	data.req.param.index = 0;

	ret = write_command(EC_CMD_BATTERY_GET_DYNAMIC, (void *)&data,
			    sizeof(data.req.param), sizeof(data.resp.info),
			    15 * MSEC);
	ret = handle_error(__func__, ret, data.resp.head.result);
	if (ret != EC_RES_SUCCESS)
		return ret;

#ifdef EXTRA_DEBUG
	CPRINTF("V:          %d mV\n", data.resp.info.actual_voltage);
	CPRINTF("I:          %d mA\n", data.resp.info.actual_current);
	CPRINTF("Remaining:  %d mAh\n", data.resp.info.remaining_capacity);
	CPRINTF("Cap-full:   %d mAh\n", data.resp.info.full_capacity);
	CPRINTF("Flags:      %04x\n", data.resp.info.flags);
	CPRINTF("V-desired:  %d mV\n", data.resp.info.desired_voltage);
	CPRINTF("I-desired:  %d mA\n", data.resp.info.desired_current);
#endif

	memcpy(&battery_dynamic[BATT_IDX_BASE], &data.resp.info,
	       sizeof(battery_dynamic[BATT_IDX_BASE]));
	return EC_RES_SUCCESS;
}

int ec_ec_client_base_get_static_info(void)
{
	int ret;
	struct {
		struct {
			struct ec_host_request4 head;
			struct ec_params_battery_static_info param;
			uint8_t crc8;
		} req;
		struct {
			struct ec_host_response4 head;
			struct ec_response_battery_static_info info;
			uint8_t crc8;
		} resp;
	} __packed data;
	const struct ec_response_battery_static_info *info = &data.resp.info;
	struct battery_static_info *bs = &battery_static[BATT_IDX_BASE];

	data.req.param.index = 0;

	ret = write_command(EC_CMD_BATTERY_GET_STATIC, (void *)&data,
			    sizeof(data.req.param), sizeof(data.resp.info),
			    15 * MSEC);
	ret = handle_error(__func__, ret, data.resp.head.result);
	if (ret != EC_RES_SUCCESS)
		return ret;

#ifdef EXTRA_DEBUG
	CPRINTF("Cap-design: %d mAh\n", data.resp.info.design_capacity);
	CPRINTF("V-design:   %d mV\n", data.resp.info.design_voltage);
	CPRINTF("Manuf:      %s\n", data.resp.info.manufacturer);
	CPRINTF("Model:      %s\n", data.resp.info.model);
	CPRINTF("Serial:     %s\n", data.resp.info.serial);
	CPRINTF("Type:       %s\n", data.resp.info.type);
	CPRINTF("C-count:    %d\n", data.resp.info.cycle_count);
#endif

	bs->design_capacity = info->design_capacity;
	bs->design_voltage = info->design_voltage;
	bs->cycle_count = info->cycle_count;
	strncpy(bs->manufacturer_ext, info->manufacturer,
		sizeof(bs->manufacturer_ext));
	strncpy(bs->model_ext, info->model, sizeof(bs->model_ext));
	strncpy(bs->serial_ext, info->serial, sizeof(bs->serial_ext));
	strncpy(bs->type_ext, info->type, sizeof(bs->type_ext));

	return EC_RES_SUCCESS;
}

int ec_ec_client_base_charge_control(int max_current, int otg_voltage,
				     int allow_charging)
{
	int ret;
	struct {
		struct {
			struct ec_host_request4 head;
			struct ec_params_charger_control ctrl;
			uint8_t crc8;
		} req;
		struct {
			struct ec_host_response4 head;
		} resp;
	} __packed data;

	data.req.ctrl.allow_charging = allow_charging;
	data.req.ctrl.max_current = max_current;
	data.req.ctrl.otg_voltage = otg_voltage;

	ret = write_command(EC_CMD_CHARGER_CONTROL, (void *)&data,
			    sizeof(data.req.ctrl), 0, 30 * MSEC);

	return handle_error(__func__, ret, data.resp.head.result);
}

int ec_ec_client_hibernate(void)
{
	int ret;
	struct {
		struct {
			struct ec_host_request4 head;
			struct ec_params_reboot_ec param;
		} req;
		struct {
			struct ec_host_response4 head;
		} resp;
	} __packed data;

	data.req.param.cmd = EC_REBOOT_HIBERNATE;
	data.req.param.flags = 0;

	ret = write_command(EC_CMD_REBOOT_EC, (void *)&data,
			    sizeof(data.req.param), 0, 30 * MSEC);

	return handle_error(__func__, ret, data.resp.head.result);
}
#endif /* CONFIG_EC_EC_COMM_BATTERY */
