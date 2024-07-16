/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_pdc.h"
#include "emul/emul_tps6699x.h"
#include "emul_tps6699x_private.h"
#include "usbc/utils.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT ti_tps6699_pdc

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(tps6699x_emul);

/* TODO(b/345292002): Implement this emulator to the point where
 * pdc.generic.tps6699x passes.
 */

struct tps6699x_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
	/** Data required to emulate PD controller */
	struct tps6699x_emul_pdc_data pdc_data;
	/** PD port number */
	uint8_t port;
};

static struct tps6699x_emul_pdc_data *
tps6699x_emul_get_pdc_data(const struct emul *emul)
{
	struct tps6699x_emul_data *data = emul->data;

	return &data->pdc_data;
}

static int emul_tps6699x_get_connector_reset(const struct emul *emul,
					     union connector_reset_t *reset_cmd)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);

	*reset_cmd = data->reset_cmd;

	return 0;
}

static bool register_is_valid(const struct tps6699x_emul_pdc_data *data,
			      int reg)
{
	return reg < sizeof(data->reg_val) / sizeof(*data->reg_val);
}

/** Check that a register access is valid. A valid access has
 *  1) a valid register address,
 *  2) a byte offset less than the size of that register, and
 *  3) a byte offset less than the size of the read or write indicated at the
 *     start of this transaction.
 *
 *  @param data  Emulator data; not really used at runtime, but makes offset
 *               checks shorter and more obviously correct
 *  @param reg   Register address from first byte of write message
 *  @param bytes Offset within register of current byte; for writes, this is 1
 *               less than the offset within the message body, because byte 0 is
 *               the write length.
 *  @return True if register access is valid
 */
static bool register_access_is_valid(const struct tps6699x_emul_pdc_data *data,
				     int reg, int bytes)
{
	return register_is_valid(data, reg) &&
	       bytes <= sizeof(*data->reg_val) &&
	       bytes <= data->transaction_bytes;
}

static void tps6699x_emul_connector_reset(struct tps6699x_emul_pdc_data *data,
					  union connector_reset_t reset_cmd)
{
	/* TODO(b/345292002): Update other registers to reflect effects of Hard
	 * Reset or Data Reset. */
	data->reset_cmd = reset_cmd;
}

static void tps6699x_emul_handle_ucsi(struct tps6699x_emul_pdc_data *data,
				      uint8_t *data_reg)
{
	/* For all UCSI commands, the first 3 data fields are
	 * the UCSI command (8 bits),
	 * the data length (8 bits, always 0), and
	 * the connector number (7 bits, must correspond to the same port as
	 * this data register.
	 * Subsequent fields vary depending on the command.
	 */
	enum ucsi_command_t cmd = data_reg[0];
	uint8_t data_len = data_reg[1];

	zassert_equal(data_len, 0);
	/* TODO(b/345292002): Validate connector number field. */

	switch (cmd) {
	case UCSI_CONNECTOR_RESET:
		tps6699x_emul_connector_reset(
			data, (union connector_reset_t)data_reg[2]);
		break;
	default:
		LOG_WRN("tps6699x_emul: Unimplemented UCSI command %#04x", cmd);
	};
}

static void tps6699x_emul_handle_command(struct tps6699x_emul_pdc_data *data,
					 enum tps6699x_command_task task,
					 uint8_t *data_reg)
{
	char task_str[5] = {
		((char *)&task)[0],
		((char *)&task)[1],
		((char *)&task)[2],
		((char *)&task)[3],
		'\0',
	};

	/* TODO(b/345292002): Respond to commands asynchronously. */

	switch (task) {
	case COMMAND_TASK_UCSI:
		tps6699x_emul_handle_ucsi(data, data_reg);
		break;
	default:
		LOG_WRN("emul_tps6699x: Unimplemented task %s", task_str);
	}
}

static void tps6699x_emul_handle_write(struct tps6699x_emul_pdc_data *data,
				       int reg)
{
	switch (reg) {
		/* Some registers trigger an action on write. */
	case TPS6699X_REG_COMMAND_I2C1:
		tps6699x_emul_handle_command(
			data,
			*(enum tps6699x_command_task *)
				 data->reg_val[TPS6699X_REG_COMMAND_I2C1],
			data->reg_val[TPS6699X_REG_DATA_I2C1]);
		break;
	default:
		/* No action on write */
		break;
	};
}

static int tps6699x_emul_start_write(const struct emul *emul, int reg)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);

	if (!register_is_valid(data, reg)) {
		return -EIO;
	}

	memset(&data->reg_val[reg], 0, sizeof(data->reg_val[reg]));

	data->reg_addr = reg;

	return 0;
}

static int tps6699x_emul_write_byte(const struct emul *emul, int reg,
				    uint8_t val, int bytes)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);
	/* The first byte of the write message is the length. */
	int data_bytes = bytes - 1;

	__ASSERT(bytes > 0, "start_write implicitly consumes byte 0");

	if (bytes == 1) {
		data->transaction_bytes = val;
		return 0;
	}

	if (!register_access_is_valid(data, reg, data_bytes)) {
		LOG_ERR("Invalid register access of %#02x[%#02x]", reg,
			data_bytes);
		return -EIO;
	}

	data->reg_val[reg][data_bytes] = val;

	return 0;
}

static int tps6699x_emul_finish_write(const struct emul *emul, int reg,
				      int bytes)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);

	__ASSERT(bytes > 0,
		 "start_write and write_byte implicitly consume bytes 0-1");

	LOG_DBG("finish_write reg=%#x, bytes=%d+2", reg, bytes - 2);

	/* No need to validate inputs; this function will only be called if
	 * write_byte validated its inputs and succeeded.
	 */

	tps6699x_emul_handle_write(data, reg);

	return 0;
}

static int tps6699x_emul_start_read(const struct emul *emul, int reg)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);

	if (!register_is_valid(data, reg)) {
		return -EIO;
	}

	data->reg_addr = reg;

	return 0;
}

static int tps6699x_emul_read_byte(const struct emul *emul, int reg,
				   uint8_t *val, int bytes)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);

	if (!register_access_is_valid(data, reg, bytes)) {
		return -EIO;
	}

	/*
	 * Response byte 0 is always the number of bytes read.
	 * Remaining bytes are read starting at offset.
	 * Note that the byte following the number of bytes is
	 * considered to be at offset 0.
	 */
	if (bytes == 0) {
		*val = bytes;
	} else {
		*val = data->reg_val[reg][bytes];
	}

	return 0;
}

static int tps6699x_emul_finish_read(const struct emul *emul, int reg,
				     int bytes)
{
	LOG_DBG("finish_read reg=%#x, bytes=%d", reg, bytes);

	/* TODO(b/345292002): Actually handle register accesses. */

	return 0;
}

static int tps6699x_emul_access_reg(const struct emul *emul, int reg, int bytes,
				    bool read)
{
	return reg;
}

static int emul_tps6699x_set_response_delay(const struct emul *target,
					    uint32_t delay_ms)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	LOG_INF("set_response_delay delay_ms=%d", delay_ms);
	data->delay_ms = delay_ms;

	return 0;
}

static int tps6699x_emul_init(const struct emul *emul,
			      const struct device *parent)
{
	return 0;
}

static int tps6699x_emul_idle_wait(const struct emul *emul)
{
	return 0;
}

static struct emul_pdc_api_t emul_tps6699x_api = {
	.reset = NULL,
	.set_response_delay = emul_tps6699x_set_response_delay,
	.get_connector_reset = emul_tps6699x_get_connector_reset,
	.set_capability = NULL,
	.set_connector_capability = NULL,
	.set_error_status = NULL,
	.set_connector_status = NULL,
	.get_uor = NULL,
	.get_pdr = NULL,
	.get_requested_power_level = NULL,
	.get_ccom = NULL,
	.get_drp_mode = NULL,
	.get_sink_path = NULL,
	.get_reconnect_req = NULL,
	.pulse_irq = NULL,
	.set_info = NULL,
	.set_lpm_ppm_info = NULL,
	.set_pdos = NULL,
	.get_pdos = NULL,
	.get_cable_property = NULL,
	.set_cable_property = NULL,
	.idle_wait = tps6699x_emul_idle_wait,
};

/* clang-format off */
#define TPS6699X_EMUL_DEFINE(n) \
	static struct tps6699x_emul_data tps6699x_emul_data_##n = { \
		.common = { \
			.start_write = tps6699x_emul_start_write, \
			.write_byte = tps6699x_emul_write_byte, \
			.finish_write = tps6699x_emul_finish_write, \
			.start_read = tps6699x_emul_start_read, \
			.read_byte = tps6699x_emul_read_byte, \
			.finish_read = tps6699x_emul_finish_read, \
			.access_reg = tps6699x_emul_access_reg, \
		}, \
		.pdc_data = { \
			.irq_gpios = GPIO_DT_SPEC_INST_GET(n, irq_gpios), \
		}, \
		.port = USBC_PORT_FROM_DRIVER_NODE(DT_DRV_INST(n), pdc), \
	}; \
	static const struct i2c_common_emul_cfg tps6699x_emul_cfg_##n = { \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
		.data = &tps6699x_emul_data_##n.common, \
		.addr = DT_INST_REG_ADDR(n), \
	}; \
	EMUL_DT_INST_DEFINE(n, tps6699x_emul_init, &tps6699x_emul_data_##n, \
			    &tps6699x_emul_cfg_##n, &i2c_common_emul_api, \
			    &emul_tps6699x_api)
/* clang-format on */

DT_INST_FOREACH_STATUS_OKAY(TPS6699X_EMUL_DEFINE)
