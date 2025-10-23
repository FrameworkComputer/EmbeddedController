/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TI TPS6699X Power Delivery Controller Driver
 */

#include "tps6699x_reg.h"
#include "usbc/utils.h"

#include <assert.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(tps6699x, CONFIG_USBC_LOG_LEVEL);
#include <zephyr/smf.h>

#include <drivers/pdc.h>

static int tps_read_reg(const struct i2c_dt_spec *i2c, enum tps6699x_reg reg,
			uint8_t *buf, uint8_t len)
{
	uint8_t byte_cnt;

	/* TPS Read Protocol
	 *   1. Write of register to be read
	 *   2. Read byte count
	 *   3. Read register contents
	 */
	struct i2c_msg msg[] = {
		{
			.buf = (uint8_t *)&reg,
			.len = 1,
			.flags = I2C_MSG_WRITE,
		},
		{
			.buf = &byte_cnt,
			.len = 1,
			.flags = I2C_MSG_READ | I2C_MSG_RESTART,
		},
		{
			.buf = buf,
			.len = len,
			.flags = I2C_MSG_READ | I2C_MSG_STOP,
		},
	};

	return i2c_transfer_dt(i2c, msg, ARRAY_SIZE(msg));
}

static int tps_write_reg(const struct i2c_dt_spec *i2c, enum tps6699x_reg reg,
			 uint8_t *buf, uint8_t len)
{
	/* TPS Write Protocol
	 *   1. Write Register
	 *   2. Write Byte Count
	 *   3. Write data
	 */
	struct i2c_msg msg[] = {
		{
			.buf = (uint8_t *)&reg,
			.len = 1,
			.flags = I2C_MSG_WRITE,
		},
		{
			.buf = &len,
			.len = 1,
			.flags = I2C_MSG_WRITE,
		},
		{
			.buf = buf,
			.len = len,
			.flags = I2C_MSG_WRITE | I2C_MSG_STOP,
		},
	};

	return i2c_transfer_dt(i2c, msg, ARRAY_SIZE(msg));
}

test_mockable_static int tps_xfer_reg(const struct i2c_dt_spec *i2c,
				      enum tps6699x_reg reg, uint8_t *buf,
				      uint8_t len, int flag)
{
	if (!i2c || !buf || (len == 0)) {
		return -EINVAL;
	}

	if (flag == I2C_MSG_READ) {
		return tps_read_reg(i2c, reg, buf, len);
	} else {
		return tps_write_reg(i2c, reg, buf, len);
	}
}

int tps_rd_mode(const struct i2c_dt_spec *i2c, union reg_mode *buf)
{
	return tps_xfer_reg(i2c, REG_MODE, buf->raw_value,
			    sizeof(union reg_mode), I2C_MSG_READ);
}

int tps_rw_tx_identity(const struct i2c_dt_spec *i2c,
		       union reg_tx_identity *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_TX_IDENTITY, buf->raw_value,
			    sizeof(union reg_tx_identity), flag);
}

int tps_rw_customer_use(const struct i2c_dt_spec *i2c,
			union reg_customer_use *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_CUSTOMER_USE, buf->raw_value,
			    sizeof(union reg_customer_use), flag);
}

int tps_rw_command_for_i2c1(const struct i2c_dt_spec *i2c,
			    union reg_command *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_COMMAND_FOR_I2C1, buf->raw_value,
			    sizeof(union reg_command), flag);
}

int tps_rw_data_for_cmd1(const struct i2c_dt_spec *i2c, union reg_data *buf,
			 int flag)
{
	return tps_xfer_reg(i2c, REG_DATA_FOR_CMD1, buf->raw_value,
			    sizeof(union reg_data), flag);
}

int tps_rd_version(const struct i2c_dt_spec *i2c, union reg_version *buf)
{
	return tps_xfer_reg(i2c, REG_VERSION, buf->raw_value,
			    sizeof(union reg_version), I2C_MSG_READ);
}

int tps_rd_interrupt_event(const struct i2c_dt_spec *i2c,
			   union reg_interrupt *buf)
{
	return tps_xfer_reg(i2c, REG_INTERRUPT_EVENT_FOR_I2C1, buf->raw_value,
			    sizeof(union reg_interrupt), I2C_MSG_READ);
}

int tps_rw_interrupt_mask(const struct i2c_dt_spec *i2c,
			  union reg_interrupt *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_INTERRUPT_MASK_FOR_I2C1, buf->raw_value,
			    sizeof(union reg_interrupt), flag);
}

int tps_rw_interrupt_clear(const struct i2c_dt_spec *i2c,
			   union reg_interrupt *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_INTERRUPT_CLEAR_FOR_I2C1, buf->raw_value,
			    sizeof(union reg_interrupt), flag);
}

int tps_rd_status(const struct i2c_dt_spec *i2c, union reg_status *buf)
{
	return tps_xfer_reg(i2c, REG_STATUS, buf->raw_value,
			    sizeof(union reg_status), I2C_MSG_READ);
}

int tps_rw_port_configuration(const struct i2c_dt_spec *i2c,
			      union reg_port_configuration *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_PORT_CONFIGURATION, buf->raw_value,
			    sizeof(union reg_port_configuration), flag);
}

test_mockable int tps_rw_port_control(const struct i2c_dt_spec *i2c,
				      union reg_port_control *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_PORT_CONTROL, buf->raw_value,
			    sizeof(union reg_port_control), flag);
}

int tps_rd_boot_flags(const struct i2c_dt_spec *i2c, union reg_boot_flags *buf)
{
	return tps_xfer_reg(i2c, REG_BOOT_FLAG, buf->raw_value,
			    sizeof(union reg_boot_flags), I2C_MSG_READ);
}

int tps_rw_transmit_source_capabilities(
	const struct i2c_dt_spec *i2c,
	union reg_transmit_source_capabilities *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_TRANSMIT_SOURCE_CAPABILITES,
			    buf->raw_value,
			    sizeof(union reg_transmit_source_capabilities),
			    flag);
}

int tps_rw_transmit_sink_capabilities(const struct i2c_dt_spec *i2c,
				      union reg_transmit_sink_capabilities *buf,
				      int flag)
{
	return tps_xfer_reg(i2c, REG_TRANSMIT_SINK_CAPABILITES, buf->raw_value,
			    sizeof(union reg_transmit_sink_capabilities), flag);
}

int tps_rw_sx_app_config(const struct i2c_dt_spec *i2c,
			 union reg_sx_app_config *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_SET_SX_APP_CONFIG, buf->raw_value,
			    sizeof(union reg_sx_app_config), flag);
}

int tps_rd_active_rdo_contract(const struct i2c_dt_spec *i2c,
			       union reg_active_rdo_contract *buf)
{
	return tps_xfer_reg(i2c, REG_ACTIVE_RDO_CONTRACT, buf->raw_value,
			    sizeof(union reg_active_rdo_contract),
			    I2C_MSG_READ);
}

int tps_rd_active_pdo_contract(const struct i2c_dt_spec *i2c,
			       union reg_active_pdo_contract *buf)
{
	return tps_xfer_reg(i2c, REG_ACTIVE_PDO_CONTRACT, buf->raw_value,
			    sizeof(union reg_active_pdo_contract),
			    I2C_MSG_READ);
}

int tps_rd_adc_results(const struct i2c_dt_spec *i2c,
		       union reg_adc_results *buf)
{
	return tps_xfer_reg(i2c, REG_ADC_RESULTS, buf->raw_value,
			    sizeof(union reg_adc_results), I2C_MSG_READ);
}

int tps_rw_autonegotiate_sink(const struct i2c_dt_spec *i2c,
			      union reg_autonegotiate_sink *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_AUTONEGOTIATE_SINK, buf->raw_value,
			    sizeof(union reg_autonegotiate_sink), flag);
}

int tps_rw_thunderbolt_configuration(const struct i2c_dt_spec *i2c,
				     union reg_thunderbolt_configuration *buf,
				     int flag)
{
	return tps_xfer_reg(i2c, REG_THUNDERBOLT_CONFIGURATION, buf->raw_value,
			    sizeof(union reg_thunderbolt_configuration), flag);
}

int tps_rd_power_path_status(const struct i2c_dt_spec *i2c,
			     union reg_power_path_status *buf)
{
	return tps_xfer_reg(i2c, REG_POWER_PATH_STATUS, buf->raw_value,
			    sizeof(union reg_power_path_status), I2C_MSG_READ);
}

int tps_rd_received_sop_identity_data_object(
	const struct i2c_dt_spec *i2c,
	union reg_received_identity_data_object *buf)
{
	return tps_xfer_reg(i2c, REG_RECEIVED_SOP_IDENTITY_DATA_OBJECT,
			    buf->raw_value,
			    sizeof(union reg_received_identity_data_object),
			    I2C_MSG_READ);
}

int tps_rd_received_sop_prime_identity_data_object(
	const struct i2c_dt_spec *i2c,
	union reg_received_identity_data_object *buf)
{
	return tps_xfer_reg(i2c, REG_RECEIVED_SOP_PRIME_IDENTITY_DATA_OBJECT,
			    buf->raw_value,
			    sizeof(union reg_received_identity_data_object),
			    I2C_MSG_READ);
}

int tps_rd_data_status_reg(const struct i2c_dt_spec *i2c,
			   union reg_data_status *status)
{
	return tps_xfer_reg(i2c, REG_DATA_STATUS, status->raw_value,
			    sizeof(*status), I2C_MSG_READ);
}

int tps_rd_status_reg(const struct i2c_dt_spec *i2c, union reg_status *status)
{
	return tps_xfer_reg(i2c, REG_STATUS, status->raw_value, sizeof(*status),
			    I2C_MSG_READ);
}

#ifdef CONFIG_USBC_PDC_TPS6699X_CONSOLE_FW_UPDATER
/* LCOV_EXCL_START */

/** Split streaming transfers down into chunks of this size for more manageable
 *  I2C write lengths.
 */
#define TPS_STREAM_CHUNK_SIZE (64)

int tps_stream_data(const struct i2c_dt_spec *i2c,
		    const uint8_t broadcast_address, const uint8_t *buf,
		    size_t buf_len)
{
	struct i2c_msg msg[1];
	int rv;

	/* Create new i2c target for transfer. */
	const struct i2c_dt_spec stream_i2c = {
		.bus = i2c->bus,
		.addr = (uint16_t)broadcast_address,
	};

	/* Perform the transfer in chunks */
	for (int chunk_offset = 0; chunk_offset < buf_len;
	     chunk_offset += TPS_STREAM_CHUNK_SIZE) {
		/* Set up I2C write */
		msg[0].buf = (uint8_t *)buf + chunk_offset;
		msg[0].len = min(TPS_STREAM_CHUNK_SIZE, buf_len - chunk_offset);
		msg[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		rv = i2c_transfer_dt(&stream_i2c, msg, 1);
		if (rv) {
			LOG_ERR("Streaming data block failed (ret=%d, "
				"offset_into_block=%d, total_block_size=%u,"
				"chunk_size=%d)",
				rv, chunk_offset, buf_len,
				TPS_STREAM_CHUNK_SIZE);
			return rv;
		}
	}

	LOG_DBG("  Block complete (%u)", buf_len);
	return 0;
}
/* LCOV_EXCL_STOP */
#endif

int tps_rd_received_attention_vdm(
	const struct i2c_dt_spec *i2c,
	union reg_received_attention_vdm *received_attention_vdm)
{
	return tps_xfer_reg(i2c, REG_RECEIVED_ATTENTION_VDM,
			    received_attention_vdm->raw_value,
			    sizeof(union reg_received_attention_vdm),
			    I2C_MSG_READ);
}
