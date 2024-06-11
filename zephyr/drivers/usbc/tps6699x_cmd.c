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

static int tps_xfer_reg(const struct i2c_dt_spec *i2c, enum tps6699x_reg reg,
			uint8_t *buf, uint8_t len, int flag)
{
	struct i2c_msg msg[2];
	int msg_len;

	if (!i2c || !buf || (len == 0)) {
		return -EINVAL;
	}

	if (flag == I2C_MSG_READ) {
		msg[0].buf = (uint8_t *)&reg;
		msg[0].len = 1;
		msg[0].flags = I2C_MSG_WRITE;

		/* Skip reg value in buf[0], and and decrement
		 * length by 1. The PDC returns the number of bytes
		 * read in buf[1]. */
		msg[1].buf = &buf[1];
		msg[1].len = len - 1;
		msg[1].flags = I2C_MSG_READ | I2C_MSG_STOP | I2C_MSG_RESTART;

		/* 2 messages to send */
		msg_len = 2;
	} else {
		/* Set register to write */
		buf[0] = reg;
		/* Set number of bytes to write */
		buf[1] = len - 2;

		msg[0].buf = buf;
		msg[0].len = len;
		msg[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		/* msg[1] is not used */
		msg[1].buf = NULL;
		msg[1].len = 0;
		msg[1].flags = 0;

		/* 1 message to send */
		msg_len = 1;
	}

	return i2c_transfer_dt(i2c, &msg[0], msg_len);
}

int tps_rd_vendor_id(const struct i2c_dt_spec *i2c, union reg_vendor_id *buf)
{
	return tps_xfer_reg(i2c, REG_VENDOR_ID, buf->raw_value,
			    sizeof(union reg_vendor_id), I2C_MSG_READ);
}

int tps_rd_device_id(const struct i2c_dt_spec *i2c, union reg_device_id *buf)
{
	return tps_xfer_reg(i2c, REG_DEVICE_ID, buf->raw_value,
			    sizeof(union reg_device_id), I2C_MSG_READ);
}

int tps_rd_protocol_version(const struct i2c_dt_spec *i2c,
			    union reg_protocol_version *buf)
{
	return tps_xfer_reg(i2c, REG_PROTOCOL_VERSION, buf->raw_value,
			    sizeof(union reg_protocol_version), I2C_MSG_READ);
}

int tps_rd_mode(const struct i2c_dt_spec *i2c, union reg_mode *buf)
{
	return tps_xfer_reg(i2c, REG_MODE, buf->raw_value,
			    sizeof(union reg_mode), I2C_MSG_READ);
}

int tps_rd_uid(const struct i2c_dt_spec *i2c, union reg_uid *buf)
{
	return tps_xfer_reg(i2c, REG_UID, buf->raw_value, sizeof(union reg_uid),
			    I2C_MSG_READ);
}

int tps_rw_tx_identity(const struct i2c_dt_spec *i2c,
		       union reg_tx_identity *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_COMMAND_FOR_I2C1, buf->raw_value,
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

int tps_rw_command_for_i2c2(const struct i2c_dt_spec *i2c,
			    union reg_command *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_COMMAND_FOR_I2C2, buf->raw_value,
			    sizeof(union reg_command), flag);
}

int tps_rw_data_for_cmd2(const struct i2c_dt_spec *i2c, union reg_data *buf,
			 int flag)
{
	return tps_xfer_reg(i2c, REG_DATA_FOR_CMD2, buf->raw_value,
			    sizeof(union reg_data), flag);
}

int tps_rd_device_capabilities(const struct i2c_dt_spec *i2c,
			       union reg_device_capabilities *buf)
{
	return tps_xfer_reg(i2c, REG_DEVICE_CAPABILITIES, buf->raw_value,
			    sizeof(union reg_device_capabilities),
			    I2C_MSG_READ);
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

int tps_rd_discovered_svids(const struct i2c_dt_spec *i2c,
			    union reg_discovered_svids *buf)
{
	return tps_xfer_reg(i2c, REG_DISCOVERED_SVIDS, buf->raw_value,
			    sizeof(union reg_discovered_svids), I2C_MSG_READ);
}

int tps_rw_port_configuration(const struct i2c_dt_spec *i2c,
			      union reg_port_configuration *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_PORT_CONFIGURATION, buf->raw_value,
			    sizeof(union reg_port_configuration), flag);
}

int tps_rw_port_control(const struct i2c_dt_spec *i2c,
			union reg_port_control *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_PORT_CONTROL, buf->raw_value,
			    sizeof(union reg_port_control), flag);
}

int tps_rd_active_rdo_contract(const struct i2c_dt_spec *i2c,
			       union reg_active_rdo_contract *buf)
{
	return tps_xfer_reg(i2c, REG_ACTIVE_RDO_CONTRACT, buf->raw_value,
			    sizeof(union reg_active_rdo_contract),
			    I2C_MSG_READ);
}

int tps_rd_adc_results(const struct i2c_dt_spec *i2c,
		       union reg_adc_results *buf)
{
	return tps_xfer_reg(i2c, REG_ADC_RESULTS, buf->raw_value,
			    sizeof(union reg_adc_results), I2C_MSG_READ);
}

int tps_rd_pd_status(const struct i2c_dt_spec *i2c, union reg_pd_status *buf)
{
	return tps_xfer_reg(i2c, REG_PD_STATUS, buf->raw_value,
			    sizeof(union reg_pd_status), I2C_MSG_READ);
}

int tps_rd_received_source_capabilities(
	const struct i2c_dt_spec *i2c,
	union reg_received_source_capabilities *buf)
{
	return tps_xfer_reg(i2c, REG_RECEIVED_SOURCE_CAPABILITIES,
			    buf->raw_value,
			    sizeof(union reg_received_source_capabilities),
			    I2C_MSG_READ);
}

int tps_rw_autonegotiate_sink(const struct i2c_dt_spec *i2c,
			      union reg_autonegotiate_sink *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_AUTONEGOTIATE_SINK, buf->raw_value,
			    sizeof(union reg_autonegotiate_sink), flag);
}

int tps_rw_global_system_configuration(
	const struct i2c_dt_spec *i2c,
	union reg_global_system_configuration *buf, int flag)
{
	return tps_xfer_reg(i2c, REG_GLOBAL_SYSTEM_CONFIGURATION,
			    buf->raw_value,
			    sizeof(union reg_global_system_configuration),
			    flag);
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

int tps_rw_connection_manager_control(const struct i2c_dt_spec *i2c,
				      union reg_connection_manager_control *buf,
				      int flag)
{
	return tps_xfer_reg(i2c, REG_CONNECTION_MANAGER_CONTROL, buf->raw_value,
			    sizeof(union reg_connection_manager_control), flag);
}

int tps_rd_connection_manager_status(const struct i2c_dt_spec *i2c,
				     union reg_connection_manager_status *buf)
{
	return tps_xfer_reg(i2c, REG_CONNECTION_MANAGER_STATUS, buf->raw_value,
			    sizeof(union reg_connection_manager_status),
			    I2C_MSG_READ);
}

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
		msg[0].len = MIN(TPS_STREAM_CHUNK_SIZE, buf_len - chunk_offset);
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

		/* Periodically print a progress log message */
		if ((chunk_offset / TPS_STREAM_CHUNK_SIZE) % 32 == 0) {
			LOG_INF("  Block progress %u / %u", chunk_offset,
				buf_len);
		}
	}

	LOG_INF("  Block complete (%u)", buf_len);
	return 0;
}
