/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_pdc.h"
#include "emul/emul_realtek_rts54xx.h"
#include "zephyr/sys/util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>
#endif

#define DT_DRV_COMPAT realtek_rts54_pdc

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(realtek_rts5453_emul);

static bool send_response(struct rts5453p_emul_pdc_data *data);

struct rts5453p_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;

	/** Data required to simulate PD Controller */
	struct rts5453p_emul_pdc_data pdc_data;
};

struct rts5453p_emul_pdc_data *
rts5453p_emul_get_pdc_data(const struct emul *emul)
{
	struct rts5453p_emul_data *data = emul->data;

	return &data->pdc_data;
}

static void set_ping_status(struct rts5453p_emul_pdc_data *data,
			    enum cmd_sts_t status, uint8_t length)
{
	LOG_DBG("ping status=0x%x, length=%d", status, length);
	data->read_ping = true;
	data->ping_status.cmd_sts = status;
	data->ping_status.data_len = length;
}

typedef int (*handler)(struct rts5453p_emul_pdc_data *data,
		       const union rts54_request *req);

static int unsupported(struct rts5453p_emul_pdc_data *data,
		       const union rts54_request *req)
{
	LOG_ERR("cmd=0x%X, subcmd=0x%X is not supported",
		req->req_subcmd.command_code, req->req_subcmd.sub_cmd);

	set_ping_status(data, CMD_ERROR, 0);
	return -EIO;
}

static int vendor_cmd_enable(struct rts5453p_emul_pdc_data *data,
			     const union rts54_request *req)
{
	data->vnd_command.raw = req->vendor_cmd_enable.sub_cmd3.raw;

	LOG_INF("VENDOR_CMD_ENABLE SMBUS=%d, FLASH=%d", data->vnd_command.smbus,
		data->vnd_command.flash);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int set_notification_enable(struct rts5453p_emul_pdc_data *data,
				   const union rts54_request *req)
{
	uint8_t port = req->set_notification_enable.port_num;

	data->notification_data[port] = req->set_notification_enable.data;
	LOG_INF("SET_NOTIFICATION_ENABLE port=%d, data=0x%X", port,
		data->notification_data[port]);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int get_ic_status(struct rts5453p_emul_pdc_data *data,
			 const union rts54_request *req)
{
	LOG_INF("%s", __func__);

	data->response.ic_status = data->ic_status;

	send_response(data);

	return 0;
}

static int block_read(struct rts5453p_emul_pdc_data *data,
		      const union rts54_request *req)
{
	data->read_ping = false;
	return 0;
}

static int ppm_reset(struct rts5453p_emul_pdc_data *data,
		     const union rts54_request *req)
{
	LOG_INF("PPM_RESET port=%d", req->ppm_reset.port_num);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int tcpm_reset(struct rts5453p_emul_pdc_data *data,
		      const union rts54_request *req)
{
	LOG_INF("%s", __func__);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static bool send_response(struct rts5453p_emul_pdc_data *data)
{
	if (data->delay_ms > 0) {
		/* Simulate work and defer completion status */
		set_ping_status(data, CMD_DEFERRED, 0);
		k_work_schedule(&data->delay_work, K_MSEC(data->delay_ms));
		return true;
	}

	set_ping_status(
		data, CMD_COMPLETE,
		data->response.byte_count ? data->response.byte_count + 1 : 0);

	return false;
}

static void delayable_work_handler(struct k_work *w)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(w);
	struct rts5453p_emul_pdc_data *data =
		CONTAINER_OF(dwork, struct rts5453p_emul_pdc_data, delay_work);

	set_ping_status(
		data, CMD_COMPLETE,
		data->response.byte_count ? data->response.byte_count + 1 : 0);
}

struct commands {
	uint8_t code;
	enum {
		HANDLER = 0,
		SUBCMD = 1,
	} type;
	union {
		struct {
			uint8_t num_cmds;
			const struct commands *sub_cmd;
		};
		handler fn;
	};
};

#define SUBCMD_DEF(subcmd) \
	.type = SUBCMD, .sub_cmd = subcmd, .num_cmds = ARRAY_SIZE(subcmd)
#define HANDLER_DEF(handler) .type = HANDLER, .fn = handler

/* Data Sheet:
 * Realtek Power Delivery Command Interface By Realtek Version 3.3.18
 */
const struct commands sub_cmd_x01[] = {
	{ .code = 0xDA, HANDLER_DEF(vendor_cmd_enable) },
};

const struct commands sub_cmd_x08[] = {
	{ .code = 0x00, HANDLER_DEF(tcpm_reset) },
	{ .code = 0x01, HANDLER_DEF(set_notification_enable) },
	{ .code = 0x03, HANDLER_DEF(unsupported) },
	{ .code = 0x04, HANDLER_DEF(unsupported) },
	{ .code = 0x44, HANDLER_DEF(unsupported) },
	{ .code = 0x05, HANDLER_DEF(unsupported) },
	{ .code = 0x19, HANDLER_DEF(unsupported) },
	{ .code = 0x1A, HANDLER_DEF(unsupported) },
	{ .code = 0x1D, HANDLER_DEF(unsupported) },
	{ .code = 0x1F, HANDLER_DEF(unsupported) },
	{ .code = 0x20, HANDLER_DEF(unsupported) },
	{ .code = 0x21, HANDLER_DEF(unsupported) },
	{ .code = 0x23, HANDLER_DEF(unsupported) },
	{ .code = 0x24, HANDLER_DEF(unsupported) },
	{ .code = 0x26, HANDLER_DEF(unsupported) },
	{ .code = 0x27, HANDLER_DEF(unsupported) },
	{ .code = 0x28, HANDLER_DEF(unsupported) },
	{ .code = 0x2B, HANDLER_DEF(unsupported) },
	{ .code = 0x83, HANDLER_DEF(unsupported) },
	{ .code = 0x84, HANDLER_DEF(unsupported) },
	{ .code = 0x85, HANDLER_DEF(unsupported) },
	{ .code = 0x99, HANDLER_DEF(unsupported) },
	{ .code = 0x9A, HANDLER_DEF(unsupported) },
	{ .code = 0x9D, HANDLER_DEF(unsupported) },
	{ .code = 0xA2, HANDLER_DEF(unsupported) },
	{ .code = 0xF0, HANDLER_DEF(unsupported) },
	{ .code = 0xA6, HANDLER_DEF(unsupported) },
	{ .code = 0xA7, HANDLER_DEF(unsupported) },
	{ .code = 0xA8, HANDLER_DEF(unsupported) },
	{ .code = 0xA9, HANDLER_DEF(unsupported) },
	{ .code = 0xAA, HANDLER_DEF(unsupported) },
};

const struct commands sub_cmd_x0E[] = {
	{ .code = 0x01, HANDLER_DEF(ppm_reset) },
	{ .code = 0x03, HANDLER_DEF(unsupported) },
	{ .code = 0x06, HANDLER_DEF(unsupported) },
	{ .code = 0x07, HANDLER_DEF(unsupported) },
	{ .code = 0x09, HANDLER_DEF(unsupported) },
	{ .code = 0x0B, HANDLER_DEF(unsupported) },
	{ .code = 0x0C, HANDLER_DEF(unsupported) },
	{ .code = 0x0D, HANDLER_DEF(unsupported) },
	{ .code = 0x0E, HANDLER_DEF(unsupported) },
	{ .code = 0x0F, HANDLER_DEF(unsupported) },
	{ .code = 0x10, HANDLER_DEF(unsupported) },
	{ .code = 0x11, HANDLER_DEF(unsupported) },
	{ .code = 0x12, HANDLER_DEF(unsupported) },
	{ .code = 0x13, HANDLER_DEF(unsupported) },
};

const struct commands sub_cmd_x12[] = {
	{ .code = 0x01, HANDLER_DEF(unsupported) },
	{ .code = 0x02, HANDLER_DEF(unsupported) },
};

const struct commands sub_cmd_x20[] = {
	{ .code = 0x00, HANDLER_DEF(unsupported) },
};

const struct commands rts54_commands[] = {
	{ .code = 0x01, SUBCMD_DEF(sub_cmd_x01) },
	{ .code = 0x08, SUBCMD_DEF(sub_cmd_x08) },
	{ .code = 0x09, HANDLER_DEF(unsupported) },
	{ .code = 0x0A, HANDLER_DEF(unsupported) },
	{ .code = 0x0E, SUBCMD_DEF(sub_cmd_x0E) },
	{ .code = 0x12, SUBCMD_DEF(sub_cmd_x12) },
	{ .code = 0x20, SUBCMD_DEF(sub_cmd_x20) },
	{ .code = 0x3A, HANDLER_DEF(get_ic_status) },
	{ .code = 0x80, HANDLER_DEF(block_read) },
};

const int num_rts54_commands = ARRAY_SIZE(rts54_commands);

int process_request(struct rts5453p_emul_pdc_data *data,
		    const union rts54_request *req, uint8_t code,
		    const struct commands *cmds, int num_cmds)
{
	int i;

	LOG_INF("process request code=0x%X", code);

	set_ping_status(data, CMD_BUSY, 0);

	for (i = 0; i < num_cmds; i++) {
		if (cmds[i].code == code) {
			if (cmds[i].type == HANDLER) {
				return cmds[i].fn(data, req);
			} else {
				return process_request(data, req,
						       req->req_subcmd.sub_cmd,
						       cmds[i].sub_cmd,
						       cmds[i].num_cmds);
			}
		}
	}

	return unsupported(data, req);
}

/**
 * @brief Handle I2C start write message.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of write message, usually selected command
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_start_write(const struct emul *emul, int reg)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	LOG_DBG("start_write cmd=%d", reg);

	memset(&data->request, 0, sizeof(union rts54_request));

	data->request.raw_data[0] = reg;

	return 0;
}

/**
 * @brief Function called for each byte of write message which is saved in
 *        data->msg_buf
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of write message, usually selected command
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 */
static int rts5453p_emul_write_byte(const struct emul *emul, int reg,
				    uint8_t val, int bytes)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	LOG_DBG("write_byte reg=%d, val=0x%X, bytes=%d", reg, val, bytes);
	data->request.raw_data[bytes] = val;

	return 0;
}

/**
 * @brief Function which finalize write messages.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of write message, usually selected command
 * @param bytes Number of bytes received in data->msg_buf
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_finish_write(const struct emul *emul, int reg,
				      int bytes)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	LOG_DBG("finish_write reg=%d, bytes=%d", reg, bytes);

	return process_request(data, &data->request,
			       data->request.request.command_code,
			       rts54_commands, num_rts54_commands);
}

/**
 * @brief Function which handles read messages. It expects that data->cur_cmd
 *        is set to command number which should be handled. It guarantees that
 *        data->num_to_read is set to number of bytes in data->msg_buf on
 *        successful handling read request. On error, data->num_to_read is
 *        always set to 0.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg Command selected by last write message. If data->cur_cmd is
 *            different than NO_CMD, then reg should equal to
 *            data->cur_cmd
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_start_read(const struct emul *emul, int reg)
{
	LOG_DBG("start_read reg=%d", reg);
	return 0;
}

/**
 * @brief Function called for each byte of read message. Byte from data->msg_buf
 *        is copied to read message response.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of last write message, usually selected command
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 */
static int rts5453p_emul_read_byte(const struct emul *emul, int reg,
				   uint8_t *val, int bytes)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	LOG_DBG("read_byte reg=0x%X, bytes=%d", reg, bytes);

	if (data->read_ping) {
		LOG_DBG("READING ping_raw_value=0x%X", data->ping_raw_value);
		*val = data->ping_raw_value;
		data->read_ping = false;
	} else {
		*val = data->response.raw_data[bytes];
	}

	return 0;
}

/**
 * @brief Function type that is used by I2C device emulator at the end of
 *        I2C read message.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by read command (first byte of last
 *            I2C write message)
 * @param bytes Number of bytes responeded to the I2C read message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_finish_read(const struct emul *target, int reg,
				     int bytes)
{
	LOG_DBG("finish_read reg=%d, bytes=%d", reg, bytes);
	return 0;
}
/**
 * @brief Get currently accessed register, which always equals to selected
 *        command.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of last write message, usually selected command
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int rts5453p_emul_access_reg(const struct emul *emul, int reg, int bytes,
				    bool read)
{
	return reg;
}

/* Device instantiation */

/**
 * @brief Set up a new RTS5453P emulator
 *
 * This should be called for each RTS5453P device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int rts5453p_emul_init(const struct emul *emul,
			      const struct device *parent)
{
	struct rts5453p_emul_data *data = emul->data;
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	data->common.i2c = parent;
	data->common.cfg = cfg;

	i2c_common_emul_init(&data->common);

	data->pdc_data.ic_status.fw_main_version = 0xAB;
	data->pdc_data.ic_status.pd_version[0] = 0xCD;
	data->pdc_data.ic_status.pd_revision[0] = 0xEF;
	data->pdc_data.ic_status.byte_count =
		sizeof(struct rts54_ic_status) - 1;

	k_work_init_delayable(&data->pdc_data.delay_work,
			      delayable_work_handler);

	return 0;
}

static int emul_realtek_rts54xx_set_response_delay(const struct emul *target,
						   uint32_t delay_ms)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	data->delay_ms = delay_ms;

	return 0;
}

struct emul_pdc_api_t emul_realtek_rts54xx_api = {
	.set_response_delay = emul_realtek_rts54xx_set_response_delay,
};

#define RTS5453P_EMUL_DEFINE(n)                                             \
	static struct rts5453p_emul_data rts5453p_emul_data_##n = {	\
		.common = {						\
			.start_write = rts5453p_emul_start_write,	\
			.write_byte = rts5453p_emul_write_byte,		\
			.finish_write = rts5453p_emul_finish_write,\
			.start_read = rts5453p_emul_start_read,	\
			.read_byte = rts5453p_emul_read_byte,		\
			.finish_read = rts5453p_emul_finish_read,	\
			.access_reg = rts5453p_emul_access_reg,		\
		},							\
	};       \
	static const struct i2c_common_emul_cfg rts5453p_emul_cfg_##n = {   \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),             \
		.data = &rts5453p_emul_data_##n.common,                     \
		.addr = DT_INST_REG_ADDR(n),                                \
	};                                                                  \
	EMUL_DT_INST_DEFINE(n, rts5453p_emul_init, &rts5453p_emul_data_##n, \
			    &rts5453p_emul_cfg_##n, &i2c_common_emul_api,   \
			    &emul_realtek_rts54xx_api)

DT_INST_FOREACH_STATUS_OKAY(RTS5453P_EMUL_DEFINE)

struct i2c_common_emul_data *
rts5453p_emul_get_i2c_common_data(const struct emul *emul)
{
	struct rts5453p_emul_data *data = emul->data;

	return &data->common;
}
