/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ctn730.h"
#include "gpio.h"
#include "i2c.h"
#include "peripheral_charger.h"
#include "printf.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/*
 * Configuration
 */

/* Print additional data */
#define CTN730_DEBUG

/*
 * When ctn730 is asleep, I2C is ignored but can wake it up. I2C will be resent
 * after this delay.
 */
static const int _wake_up_delay_ms = 10;

/* Device detection interval */
static const int _detection_interval_ms = 500;

/* Buffer size for i2c read & write */
#define CTN730_MESSAGE_BUFFER_SIZE 0x20

/* This driver isn't compatible with big endian. */
BUILD_ASSERT(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

#define CPRINTS(fmt, args...) cprints(CC_PCHG, "CTN730: " fmt, ##args)

static const char *_text_instruction(uint8_t instruction)
{
	switch (instruction) {
	case WLC_HOST_CTRL_RESET:
		return "RESET";
	case WLC_HOST_CTRL_DL_OPEN_SESSION:
		return "DL_OPEN";
	case WLC_HOST_CTRL_DL_COMMIT_SESSION:
		return "DL_COMMIT";
	case WLC_HOST_CTRL_DL_WRITE_FLASH:
		return "DL_WRITE";
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
	case WLC_CHG_CTRL_OPTIONAL_NDEF:
		return "OPTIONAL_NDEF";
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

static const char *_text_reset_reason(uint8_t code)
{
	switch (code) {
	case WLC_HOST_CTRL_RESET_REASON_INTENDED:
		return "intended";
	case WLC_HOST_CTRL_RESET_REASON_CORRUPTED:
		return "corrupted";
	case WLC_HOST_CTRL_RESET_REASON_UNRECOVERABLE:
		return "unrecoverable";
	default:
		return "unknown";
	}
}

static int _i2c_read(int i2c_port, uint8_t *in, int in_len)
{
	int rv;

	memset(in, 0, in_len);

	rv = i2c_xfer(i2c_port, CTN730_I2C_ADDR, NULL, 0, in, in_len);
	if (rv) {
		crec_msleep(_wake_up_delay_ms);
		rv = i2c_xfer(i2c_port, CTN730_I2C_ADDR, NULL, 0, in, in_len);
	}
	if (rv)
		CPRINTS("Failed to read: %d", rv);

	return rv;
}

static void _print_header(const struct ctn730_msg *msg)
{
	CPRINTS("%s_%s", _text_instruction(msg->instruction),
		_text_message_type(msg->message_type));
}

static int _send_command(struct pchg *ctx, const struct ctn730_msg *cmd)
{
	int i2c_port = ctx->cfg->i2c_port;
	int rv;

	_print_header(cmd);

	rv = i2c_xfer(i2c_port, CTN730_I2C_ADDR, (void *)cmd,
		      sizeof(*cmd) + cmd->length, NULL, 0);

	if (rv) {
		crec_msleep(_wake_up_delay_ms);
		rv = i2c_xfer(i2c_port, CTN730_I2C_ADDR, (void *)cmd,
			      sizeof(*cmd) + cmd->length, NULL, 0);
	}
	if (rv)
		CPRINTS("Failed to write: %d", rv);

	return rv;
}

static int ctn730_reset(struct pchg *ctx)
{
	gpio_set_level(GPIO_WLC_NRST_CONN, 0);
	/*
	 * Datasheet says minimum is 10 us. This is better not to be a sleep
	 * especially if it's long (e.g. ~1 ms) since the PCHG state machine
	 * may try to access the I2C bus, which is held low by ctn730.
	 */
	udelay(15);
	gpio_set_level(GPIO_WLC_NRST_CONN, 1);
	return EC_SUCCESS_IN_PROGRESS;
}

static int ctn730_init(struct pchg *ctx)
{
	uint8_t buf[CTN730_MESSAGE_BUFFER_SIZE];
	struct ctn730_msg *cmd = (void *)buf;
	int rv;

	cmd->message_type = CTN730_MESSAGE_TYPE_COMMAND;
	cmd->instruction = WLC_HOST_CTRL_RESET;
	cmd->length = WLC_HOST_CTRL_RESET_CMD_SIZE;
	cmd->payload[0] = ctx->mode == PCHG_MODE_DOWNLOAD ?
				  WLC_HOST_CTRL_RESET_CMD_MODE_DOWNLOAD :
				  WLC_HOST_CTRL_RESET_CMD_MODE_NORMAL;

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

	if (sizeof(buf) < len) {
		CPRINTS("Response size (%d) exceeds buffer", len);
		return EC_ERROR_OVERFLOW;
	}

	if (len > 0) {
		int rv = _i2c_read(ctx->cfg->i2c_port, buf, len);
		if (rv)
			return rv;
		if (IS_ENABLED(CTN730_DEBUG)) {
			char str_buf[hex_str_buf_size(len)];

			snprintf_hex_buffer(str_buf, sizeof(str_buf),
					    HEX_BUF(buf, len));
			CPRINTS("Payload: %s", str_buf);
		}
	}

	ctx->event = PCHG_EVENT_NONE;

	/*
	 * Messages with no payload (<len> == 0) is allowed in the spec. So,
	 * make sure <len> is checked before reading buf[0].
	 */
	switch (res->instruction) {
	case WLC_HOST_CTRL_RESET:
		if (len != WLC_HOST_CTRL_RESET_RSP_SIZE)
			return EC_ERROR_UNKNOWN;
		if (buf[0] != WLC_HOST_STATUS_OK) {
			ctx->event = PCHG_EVENT_ERROR;
			ctx->error |= PCHG_ERROR_MASK(PCHG_ERROR_RESPONSE);
		}
		break;
	case WLC_HOST_CTRL_DL_OPEN_SESSION:
		if (len != WLC_HOST_CTRL_DL_OPEN_SESSION_RSP_SIZE)
			return EC_ERROR_UNKNOWN;
		if (buf[0] != WLC_HOST_STATUS_OK) {
			CPRINTS("FW open session failed for %s",
				_text_status_code(buf[0]));
			ctx->event = PCHG_EVENT_UPDATE_ERROR;
			ctx->error |= PCHG_ERROR_MASK(PCHG_ERROR_FW_VERSION);
		} else {
			ctx->event = PCHG_EVENT_UPDATE_OPENED;
		}
		break;
	case WLC_HOST_CTRL_DL_COMMIT_SESSION:
		if (len != WLC_HOST_CTRL_DL_COMMIT_SESSION_RSP_SIZE)
			return EC_ERROR_UNKNOWN;
		if (buf[0] != WLC_HOST_STATUS_OK) {
			CPRINTS("FW commit failed for %s",
				_text_status_code(buf[0]));
			ctx->event = PCHG_EVENT_UPDATE_ERROR;
			ctx->error |= PCHG_ERROR_MASK(PCHG_ERROR_INVALID_FW);
		} else {
			ctx->event = PCHG_EVENT_UPDATE_CLOSED;
		}
		break;
	case WLC_HOST_CTRL_DL_WRITE_FLASH:
		if (len != WLC_HOST_CTRL_DL_WRITE_FLASH_RSP_SIZE)
			return EC_ERROR_UNKNOWN;
		if (buf[0] != WLC_HOST_STATUS_OK) {
			CPRINTS("FW write failed for %s",
				_text_status_code(buf[0]));
			ctx->event = PCHG_EVENT_UPDATE_ERROR;
			ctx->error |= PCHG_ERROR_MASK(PCHG_ERROR_WRITE_FLASH);
		} else {
			ctx->event = PCHG_EVENT_UPDATE_WRITTEN;
		}
		break;
	case WLC_HOST_CTRL_BIST:
		if (len != WLC_HOST_CTRL_BIST_CMD_SIZE)
			return EC_ERROR_UNKNOWN;
		if (buf[0] != WLC_HOST_STATUS_OK) {
			CPRINTS("BIST command failed for %s",
				_text_status_code(buf[0]));
			ctx->event = PCHG_EVENT_ERROR;
			ctx->error |= PCHG_ERROR_MASK(PCHG_ERROR_RESPONSE);
		}
		break;
	case WLC_CHG_CTRL_ENABLE:
		if (len != WLC_CHG_CTRL_ENABLE_RSP_SIZE)
			return EC_ERROR_UNKNOWN;
		if (buf[0] != WLC_HOST_STATUS_OK) {
			ctx->event = PCHG_EVENT_ERROR;
			ctx->error |= PCHG_ERROR_MASK(PCHG_ERROR_RESPONSE);
		} else
			ctx->event = PCHG_EVENT_ENABLED;
		break;
	case WLC_CHG_CTRL_DISABLE:
		if (len != WLC_CHG_CTRL_DISABLE_RSP_SIZE)
			return EC_ERROR_UNKNOWN;
		if (buf[0] != WLC_HOST_STATUS_OK) {
			ctx->event = PCHG_EVENT_ERROR;
			ctx->error |= PCHG_ERROR_MASK(PCHG_ERROR_RESPONSE);
		} else
			ctx->event = PCHG_EVENT_DISABLED;
		break;
	case WLC_CHG_CTRL_CHARGING_INFO:
		if (len != WLC_CHG_CTRL_CHARGING_INFO_RSP_SIZE)
			return EC_ERROR_UNKNOWN;
		if (buf[0] != WLC_HOST_STATUS_OK) {
			ctx->event = PCHG_EVENT_ERROR;
			ctx->error |= PCHG_ERROR_MASK(PCHG_ERROR_RESPONSE);
		} else {
			ctx->battery_percent = buf[1];
			ctx->event = PCHG_EVENT_CHARGE_UPDATE;
		}
		break;
	default:
		CPRINTS("Received unknown response (%d)", res->instruction);
		break;
	}

	return EC_SUCCESS;
}

static int _process_payload_event(struct pchg *ctx, struct ctn730_msg *res)
{
	uint8_t len = res->length;
	uint8_t buf[CTN730_MESSAGE_BUFFER_SIZE];

	if (sizeof(buf) < len) {
		CPRINTS("Response size (%d) exceeds buffer", len);
		return EC_ERROR_OVERFLOW;
	}

	if (len > 0) {
		int rv = _i2c_read(ctx->cfg->i2c_port, buf, len);
		if (rv)
			return rv;
		if (IS_ENABLED(CTN730_DEBUG)) {
			char str_buf[hex_str_buf_size(len)];

			snprintf_hex_buffer(str_buf, sizeof(str_buf),
					    HEX_BUF(buf, len));
			CPRINTS("Payload: %s", str_buf);
		}
	}

	ctx->event = PCHG_EVENT_NONE;

	/*
	 * Messages with no payload (<len> == 0) is allowed in the spec. So,
	 * make sure <len> is checked before reading buf[0].
	 */
	switch (res->instruction) {
	case WLC_HOST_CTRL_RESET:
		if (len < WLC_HOST_CTRL_RESET_EVT_MIN_SIZE)
			return EC_ERROR_INVAL;
		if (buf[0] == WLC_HOST_CTRL_RESET_EVT_NORMAL_MODE) {
			if (len != WLC_HOST_CTRL_RESET_EVT_NORMAL_MODE_SIZE)
				return EC_ERROR_INVAL;
			ctx->event = PCHG_EVENT_IN_NORMAL;
			ctx->fw_version = (uint16_t)buf[1] << 8 | buf[2];
			CPRINTS("Normal Mode (FW=0x%02x.%02x)", buf[1], buf[2]);
			/*
			 * ctn730 isn't immediately ready for i2c write after
			 * normal mode initialization (b:178096436).
			 */
			crec_msleep(5);
		} else if (buf[0] == WLC_HOST_CTRL_RESET_EVT_DOWNLOAD_MODE) {
			if (len != WLC_HOST_CTRL_RESET_EVT_DOWNLOAD_MODE_SIZE)
				return EC_ERROR_INVAL;
			CPRINTS("Download Mode (%s)",
				_text_reset_reason(buf[1]));
			ctx->event = PCHG_EVENT_RESET;
			/*
			 * CTN730 sends a reset event to notify us it entered
			 * download mode unintentionally (e.g. corrupted FW).
			 * In such cases, we stay in download mode to avoid an
			 * infinite loop.
			 *
			 * If it's intended, we leave alone the mode, respecting
			 * the mode set by the PCHG task.
			 */
			if (buf[1] != WLC_HOST_CTRL_RESET_REASON_INTENDED)
				ctx->mode = PCHG_MODE_DOWNLOAD;
		} else {
			return EC_ERROR_INVAL;
		}
		break;
	case WLC_HOST_CTRL_GENERIC_ERROR:
		break;
	case WLC_CHG_CTRL_DISABLE:
		if (len != WLC_CHG_CTRL_DISABLE_EVT_SIZE)
			return EC_ERROR_INVAL;
		ctx->event = PCHG_EVENT_DISABLED;
		break;
	case WLC_CHG_CTRL_DEVICE_STATE:
		if (len < WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE)
			return EC_ERROR_INVAL;
		switch (buf[0]) {
		case WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DOCKED:
			if (len != WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE)
				return EC_ERROR_INVAL;
			ctx->event = PCHG_EVENT_DEVICE_DETECTED;
			break;
		case WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DETECTED:
			if (len != WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE_DETECTED)
				return EC_ERROR_INVAL;
			ctx->event = PCHG_EVENT_DEVICE_CONNECTED;
			break;
		case WLC_CHG_CTRL_DEVICE_STATE_DEVICE_DEVICE_LOST:
		case WLC_CHG_CTRL_DEVICE_STATE_DEVICE_UNDOCKED:
			if (len != WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE)
				return EC_ERROR_INVAL;
			ctx->event = PCHG_EVENT_DEVICE_LOST;
			break;
		case WLC_CHG_CTRL_DEVICE_STATE_DEVICE_FO_PRESENT:
			if (len != WLC_CHG_CTRL_DEVICE_STATE_EVT_SIZE)
				return EC_ERROR_INVAL;
			ctx->event = PCHG_EVENT_ERROR;
			ctx->error |=
				PCHG_ERROR_MASK(PCHG_ERROR_FOREIGN_OBJECT);
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
	case WLC_CHG_CTRL_OPTIONAL_NDEF:
		if (len == 0)
			return EC_ERROR_INVAL;
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

static int ctn730_get_soc(struct pchg *ctx)
{
	struct ctn730_msg cmd;
	int rv;

	cmd.message_type = CTN730_MESSAGE_TYPE_COMMAND;
	cmd.instruction = WLC_CHG_CTRL_CHARGING_INFO;
	cmd.length = WLC_CHG_CTRL_CHARGING_INFO_CMD_SIZE;

	rv = _send_command(ctx, &cmd);
	if (rv)
		return rv;

	return EC_SUCCESS_IN_PROGRESS;
}

static int ctn730_update_open(struct pchg *ctx)
{
	uint8_t buf[sizeof(struct ctn730_msg) +
		    WLC_HOST_CTRL_DL_OPEN_SESSION_CMD_SIZE];
	struct ctn730_msg *cmd = (void *)buf;
	uint32_t version = ctx->update.version;
	int rv;

	cmd->message_type = CTN730_MESSAGE_TYPE_COMMAND;
	cmd->instruction = WLC_HOST_CTRL_DL_OPEN_SESSION;
	cmd->length = WLC_HOST_CTRL_DL_OPEN_SESSION_CMD_SIZE;
	cmd->payload[0] = (version >> 8) & 0xff;
	cmd->payload[1] = version & 0xff;

	rv = _send_command(ctx, cmd);
	if (rv)
		return rv;

	return EC_SUCCESS_IN_PROGRESS;
}

static int ctn730_update_write(struct pchg *ctx)
{
	uint8_t buf[sizeof(struct ctn730_msg) +
		    WLC_HOST_CTRL_DL_WRITE_FLASH_CMD_SIZE];
	struct ctn730_msg *cmd = (void *)buf;
	uint32_t *a = (void *)cmd->payload;
	uint8_t *d = (void *)&cmd->payload[CTN730_FLASH_ADDR_SIZE];
	int rv;

	/* Address is 3 bytes. FW size must be a multiple of 128 bytes. */
	if (ctx->update.addr & GENMASK(31, 24) ||
	    ctx->update.size != WLC_HOST_CTRL_DL_WRITE_FLASH_BLOCK_SIZE)
		return EC_ERROR_INVAL;

	cmd->message_type = CTN730_MESSAGE_TYPE_COMMAND;
	cmd->instruction = WLC_HOST_CTRL_DL_WRITE_FLASH;
	cmd->length = WLC_HOST_CTRL_DL_WRITE_FLASH_CMD_SIZE;

	/* 4th byte will be overwritten by memcpy below. */
	*a = ctx->update.addr;

	/* Store data in payload with 0-padding for short blocks. */
	memset(d, 0, WLC_HOST_CTRL_DL_WRITE_FLASH_BLOCK_SIZE);
	memcpy(d, ctx->update.data, ctx->update.size);

	rv = _send_command(ctx, cmd);
	if (rv)
		return rv;

	return EC_SUCCESS_IN_PROGRESS;
}

static int ctn730_update_close(struct pchg *ctx)
{
	uint8_t buf[sizeof(struct ctn730_msg) +
		    WLC_HOST_CTRL_DL_COMMIT_SESSION_CMD_SIZE];
	struct ctn730_msg *cmd = (void *)buf;
	uint32_t *crc32 = (void *)cmd->payload;
	int rv;

	cmd->message_type = CTN730_MESSAGE_TYPE_COMMAND;
	cmd->instruction = WLC_HOST_CTRL_DL_COMMIT_SESSION;
	cmd->length = WLC_HOST_CTRL_DL_COMMIT_SESSION_CMD_SIZE;
	*crc32 = ctx->update.crc32;

	rv = _send_command(ctx, cmd);
	if (rv)
		return rv;

	return EC_SUCCESS_IN_PROGRESS;
}

static int ctn730_passthru(struct pchg *ctx, bool enable)
{
	ctx->mode = enable ? PCHG_MODE_PASSTHRU : PCHG_MODE_NORMAL;

	return EC_SUCCESS;
}

static int ctn730_bist(struct pchg *ctx, uint8_t test_id)
{
	uint8_t buf[sizeof(struct ctn730_msg) + WLC_HOST_CTRL_BIST_CMD_SIZE];
	struct ctn730_msg *cmd = (void *)buf;
	uint8_t *id = cmd->payload;
	int rv;

	cmd->message_type = CTN730_MESSAGE_TYPE_COMMAND;
	cmd->instruction = WLC_HOST_CTRL_BIST;
	*id = test_id;

	switch (test_id) {
	case PCHG_BIST_CMD_RF_CHARGE_ON:
		cmd->length = 1;
		break;
	default:
		return EC_ERROR_UNIMPLEMENTED;
	}

	rv = _send_command(ctx, cmd);
	if (rv)
		return rv;

	return EC_SUCCESS_IN_PROGRESS;
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
	.reset = ctn730_reset,
	.init = ctn730_init,
	.enable = ctn730_enable,
	.get_event = ctn730_get_event,
	.get_soc = ctn730_get_soc,
	.update_open = ctn730_update_open,
	.update_write = ctn730_update_write,
	.update_close = ctn730_update_close,
	.passthru = ctn730_passthru,
	.bist = ctn730_bist,
};

static int cc_ctn730(int argc, const char **argv)
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
	if (*end || port < 0 || board_get_pchg_count() <= port)
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
		case WLC_BIST_CMD_RF_SWITCH_ON:
		case WLC_BIST_CMD_RF_SWITCH_OFF:
			/* Tx driver configuration is not implemented. */
			cmd->length = 1;
			break;
		case WLC_BIST_CMD_DEVICE_ACTIVATION_TEST:
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
