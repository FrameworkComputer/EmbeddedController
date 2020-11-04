/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "peripheral_charger.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/*
 * Configuration
 */

/*
 * When ctn730 is asleep, I2C is ignored but can wake it up. I2C will be resent
 * after this delay.
 */
static const int _wake_up_delay_ms = 10;

/* Device detection interval */
static const int _detection_interval_ms = 100;

/* Buffer size for i2c read & write */
#define CTN730_MESSAGE_BUFFER_SIZE	0x20

/*
 * Static (Chip) Parameters
 */
#define CTN730_I2C_ADDR			0x28

/* All commands are guaranteed to finish within 1 second. */
#define CTN730_COMMAND_TIME_OUT		(1 * SECOND)

/* Message Types */
#define CTN730_MESSAGE_TYPE_COMMAND	0b00
#define CTN730_MESSAGE_TYPE_RESPONSE	0b01
#define CTN730_MESSAGE_TYPE_EVENT	0b10

/* Instruction Codes */
#define WLC_HOST_CTRL_RESET			0b000000
#define WLC_HOST_CTRL_DUMP_STATUS		0b001100
#define WLC_HOST_CTRL_GENERIC_ERROR		0b001111
#define WLC_HOST_CTRL_BIST			0b000110
#define WLC_CHG_CTRL_ENABLE			0b010000
#define WLC_CHG_CTRL_DISABLE			0b010001
#define WLC_CHG_CTRL_DEVICE_STATE		0b010010
#define WLC_CHG_CTRL_CHARGING_STATE		0b010100
#define WLC_CHG_CTRL_CHARGING_INFO		0b010101

/* WLC_HOST_CTRL_RESET constants */
#define WLC_HOST_CTRL_RESET_CMD_SIZE		1
#define WLC_HOST_CTRL_RESET_RSP_SIZE		1
#define WLC_HOST_CTRL_RESET_EVT_NORMAL_MODE	0x00
#define WLC_HOST_CTRL_RESET_EVT_DOWNLOAD_MODE	0x01
#define WLC_HOST_CTRL_RESET_CMD_MODE_NORMAL	0x00
#define WLC_HOST_CTRL_RESET_CMD_MODE_DOWNLOAD	0x01

/* WLC_CHG_CTRL_ENABLE constants */
#define WLC_CHG_CTRL_ENABLE_CMD_SIZE		2
#define WLC_CHG_CTRL_ENABLE_RSP_SIZE		1

/* WLC_CHG_CTRL_DISABLE constants */
#define WLC_CHG_CTRL_DISABLE_CMD_SIZE		0
#define WLC_CHG_CTRL_DISABLE_RSP_SIZE		1
#define WLC_CHG_CTRL_DISABLE_EVT_SIZE		1

/* WLC_CHG_CTRL_DEVICE_STATE constants */
#define WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DETECTED		0x00
#define WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DEACTIVATED		0x01
#define WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DEVICE_LOST		0x02
#define WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DEVICE_BAD_VERSION	0x03
#define WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE_DETECTED		8
#define WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE			1

/* WLC_CHG_CTRL_CHARGING_STATE constants */
#define WLC_CHG_CTRL_CHARGING_STATE_CHARGE_STARTED		0x00
#define WLC_CHG_CTRL_CHARGING_STATE_CHARGE_ENDED		0x01
#define WLC_CHG_CTRL_CHARGING_STATE_CHARGE_STOPPED		0x02
#define WLC_CHG_CTRL_CHARGING_STATE_EVT_SIZE			1

/* WLC_HOST_CTRL_DUMP_STATUS constants */
#define WLC_HOST_CTRL_DUMP_STATUS_CMD_SIZE	1

/* WLC_CHG_CTRL_CHARGING_INFO constants */
#define WLC_CHG_CTRL_CHARGING_INFO_EVT_SIZE	5

/* Status Codes */
enum wlc_host_status {
	WLC_HOST_STATUS_OK				= 0x00,
	WLC_HOST_STATUS_PARAMETER_ERROR			= 0x01,
	WLC_HOST_STATUS_STATE_ERROR			= 0x02,
	WLC_HOST_STATUS_VALUE_ERROR			= 0x03,
	WLC_HOST_STATUS_REJECTED			= 0x04,
	WLC_HOST_STATUS_RESOURCE_ERROR			= 0x10,
	WLC_HOST_STATUS_TXLDO_ERROR			= 0x11,
	WLC_HOST_STATUS_ANTENNA_SELECTION_ERROR		= 0x12,
	WLC_HOST_STATUS_BIST_FAILED			= 0x20,
	WLC_HOST_STATUS_BIST_NO_WLC_CAP			= 0x21,
	WLC_HOST_STATUS_BIST_TXLDO_CURRENT_OVERFLOW	= 0x22,
	WLC_HOST_STATUS_BIST_TXLDO_CURRENT_UNDERFLOW	= 0x23,
	WLC_HOST_STATUS_FW_VERSION_ERROR		= 0x30,
	WLC_HOST_STATUS_FW_VERIFICATION_ERROR		= 0x31,
	WLC_HOST_STATUS_NTAG_BLOCK_PARAMETER_ERROR	= 0x32,
	WLC_HOST_STATUS_NTAG_READ_ERROR			= 0x33,
};

struct ctn730_msg {
	uint8_t instruction : 6;
	uint8_t message_type : 2;
	uint8_t length;
	uint8_t payload[];
} __packed;

/* This driver isn't compatible with big endian. */
BUILD_ASSERT(__BYTE_ORDER__  == __ORDER_LITTLE_ENDIAN__);

#define CPRINTS(fmt, args...) cprints(CC_PCHG, "CTN730: " fmt, ##args)

static const char *_text_instruction(uint8_t instruction)
{
	/* TODO: For normal build, use %pb and BINARY_VALUE(res->inst, 6) */
	switch (instruction) {
	case WLC_HOST_CTRL_RESET:
		return "RESET";
	case WLC_HOST_CTRL_DUMP_STATUS:
		return "DUMP_STATUS";
	case WLC_HOST_CTRL_GENERIC_ERROR:
		return "GENERIC_ERROR";
	case WLC_HOST_CTRL_BIST:
		return "BIST";
	case WLC_CHG_CTRL_ENABLE:
		return "ENABLE";
	case WLC_CHG_CTRL_DISABLE:
		return "DISABLE";
	case WLC_CHG_CTRL_DEVICE_STATE:
		return "DEVICE_STATE";
	case WLC_CHG_CTRL_CHARGING_STATE:
		return "CHARGING_STATE";
	case WLC_CHG_CTRL_CHARGING_INFO:
		return "CHARGING_INFO";
	default:
		return "UNDEF";
	}
}

static const char *_text_message_type(uint8_t type)
{
	switch (type) {
	case CTN730_MESSAGE_TYPE_COMMAND:
		return "CMD";
	case CTN730_MESSAGE_TYPE_RESPONSE:
		return "RSP";
	case CTN730_MESSAGE_TYPE_EVENT:
		return "EVT";
	default:
		return "BAD";
	}
}

static const char *_text_status_code(uint8_t code)
{
	switch (code) {
	case WLC_HOST_STATUS_OK:
		return "OK";
	case WLC_HOST_STATUS_PARAMETER_ERROR:
		return "PARAMETER_ERR";
	case WLC_HOST_STATUS_STATE_ERROR:
		return "STATE_ERR";
	case WLC_HOST_STATUS_VALUE_ERROR:
		return "VALUE_ERR";
	case WLC_HOST_STATUS_REJECTED:
		return "REJECTED";
	case WLC_HOST_STATUS_RESOURCE_ERROR:
		return "RESOURCE_ERR";
	case WLC_HOST_STATUS_TXLDO_ERROR:
		return "TXLDO_ERR";
	case WLC_HOST_STATUS_ANTENNA_SELECTION_ERROR:
		return "ANTENNA_SELECTION_ERR";
	case WLC_HOST_STATUS_BIST_FAILED:
		return "BIST_FAILED";
	case WLC_HOST_STATUS_BIST_NO_WLC_CAP:
		return "BIST_NO_WLC_CAP";
	case WLC_HOST_STATUS_BIST_TXLDO_CURRENT_OVERFLOW:
		return "BIST_TXLDO_CURRENT_OVERFLOW";
	case WLC_HOST_STATUS_BIST_TXLDO_CURRENT_UNDERFLOW:
		return "BIST_TXLDO_CURRENT_UNDERFLOW";
	case WLC_HOST_STATUS_FW_VERSION_ERROR:
		return "FW_VERSION_ERR";
	case WLC_HOST_STATUS_FW_VERIFICATION_ERROR:
		return "FW_VERIFICATION_ERR";
	case WLC_HOST_STATUS_NTAG_BLOCK_PARAMETER_ERROR:
		return "NTAG_BLOCK_PARAMETER_ERR";
	case WLC_HOST_STATUS_NTAG_READ_ERROR:
		return "NTAG_READ_ERR";
	default:
		return "UNDEF";
	}
}

static int _i2c_read(int i2c_port, uint8_t *in, int in_len)
{
	int rv;

	memset(in, 0, in_len);

	rv = i2c_xfer(i2c_port, CTN730_I2C_ADDR, NULL, 0, in, in_len);
	if (rv == EC_ERROR_BUSY) {
		msleep(_wake_up_delay_ms);
		rv = i2c_xfer(i2c_port, CTN730_I2C_ADDR, NULL, 0, in, in_len);
	}
	if (rv)
		CPRINTS("Failed to read: %d", rv);

	return rv;
}

static void _print_header(const struct ctn730_msg *msg)
{
	CPRINTS("%s_%s LEN=%d",
		_text_instruction(msg->instruction),
		_text_message_type(msg->message_type),
		msg->length);
}

static int _send_command(struct pchg *ctx, const struct ctn730_msg *cmd)
{
	int i2c_port = ctx->cfg->i2c_port;
	int rv;

	_print_header(cmd);

	rv = i2c_xfer(i2c_port, CTN730_I2C_ADDR, (void *)cmd,
		      sizeof(*cmd) + cmd->length, NULL, 0);

	if (rv == EC_ERROR_BUSY) {
		msleep(_wake_up_delay_ms);
		rv = i2c_xfer(i2c_port, CTN730_I2C_ADDR, (void *)cmd,
			      sizeof(*cmd) + cmd->length, NULL, 0);
	}
	if (rv)
		CPRINTS("Failed to write: %d", rv);

	return rv;
}

static int ctn730_init(struct pchg *ctx)
{
	uint8_t buf[CTN730_MESSAGE_BUFFER_SIZE];
	struct ctn730_msg *cmd = (void *)buf;
	int rv;

	cmd->message_type = CTN730_MESSAGE_TYPE_COMMAND;
	cmd->instruction = WLC_HOST_CTRL_RESET;
	cmd->length = WLC_HOST_CTRL_RESET_CMD_SIZE;
	cmd->payload[0] = WLC_HOST_CTRL_RESET_CMD_MODE_NORMAL;

	/* TODO: Run 1 sec timeout timer. */
	rv = _send_command(ctx, cmd);
	if (rv)
		return rv;

	/* WLC-host should send EVT_HOST_CTRL_RESET_EVT shortly. */
	return EC_SUCCESS_IN_PROGRESS;
}

static int ctn730_enable(struct pchg *ctx, bool enable)
{
	uint8_t buf[CTN730_MESSAGE_BUFFER_SIZE];
	struct ctn730_msg *cmd = (void *)buf;
	uint16_t *interval = (void *)cmd->payload;
	int rv;

	cmd->message_type = CTN730_MESSAGE_TYPE_COMMAND;
	if (enable) {
		cmd->instruction = WLC_CHG_CTRL_ENABLE;
		cmd->length = WLC_CHG_CTRL_ENABLE_CMD_SIZE;
		/* Assume core is little endian. Use htole16 for portability. */
		*interval = _detection_interval_ms;
	} else {
		cmd->instruction = WLC_CHG_CTRL_DISABLE;
		cmd->length = WLC_CHG_CTRL_DISABLE_CMD_SIZE;
	}

	rv = _send_command(ctx, cmd);
	if (rv)
		return rv;

	return EC_SUCCESS_IN_PROGRESS;
}

static int _process_payload_response(struct pchg *ctx, struct ctn730_msg *res)
{
	uint8_t len = res->length;
	uint8_t buf[CTN730_MESSAGE_BUFFER_SIZE];
	int rv;

	if (sizeof(buf) < len) {
		CPRINTS("Response size (%d) exceeds buffer", len);
		return EC_ERROR_OVERFLOW;
	}

	rv = _i2c_read(ctx->cfg->i2c_port, buf, len);
	if (rv)
		return rv;

	switch (res->instruction) {
	case WLC_HOST_CTRL_RESET:
		if (len != WLC_HOST_CTRL_RESET_RSP_SIZE
				|| buf[0] != WLC_HOST_STATUS_OK)
			return EC_ERROR_UNKNOWN;
		ctx->event = PCHG_EVENT_NONE;
		break;
	case WLC_CHG_CTRL_ENABLE:
		if (len != WLC_CHG_CTRL_ENABLE_RSP_SIZE
				|| buf[0] != WLC_HOST_STATUS_OK)
			return EC_ERROR_UNKNOWN;
		ctx->event = PCHG_EVENT_ENABLED;
		break;
	case WLC_CHG_CTRL_DISABLE:
		if (len != WLC_CHG_CTRL_DISABLE_RSP_SIZE
				|| buf[0] != WLC_HOST_STATUS_OK)
			return EC_ERROR_UNKNOWN;
		ctx->event = PCHG_EVENT_NONE;
		break;
	default:
		CPRINTS("Received unknown response (%d)", res->instruction);
		ctx->event = PCHG_EVENT_NONE;
		break;
	}

	return EC_SUCCESS;
}

static int _process_payload_event(struct pchg *ctx, struct ctn730_msg *res)
{
	uint8_t len = res->length;
	uint8_t buf[CTN730_MESSAGE_BUFFER_SIZE];
	int rv;

	if (sizeof(buf) < len) {
		CPRINTS("Response size (%d) exceeds buffer", len);
		return EC_ERROR_OVERFLOW;
	}

	rv = _i2c_read(ctx->cfg->i2c_port, buf, len);
	if (rv)
		return rv;

	switch (res->instruction) {
	case WLC_HOST_CTRL_RESET:
		if (buf[0] == WLC_HOST_CTRL_RESET_EVT_NORMAL_MODE)
			ctx->event = PCHG_EVENT_INITIALIZED;
		else
			return EC_ERROR_INVAL;
		break;
	case WLC_HOST_CTRL_GENERIC_ERROR:
		break;
	case WLC_CHG_CTRL_DISABLE:
		if (len != WLC_CHG_CTRL_DISABLE_EVT_SIZE)
			return EC_ERROR_INVAL;
		ctx->event = PCHG_EVENT_DISABLED;
		break;
	case WLC_CHG_CTRL_DEVICE_STATE:
		switch (buf[0]) {
		case WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DETECTED:
			if (len != WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE_DETECTED)
				return EC_ERROR_INVAL;
			ctx->event = PCHG_EVENT_DEVICE_DETECTED;
			break;
		case WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DEVICE_LOST:
			if (len != WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE)
				return EC_ERROR_INVAL;
			ctx->event = PCHG_EVENT_DEVICE_LOST;
			break;
		default:
			return EC_ERROR_INVAL;
		}
		break;
	case WLC_CHG_CTRL_CHARGING_STATE:
		if (len != WLC_CHG_CTRL_CHARGING_STATE_EVT_SIZE)
			return EC_ERROR_INVAL;
		switch (buf[0]) {
		case WLC_CHG_CTRL_CHARGING_STATE_CHARGE_STARTED:
			ctx->event = PCHG_EVENT_CHARGE_STARTED;
			break;
		case WLC_CHG_CTRL_CHARGING_STATE_CHARGE_ENDED:
			ctx->event = PCHG_EVENT_CHARGE_ENDED;
			break;
		case WLC_CHG_CTRL_CHARGING_STATE_CHARGE_STOPPED:
			/* Includes over temp., DISABLE_CMD, device removal. */
			ctx->event = PCHG_EVENT_CHARGE_STOPPED;
			break;
		default:
			return EC_ERROR_INVAL;
		}
		break;
	case WLC_CHG_CTRL_CHARGING_INFO:
		if (len != WLC_CHG_CTRL_CHARGING_INFO_EVT_SIZE || buf[0] > 100)
			return EC_ERROR_INVAL;
		ctx->event = PCHG_EVENT_CHARGE_UPDATE;
		ctx->battery_percent = buf[0];
		break;
	default:
		CPRINTS("Received unknown event (%d)", res->instruction);
		break;
	}

	return EC_SUCCESS;
}

static int ctn730_get_event(struct pchg *ctx)
{
	struct ctn730_msg res;
	int i2c_port = ctx->cfg->i2c_port;
	int rv;

	/* Read message header */
	rv = _i2c_read(i2c_port, (uint8_t *)&res, sizeof(res));
	if (rv)
		return rv;

	_print_header(&res);

	if (res.message_type == CTN730_MESSAGE_TYPE_RESPONSE) {
		/* TODO: Check 1 sec timeout. */
		return _process_payload_response(ctx, &res);
	} else if (res.message_type == CTN730_MESSAGE_TYPE_EVENT) {
		return _process_payload_event(ctx, &res);
	}

	CPRINTS("Invalid message type (%d)", res.message_type);
	return EC_ERROR_UNKNOWN;
}

/**
 * Send command in blocking loop
 *
 * @param ctx     PCHG port context
 * @param buf     [IN] Command header and payload to send.
 *                [OUT] Response header and payload received.
 * @param buf_len Size of <buf>
 * @return        enum ec_error_list
 */
static int _send_command_blocking(struct pchg *ctx, uint8_t *buf, int buf_len)
{
	int i2c_port = ctx->cfg->i2c_port;
	int irq_pin = ctx->cfg->irq_pin;
	struct ctn730_msg *cmd = (void *)buf;
	struct ctn730_msg *res = (void *)buf;
	timestamp_t deadline;
	int rv;

	gpio_disable_interrupt(irq_pin);

	rv = _send_command(ctx, cmd);
	if (rv)
		goto exit;

	deadline.val = get_time().val + CTN730_COMMAND_TIME_OUT;

	/* Busy loop */
	while (gpio_get_level(irq_pin) == 0 && !rv) {
		udelay(1 * MSEC);
		rv = timestamp_expired(deadline, NULL);
		watchdog_reload();
	}

	if (rv) {
		ccprintf("Response timeout\n");
		rv = EC_ERROR_TIMEOUT;
		goto exit;
	}

	rv = _i2c_read(i2c_port, buf, sizeof(*res));
	if (rv)
		goto exit;

	_print_header(res);

	if (sizeof(*res) + res->length > buf_len) {
		ccprintf("RSP size exceeds buffer\n");
		rv = EC_ERROR_OVERFLOW;
		goto exit;
	}

	rv = _i2c_read(i2c_port, res->payload, res->length);
	if (rv)
		goto exit;

exit:
	gpio_clear_pending_interrupt(irq_pin);
	gpio_enable_interrupt(irq_pin);

	return rv;
}

const struct pchg_drv ctn730_drv = {
	.init = ctn730_init,
	.enable = ctn730_enable,
	.get_event = ctn730_get_event,
};

static int cc_ctn730(int argc, char **argv)
{
	int port;
	char *end;
	uint8_t buf[CTN730_MESSAGE_BUFFER_SIZE];
	struct ctn730_msg *cmd = (void *)buf;
	struct ctn730_msg *res = (void *)buf;
	int rv;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &end, 0);
	if (*end || port < 0 || pchg_count <= port)
		return EC_ERROR_PARAM2;

	cmd->message_type = CTN730_MESSAGE_TYPE_COMMAND;

	if (!strcasecmp(argv[2], "dump")) {
		int tag = strtoi(argv[3], &end, 0);

		if (*end || tag < 0 || 0x07 < tag)
			return EC_ERROR_PARAM3;

		cmd->instruction = WLC_HOST_CTRL_DUMP_STATUS;
		cmd->length = WLC_HOST_CTRL_DUMP_STATUS_CMD_SIZE;
		cmd->payload[0] = tag;
	} else if (!strcasecmp(argv[2], "bist")) {
		int id = strtoi(argv[3], &end, 0);

		if (*end || id < 0)
			return EC_ERROR_PARAM3;

		cmd->instruction = WLC_HOST_CTRL_BIST;
		cmd->payload[0] = id;

		switch (id) {
		case 0x01:
			/* Switch on RF field. Tx driver conf not implemented */
			cmd->length = 1;
			break;
		case 0x04:
			/* WLC device activation test */
			cmd->length = 1;
			break;
		default:
			return EC_ERROR_PARAM3;
		}
	} else {
		return EC_ERROR_PARAM2;
	}

	rv = _send_command_blocking(&pchgs[port], buf, sizeof(buf));
	if (rv)
		return rv;

	ccprintf("STATUS_%s\n", _text_status_code(res->payload[0]));
	hexdump(res->payload, res->length);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ctn730, cc_ctn730,
			"<port> dump/bist <tag/id>"
			"\n\t<port> dump <tag>"
			"\n\t<port> bist <test_id>",
			"Control ctn730");
