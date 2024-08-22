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

static void tps699x_emul_get_capability(struct tps6699x_emul_pdc_data *data)
{
	data->response.result = COMMAND_RESULT_SUCCESS;
	data->response.data.capability = data->capability;

	memcpy(data->reg_val[TPS6699X_REG_DATA_I2C1], &data->response,
	       sizeof(data->response));
}

static void
tps699x_emul_get_connector_capability(struct tps6699x_emul_pdc_data *data)
{
	data->response.result = COMMAND_RESULT_SUCCESS;
	data->response.data.connector_capability = data->connector_capability;

	memcpy(data->reg_val[TPS6699X_REG_DATA_I2C1], &data->response,
	       sizeof(data->response));
}

static void tps699x_emul_get_error_status(struct tps6699x_emul_pdc_data *data)
{
	data->response.result = COMMAND_RESULT_SUCCESS;
	data->response.data.length = sizeof(data->error);
	data->response.data.error = data->error;

	memcpy(&data->reg_val[TPS6699X_REG_DATA_I2C1], &data->response,
	       sizeof(data->response));
}

static void
tps699x_emul_get_connector_status(struct tps6699x_emul_pdc_data *data)
{
	data->response.result = COMMAND_RESULT_SUCCESS;
	data->response.data.connector_status = data->connector_status;

	memcpy(data->reg_val[TPS6699X_REG_DATA_I2C1], &data->response,
	       sizeof(data->response));

	/* TPS6699x clears the connector status change on read. */
	data->connector_status.raw_conn_status_change_bits = 0;
}

static void tps699x_emul_set_uor(struct tps6699x_emul_pdc_data *data,
				 const union uor_t *uor)
{
	data->response.result = COMMAND_RESULT_SUCCESS;

	data->uor = *uor;
	LOG_INF("UOR=0x%x", data->uor.raw_value);
}

static void tps699x_emul_set_pdr(struct tps6699x_emul_pdc_data *data,
				 const union pdr_t *pdr)
{
	data->response.result = COMMAND_RESULT_SUCCESS;

	data->pdr = *pdr;
}

static void tps699x_emul_set_ccom(struct tps6699x_emul_pdc_data *data,
				  const void *in)
{
	const struct ti_ccom *ccom = in;
	data->response.result = COMMAND_RESULT_SUCCESS;

	switch (ccom->cc_operation_mode) {
	case 1:
		data->ccom = CCOM_RP;
		break;
	case 2:
		data->ccom = CCOM_RD;
		break;
	case 4:
		data->ccom = CCOM_DRP;
		break;
	default:
		LOG_ERR("Unexpected ccom = %u", ccom->cc_operation_mode);
		break;
	}
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

	LOG_INF("UCSI command 0x%X", cmd);
	switch (cmd) {
	case UCSI_GET_CAPABILITY:
		tps699x_emul_get_capability(data);
		break;
	case UCSI_GET_CONNECTOR_CAPABILITY:
		tps699x_emul_get_connector_capability(data);
		break;
	case UCSI_GET_ERROR_STATUS:
		tps699x_emul_get_error_status(data);
		break;
	case UCSI_GET_CONNECTOR_STATUS:
		tps699x_emul_get_connector_status(data);
		break;
	case UCSI_CONNECTOR_RESET:
		tps6699x_emul_connector_reset(
			data, (union connector_reset_t)data_reg[2]);
		break;
	case UCSI_SET_UOR:
		tps699x_emul_set_uor(data, (union uor_t *)&data_reg[2]);
		break;
	case UCSI_SET_PDR:
		tps699x_emul_set_pdr(data, (union pdr_t *)&data_reg[2]);
		break;
	case UCSI_SET_CCOM:
		tps699x_emul_set_ccom(data, &data_reg[2]);
		break;
	default:
		LOG_WRN("tps6699x_emul: Unimplemented UCSI command %#04x", cmd);
	};

	/* By default, indicate task success.
	 * TODO(b/345292002): Allow a test to emulate task failure.
	 */
	data_reg[0] = COMMAND_RESULT_SUCCESS;
}

static void tps6699x_emul_handle_command(struct tps6699x_emul_pdc_data *data,
					 enum tps6699x_command_task task,
					 uint8_t *data_reg)
{
	enum tps6699x_command_task *cmd_reg =
		(enum tps6699x_command_task *)&data
			->reg_val[TPS6699X_REG_COMMAND_I2C1];

	/* TODO(b/345292002): Respond to commands asynchronously. */

	switch (task) {
	case COMMAND_TASK_UCSI:
		tps6699x_emul_handle_ucsi(data, data_reg);
		break;
	case COMMAND_TASK_SRYR:
	case COMMAND_TASK_SRDY:
		/* TODO(b/345292002) - Actually implement support for these. */
		*data_reg = COMMAND_TASK_COMPLETE;
		break;
	default: {
		char task_str[5] = {
			((char *)&task)[0],
			((char *)&task)[1],
			((char *)&task)[2],
			((char *)&task)[3],
			'\0',
		};

		LOG_WRN("emul_tps6699x: Unimplemented task %s", task_str);
		/* Indicate an error to the PPM. */
		*cmd_reg = COMMAND_TASK_NO_COMMAND;
		return;
	}
	}

	*cmd_reg = COMMAND_TASK_COMPLETE;
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

	data->reg_addr = reg;

	return 0;
}

static int tps6699x_emul_write_byte(const struct emul *emul, int reg,
				    uint8_t val, int bytes)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);
	/* Byte 0 of a write is the register address. Byte 1 (if present) is the
	 * number of bytes to be written.
	 */
	const int data_bytes = bytes - 2;

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

	/* No need to validate inputs; this function will only be called if
	 * write_byte validated its inputs and succeeded.
	 */

	/* A 1-byte write only contains a register offset and is used to
	 * initiate a read of that register. Do not treat it as a write to that
	 * register.
	 */
	if (bytes > 1) {
		const int data_bytes = bytes - 2;
		const int rem_bytes = TPS6699X_REG_SIZE - data_bytes;

		__ASSERT(rem_bytes >= 0, "write size exceeds register size");
		memset(&data->reg_val[reg][data_bytes], 0, rem_bytes);

		LOG_DBG("finish_write reg=%#x, bytes=%d+2", reg, data_bytes);
		tps6699x_emul_handle_write(data, reg);
	}

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

	/*
	 * Response byte 0 is always the number of bytes in the register.
	 * Remaining bytes are read starting at offset. Note that the byte
	 * following the number of bytes is considered to be at offset 0.
	 */
	if (bytes == 0) {
		*val = sizeof(data->reg_val[reg]);
		data->transaction_bytes = *val;

	} else {
		const int data_bytes = bytes - 1;

		if (!register_access_is_valid(data, reg, data_bytes)) {
			return -EIO;
		}
		*val = data->reg_val[reg][data_bytes];
	}

	return 0;
}

static int tps6699x_emul_finish_read(const struct emul *emul, int reg,
				     int bytes)
{
	const int data_bytes = bytes - 1;

	LOG_DBG("finish_read reg=%#x, bytes=%d", reg, data_bytes);

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

static int emul_tps6699x_set_capability(const struct emul *target,
					const struct capability_t *caps)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	data->capability = *caps;

	return 0;
}

static int
emul_tps6699x_set_connector_capability(const struct emul *target,
				       const union connector_capability_t *caps)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	data->connector_capability = *caps;

	return 0;
}

static int emul_tps6699x_set_error_status(const struct emul *target,
					  const union error_status_t *es)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	data->error = *es;

	return 0;
}

static int emul_tps6699x_set_connector_status(
	const struct emul *target,
	const union connector_status_t *connector_status)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);
	union reg_adc_results *adc_results =
		(union reg_adc_results *)data->reg_val[TPS6699X_REG_ADC_RESULTS];
	uint16_t voltage;

	data->connector_status = *connector_status;

	voltage = data->connector_status.voltage_reading *
		  data->connector_status.voltage_scale * 5;
	LOG_INF("Setting adc_results %u", voltage);
	adc_results->pa_vbus = voltage;
	adc_results->pb_vbus = voltage;

	return 0;
}

static int emul_tps6699x_get_uor(const struct emul *target, union uor_t *uor)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	*uor = data->uor;

	return 0;
}

static int emul_tps6699x_get_pdr(const struct emul *target, union pdr_t *pdr)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	*pdr = data->pdr;

	return 0;
}

static int
emul_tps6699x_get_requested_power_level(const struct emul *target,
					enum usb_typec_current_t *tcc)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);
	const union reg_port_control *pdc_port_control =
		(const union reg_port_control *)
			data->reg_val[TPS6699X_REG_PORT_CONTROL];
	const enum usb_typec_current_t convert[] = {
		TC_CURRENT_USB_DEFAULT,
		TC_CURRENT_1_5A,
		TC_CURRENT_3_0A,
	};

	if (pdc_port_control->typec_current >= ARRAY_SIZE(convert)) {
		return -EINVAL;
	}

	/* Convert back to EC type */
	*tcc = convert[pdc_port_control->typec_current];

	return 0;
}

static int emul_tps6699x_get_ccom(const struct emul *target, enum ccom_t *ccom)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	*ccom = data->ccom;

	return 0;
}

static int emul_tps6699x_get_drp_mode(const struct emul *target,
				      enum drp_mode_t *dm)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	const union reg_port_configuration *pdc_port_cfg =
		(const union reg_port_configuration *)
			data->reg_val[TPS6699X_REG_PORT_CONFIGURATION];

	*dm = pdc_port_cfg->typec_support_options;

	return 0;
}

static int emul_tps6699x_get_supported_drp_modes(const struct emul *target,
						 enum drp_mode_t *dm,
						 uint8_t size, uint8_t *num)
{
	enum drp_mode_t supported[] = { DRP_NORMAL, DRP_TRY_SRC };

	memcpy(dm, supported,
	       sizeof(enum drp_mode_t) * MIN(size, ARRAY_SIZE(supported)));

	*num = ARRAY_SIZE(supported);

	return 0;
}

static int emul_tps6699x_reset(const struct emul *target)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	/* Reset PDOs. */
	memset(data->src_pdos, 0x0, sizeof(data->src_pdos));
	memset(data->snk_pdos, 0x0, sizeof(data->snk_pdos));
	memset(data->partner_src_pdos, 0x0, sizeof(data->partner_src_pdos));
	memset(data->partner_snk_pdos, 0x0, sizeof(data->partner_snk_pdos));

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
	.reset = emul_tps6699x_reset,
	.set_response_delay = emul_tps6699x_set_response_delay,
	.get_connector_reset = emul_tps6699x_get_connector_reset,
	.set_capability = emul_tps6699x_set_capability,
	.set_connector_capability = emul_tps6699x_set_connector_capability,
	.set_error_status = emul_tps6699x_set_error_status,
	.set_connector_status = emul_tps6699x_set_connector_status,
	.get_uor = emul_tps6699x_get_uor,
	.get_pdr = emul_tps6699x_get_pdr,
	.get_requested_power_level = emul_tps6699x_get_requested_power_level,
	.get_ccom = emul_tps6699x_get_ccom,
	.get_drp_mode = emul_tps6699x_get_drp_mode,
	.get_supported_drp_modes = emul_tps6699x_get_supported_drp_modes,
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
