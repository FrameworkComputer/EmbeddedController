/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_pdc.h"
#include "emul/emul_tps6699x.h"
#include "usbc/utils.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

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

static bool register_is_valid(const struct tps6699x_emul_pdc_data *data,
			      int reg)
{
	return reg <= sizeof(data->reg_val) / sizeof(*data->reg_val);
}

static bool register_access_is_valid(const struct tps6699x_emul_pdc_data *data,
				     int reg, int bytes)
{
	return register_is_valid(data, reg) && bytes <= sizeof(*data->reg_val);
}

static int tps6699x_emul_start_write(const struct emul *emul, int reg)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);

	LOG_DBG("start_write reg=%#x", reg);

	if (register_is_valid(data, reg)) {
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

	LOG_DBG("write_byte reg=%#x, val=%#02x, bytes=%d", reg, val, bytes);

	if (register_access_is_valid(data, reg, bytes)) {
		return -EIO;
	}

	data->reg_val[reg][bytes] = val;

	return 0;
}

static int tps6699x_emul_finish_write(const struct emul *emul, int reg,
				      int bytes)
{
	LOG_DBG("finish_write reg=%#x, bytes=%d", reg, bytes);

	/* TODO(b/345292002): Actually handle register accesses. */

	return 0;
}

static int tps6699x_emul_start_read(const struct emul *emul, int reg)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);

	LOG_DBG("start_read reg=%#x", reg);

	if (register_is_valid(data, reg)) {
		return -EIO;
	}

	data->reg_addr = reg;

	return 0;
}

static int tps6699x_emul_read_byte(const struct emul *emul, int reg,
				   uint8_t *val, int bytes)
{
	struct tps6699x_emul_pdc_data *data = tps6699x_emul_get_pdc_data(emul);

	if (register_access_is_valid(data, reg, bytes)) {
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

	LOG_DBG("read_byte reg=%#x, bytes=%d, val=%#02x", reg, bytes, *val);

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

static int emul_tps669x_set_response_delay(const struct emul *target,
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
	.set_response_delay = emul_tps669x_set_response_delay,
	.get_connector_reset = NULL,
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
