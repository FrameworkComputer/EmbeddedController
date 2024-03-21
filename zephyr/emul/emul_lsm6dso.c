/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_lsm6dso_emul

#include "driver/accelgyro_lsm6dso_public.h"
#include "emul/emul_stub_device.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lsm6dso_emul, CONFIG_SENSOR_LOG_LEVEL);

struct lsm6dso_emul_data {
	uint8_t reg[0x7f];
};

struct lsm6dso_emul_cfg {};

static void lsm6dso_emul_reset(const struct emul *target)
{
	struct lsm6dso_emul_data *data = target->data;

	data->reg[LSM6DSO_INT1_CTRL] = 0;
	data->reg[LSM6DSO_CTRL3_ADDR] = LSM6DSO_IF_INC;
}

static int lsm6dso_emul_handle_write(const struct emul *target, uint8_t regn,
				     uint8_t value)
{
	struct lsm6dso_emul_data *data = target->data;

	switch (regn) {
	case LSM6DSO_CTRL3_ADDR:
		if (FIELD_GET(LSM6DSO_SW_RESET, value) == 1) {
			/* Perform a soft reset */
			lsm6dso_emul_reset(target);
			return 0;
		}
		break;
	}

	data->reg[regn] = value;
	return 0;
}

static int lsm6dso_emul_transfer_i2c(const struct emul *target,
				     struct i2c_msg *msgs, int num_msgs,
				     int addr)
{
	struct lsm6dso_emul_data *data = target->data;

	i2c_dump_msgs_rw(target->dev, msgs, num_msgs, addr, false);

	if (num_msgs < 1) {
		LOG_ERR("Invalid number of messages: %d", num_msgs);
		return -EIO;
	}
	if (FIELD_GET(I2C_MSG_READ, msgs->flags) != 0) {
		LOG_ERR("Unexpected read");
		return -EIO;
	}
	if (msgs->len < 1) {
		LOG_ERR("Unexpected msg0 length %d", msgs->len);
		return -EIO;
	}

	uint8_t regn = msgs->buf[0];
	bool is_read = FIELD_GET(I2C_MSG_READ, msgs->flags) == 1;
	bool is_stop = FIELD_GET(I2C_MSG_STOP, msgs->flags) == 1;

	if (!is_stop && !is_read) {
		/* First message was a write with the register number, check
		 * next message
		 */
		msgs++;
		is_read = FIELD_GET(I2C_MSG_READ, msgs->flags) == 1;
		is_stop = FIELD_GET(I2C_MSG_STOP, msgs->flags) == 1;
	}
	if (is_read) {
		/* Read data */
		for (int i = 0; i < msgs->len; ++i) {
			msgs->buf[i] = data->reg[regn + i];
		}
	} else {
		/* Write data */
		int rc = lsm6dso_emul_handle_write(target, regn, msgs->buf[1]);

		if (rc != 0) {
			return rc;
		}
	}
	return 0;
}

static int lsm6dso_emul_init(const struct emul *target,
			     const struct device *parent)
{
	struct lsm6dso_emul_data *data = target->data;

	data->reg[LSM6DSO_WHO_AM_I_REG] = LSM6DSO_WHO_AM_I;

	return 0;
}

static const struct i2c_emul_api lsm6dso_emul_api_i2c = {
	.transfer = lsm6dso_emul_transfer_i2c,
};

#define LSM6DSO_EMUL(inst)                                                   \
	static struct lsm6dso_emul_data lsm6dso_emul_data_##inst;            \
	const static struct lsm6dso_emul_cfg lsm6dso_emul_cfg_##inst;        \
	EMUL_DT_INST_DEFINE(inst, lsm6dso_emul_init,                         \
			    &lsm6dso_emul_data_##inst,                       \
			    &lsm6dso_emul_cfg_##inst, &lsm6dso_emul_api_i2c, \
			    NULL)

DT_INST_FOREACH_STATUS_OKAY(LSM6DSO_EMUL)
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE)
