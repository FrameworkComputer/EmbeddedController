/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_pdc.h"
#include "emul/emul_pdc_pdo.h"
#include "emul/emul_tps6699x.h"
#include "tps6699x_reg.h"
#include "usbc/utils.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT ti_tps6699_pdc

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(tps6699x_emul);

/* TODO(b/349609367): Do not rely on this test-only driver function. */
bool pdc_tps6699x_test_idle_wait(void);

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
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;
	data->response.data.capability = data->capability;

	memcpy(data->reg_val[REG_DATA_FOR_CMD1], &data->response,
	       sizeof(data->response));
}

static void
tps699x_emul_get_connector_capability(struct tps6699x_emul_pdc_data *data)
{
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;
	data->response.data.connector_capability = data->connector_capability;

	memcpy(data->reg_val[REG_DATA_FOR_CMD1], &data->response,
	       sizeof(data->response));
}

static void tps699x_emul_get_error_status(struct tps6699x_emul_pdc_data *data)
{
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;
	data->response.data.length = sizeof(data->error);
	data->response.data.error = data->error;

	memcpy(&data->reg_val[REG_DATA_FOR_CMD1], &data->response,
	       sizeof(data->response));
}

static void
tps699x_emul_get_connector_status(struct tps6699x_emul_pdc_data *data)
{
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;
	data->response.data.connector_status = data->connector_status;

	memcpy(data->reg_val[REG_DATA_FOR_CMD1], &data->response,
	       sizeof(data->response));

	/* TPS6699x clears the connector status change on read. */
	data->connector_status.raw_conn_status_change_bits = 0;
}

static void tps699x_emul_set_uor(struct tps6699x_emul_pdc_data *data,
				 const union uor_t *uor)
{
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;

	data->uor = *uor;
	LOG_INF("UOR=0x%x", data->uor.raw_value);
}

static void tps699x_emul_set_pdr(struct tps6699x_emul_pdc_data *data,
				 const union pdr_t *pdr)
{
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;

	data->pdr = *pdr;
}

static void tps699x_emul_set_ccom(struct tps6699x_emul_pdc_data *data,
				  const void *in)
{
	const struct ti_ccom *ccom = in;
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;

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

static void tps699x_emul_get_pdos(struct tps6699x_emul_pdc_data *data,
				  const void *in)
{
	const struct ti_get_pdos *req = in;
	enum pdo_type_t pdo_type = req->source ? SOURCE_PDO : SINK_PDO;
	enum pdo_offset_t pdo_offset = req->pdo_offset;
	uint8_t pdo_count =
		MIN(PDO_OFFSET_MAX - req->pdo_offset, req->num_pdos + 1);

	LOG_INF("GET_PDO type=%d, offset=%d, count=%d, partner_pdo=%d",
		pdo_type, pdo_offset, pdo_count, req->partner_pdo);

	emul_pdc_pdo_get_direct(&data->pdo, pdo_type, pdo_offset, pdo_count,
				req->partner_pdo, data->response.data.pdos);

	data->response.data.length = pdo_count * 4;
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;

	memcpy(&data->reg_val[REG_DATA_FOR_CMD1], &data->response,
	       sizeof(data->response));
}

static void tps699x_emul_get_cable_property(struct tps6699x_emul_pdc_data *data)
{
	data->response.result = TASK_COMPLETED_SUCCESSFULLY;
	data->response.data.cable_property = data->cable_property;

	/* UCSI v2 cable response is 5 bytes + 1 byte TI return code */
	memcpy(&data->reg_val[REG_DATA_FOR_CMD1], &data->response, 5 + 1);
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
	case UCSI_GET_PDOS:
		tps699x_emul_get_pdos(data, &data_reg[2]);
		break;
	case UCSI_GET_CABLE_PROPERTY:
		tps699x_emul_get_cable_property(data);
		break;
	case UCSI_READ_POWER_LEVEL:
		break;
	default:
		LOG_WRN("tps6699x_emul: Unimplemented UCSI command %#04x", cmd);
	};

	/* By default, indicate task success.
	 * TODO(b/345292002): Allow a test to emulate task failure.
	 */
	data_reg[0] = TASK_COMPLETED_SUCCESSFULLY;
}

static void tps6699x_emul_handle_srdy(struct tps6699x_emul_pdc_data *data,
				      uint8_t *data_reg)
{
	struct ti_task_srdy *srdy = (struct ti_task_srdy *)data_reg;
	union reg_power_path_status *power_path_status =
		(union reg_power_path_status *)
			data->reg_val[REG_POWER_PATH_STATUS];

	LOG_INF("SRDY TASK");

	switch (srdy->switch_select) {
	case PP_EXT1:
	case PP_EXT2:
		power_path_status->pa_ext_vbus_sw =
			EXT_VBUS_SWITCH_ENABLED_INPUT;
		power_path_status->pb_ext_vbus_sw =
			EXT_VBUS_SWITCH_ENABLED_INPUT;
		break;
	default:
		break;
	}
	data_reg[0] = TASK_COMPLETED_SUCCESSFULLY;
}

static void tps6699x_emul_handle_sryr(struct tps6699x_emul_pdc_data *data,
				      uint8_t *data_reg)
{
	union reg_power_path_status *power_path_status =
		(union reg_power_path_status *)
			data->reg_val[REG_POWER_PATH_STATUS];

	LOG_INF("SRYR TASK");
	power_path_status->pa_ext_vbus_sw = EXT_VBUS_SWITCH_DISABLED;
	power_path_status->pb_ext_vbus_sw = EXT_VBUS_SWITCH_DISABLED;
	data_reg[0] = TASK_COMPLETED_SUCCESSFULLY;
}

static void tps6699x_emul_handle_aneg(struct tps6699x_emul_pdc_data *data,
				      uint8_t *data_reg)
{
	LOG_INF("ANEg TASK");
	data_reg[0] = TASK_COMPLETED_SUCCESSFULLY;
}

static void delayable_work_handler(struct k_work *w)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(w);
	struct tps6699x_emul_pdc_data *data =
		CONTAINER_OF(dwork, struct tps6699x_emul_pdc_data, delay_work);
	enum command_task *cmd_reg =
		(enum command_task *)&data->reg_val[REG_COMMAND_FOR_I2C1];

	*cmd_reg = COMMAND_TASK_COMPLETE;
}

static void tps6699x_emul_handle_command(struct tps6699x_emul_pdc_data *data,
					 enum command_task task,
					 uint8_t *data_reg)
{
	enum command_task *cmd_reg =
		(enum command_task *)&data->reg_val[REG_COMMAND_FOR_I2C1];

	/* TODO(b/345292002): Respond to commands asynchronously. */

	switch (task) {
	case COMMAND_TASK_UCSI:
		tps6699x_emul_handle_ucsi(data, data_reg);
		break;
	case COMMAND_TASK_SRDY:
		tps6699x_emul_handle_srdy(data, data_reg);
		break;
	case COMMAND_TASK_SRYR:
		tps6699x_emul_handle_sryr(data, data_reg);
		break;
	case COMMAND_TASK_ANEG:
		tps6699x_emul_handle_aneg(data, data_reg);
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
	if (data->delay_ms > 0) {
		k_work_schedule(&data->delay_work, K_MSEC(data->delay_ms));
	} else {
		*cmd_reg = COMMAND_TASK_COMPLETE;
	}
}

static void tps6699x_emul_handle_write(struct tps6699x_emul_pdc_data *data,
				       int reg)
{
	switch (reg) {
		/* Some registers trigger an action on write. */
	case REG_COMMAND_FOR_I2C1:
		tps6699x_emul_handle_command(
			data,
			*(enum command_task *)
				 data->reg_val[REG_COMMAND_FOR_I2C1],
			data->reg_val[REG_DATA_FOR_CMD1]);
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
	union reg_interrupt *reg_interrupt =
		(union reg_interrupt *)
			data->reg_val[REG_INTERRUPT_EVENT_FOR_I2C1];
	union reg_adc_results *adc_results =
		(union reg_adc_results *)data->reg_val[REG_ADC_RESULTS];

	union reg_received_identity_data_object *received_identity_data_object;
	uint16_t voltage;

	data->connector_status = *connector_status;

	reg_interrupt->ucsi_connector_status_change_notification = 1;

	voltage = data->connector_status.voltage_reading *
		  data->connector_status.voltage_scale * 5;
	LOG_INF("Setting adc_results %u", voltage);
	adc_results->pa_vbus = voltage;
	adc_results->pb_vbus = voltage;

	if (data->connector_status.connect_status &&
	    data->connector_status.conn_partner_flags &
		    CONNECTOR_PARTNER_PD_CAPABLE) {
		received_identity_data_object =
			(union reg_received_identity_data_object *)data
				->reg_val[REG_RECEIVED_SOP_IDENTITY_DATA_OBJECT];
		received_identity_data_object->response_type = 1;
		received_identity_data_object =
			(union reg_received_identity_data_object *)data->reg_val
				[REG_RECEIVED_SOP_PRIME_IDENTITY_DATA_OBJECT];
		received_identity_data_object->response_type = 1;
	} else {
		received_identity_data_object =
			(union reg_received_identity_data_object *)data
				->reg_val[REG_RECEIVED_SOP_IDENTITY_DATA_OBJECT];
		received_identity_data_object->response_type = 0;
		received_identity_data_object =
			(union reg_received_identity_data_object *)data->reg_val
				[REG_RECEIVED_SOP_PRIME_IDENTITY_DATA_OBJECT];
		received_identity_data_object->response_type = 0;
	}
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
		(const union reg_port_control *)data->reg_val[REG_PORT_CONTROL];
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
			data->reg_val[REG_PORT_CONFIGURATION];

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

static int emul_tps6699x_get_sink_path(const struct emul *target, bool *en)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	const union reg_power_path_status *power_path_status =
		(const union reg_power_path_status *)
			data->reg_val[REG_POWER_PATH_STATUS];

	*en = (power_path_status->pa_ext_vbus_sw ==
		       EXT_VBUS_SWITCH_ENABLED_INPUT ||
	       power_path_status->pb_ext_vbus_sw ==
		       EXT_VBUS_SWITCH_ENABLED_INPUT);

	return 0;
}

static int emul_tps6699x_set_info(const struct emul *target,
				  const struct pdc_info_t *info)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	union reg_version *reg_version =
		(union reg_version *)data->reg_val[REG_VERSION];
	union reg_tx_identity *reg_tx_identity =
		(union reg_tx_identity *)data->reg_val[REG_TX_IDENTITY];
	union reg_customer_use *reg_customer_use =
		(union reg_customer_use *)data->reg_val[REG_CUSTOMER_USE];
	union reg_mode *reg_mode = (union reg_mode *)data->reg_val[REG_MODE];

	reg_version->version = info->fw_version;
	*((uint16_t *)reg_tx_identity->vendor_id) = info->vid_pid >> 16;
	*((uint16_t *)reg_tx_identity->product_id) = info->vid_pid & 0xFFFF;
	memset(reg_customer_use->data, 0, sizeof(reg_customer_use->data));
	memcpy(reg_customer_use->data, info->project_name,
	       MIN(sizeof(reg_customer_use->data), strlen(info->project_name)));
	*((uint32_t *)reg_mode->data) =
		(info->is_running_flash_code ? REG_MODE_APP0 : 0);

	return 0;
}

static int emul_tps6699x_get_cable_property(const struct emul *target,
					    union cable_property_t *property)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);
	*property = data->cable_property;
	return 0;
}

static int
emul_tps6699x_set_cable_property(const struct emul *target,
				 const union cable_property_t property)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);
	data->cable_property = property;
	return 0;
}

static int emul_tps6699x_reset(const struct emul *target)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);

	memset(data->reg_val, 0, sizeof(data->reg_val));

	/* Reset PDOs. */
	emul_pdc_pdo_reset(&data->pdo);

	return 0;
}

static int emul_tps6699x_pulse_irq(const struct emul *target)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);
	union reg_interrupt *reg_interrupt =
		(union reg_interrupt *)
			data->reg_val[REG_INTERRUPT_EVENT_FOR_I2C1];

	reg_interrupt->plug_insert_or_removal = 1;
	gpio_emul_input_set(data->irq_gpios.port, data->irq_gpios.pin, 1);
	gpio_emul_input_set(data->irq_gpios.port, data->irq_gpios.pin, 0);

	return 0;
}

static int emul_tps6699x_get_pdos(const struct emul *target,
				  enum pdo_type_t pdo_type,
				  enum pdo_offset_t pdo_offset,
				  uint8_t num_pdos, enum pdo_source_t source,
				  uint32_t *pdos)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);
	return emul_pdc_pdo_get_direct(&data->pdo, pdo_type, pdo_offset,
				       num_pdos, source, pdos);
}

static int emul_tps6699x_set_pdos(const struct emul *target,
				  enum pdo_type_t pdo_type,
				  enum pdo_offset_t pdo_offset,
				  uint8_t num_pdos, enum pdo_source_t source,
				  const uint32_t *pdos)
{
	struct tps6699x_emul_pdc_data *data =
		tps6699x_emul_get_pdc_data(target);
	return emul_pdc_pdo_set_direct(&data->pdo, pdo_type, pdo_offset,
				       num_pdos, source, pdos);
}

static int tps6699x_emul_init(const struct emul *emul,
			      const struct device *parent)
{
	struct tps6699x_emul_data *data = emul->data;
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	LOG_INF("TPS669X emul init");

	data->common.i2c = parent;
	data->common.cfg = cfg;

	i2c_common_emul_init(&data->common);
	k_work_init_delayable(&data->pdc_data.delay_work,
			      delayable_work_handler);

	return 0;
}

static int tps6699x_emul_idle_wait(const struct emul *emul)
{
	/* TODO(b/349609367): This should be handled entirely in the emulator,
	 * not in the driver, and it should be specific to the passed-in target.
	 */

	ARG_UNUSED(emul);

	if (pdc_tps6699x_test_idle_wait())
		return 0;
	return -ETIMEDOUT;
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
	.get_sink_path = emul_tps6699x_get_sink_path,
	.get_reconnect_req = NULL,
	.pulse_irq = emul_tps6699x_pulse_irq,
	.set_info = emul_tps6699x_set_info,
	.set_lpm_ppm_info = NULL,
	.set_pdos = emul_tps6699x_set_pdos,
	.get_pdos = emul_tps6699x_get_pdos,
	.get_cable_property = emul_tps6699x_get_cable_property,
	.set_cable_property = emul_tps6699x_set_cable_property,
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
