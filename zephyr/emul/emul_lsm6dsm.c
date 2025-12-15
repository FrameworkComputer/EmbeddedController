/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_lsm6dsm

#include "driver/accelgyro_lsm6dsm_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_lsm6dsm.h"
#include "math_util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lsm6dsm_emul, CONFIG_SENSOR_LOG_LEVEL);

struct lsm6dsm_emul_data {
	struct i2c_common_emul_data i2c;
	uint8_t reg[0x75];
	/* FIFO is 4kB, but we need multiples of 6 so 4092 */
	uint8_t fifo[4092];
	uint8_t *fifo_head;
};

struct lsm6dsm_emul_cfg {
	struct i2c_common_emul_cfg i2c;
	const struct gpio_dt_spec gpio_spec;
};

static int emul_lsm6dsm_get_fifo_count(const struct emul *emul)
{
	struct lsm6dsm_emul_data *data = emul->data;

	return ((int)FIELD_GET(0x7, data->reg[LSM6DSM_FIFO_STS2_ADDR]) << 8) |
	       data->reg[LSM6DSM_FIFO_STS1_ADDR];
}

static int emul_lsm6dsm_get_fifo_wm(const struct emul *emul)
{
	struct lsm6dsm_emul_data *data = emul->data;

	return ((int)FIELD_GET(0x7, data->reg[LSM6DSM_FIFO_CTRL2_ADDR]) << 8) |
	       data->reg[LSM6DSM_FIFO_CTRL1_ADDR];
}

static void emul_lsm6dsm_set_fifo_count(const struct emul *emul, int count)
{
	struct lsm6dsm_emul_data *data = emul->data;
	int wm = emul_lsm6dsm_get_fifo_wm(emul);

	data->reg[LSM6DSM_FIFO_STS1_ADDR] = 0;
	data->reg[LSM6DSM_FIFO_STS2_ADDR] = 0;

	if (count >= wm && wm != 0) {
		data->reg[LSM6DSM_FIFO_STS2_ADDR] |= BIT(7);
	}
	if (count == 0) {
		data->reg[LSM6DSM_FIFO_STS2_ADDR] |= BIT(4);
	} else {
		data->reg[LSM6DSM_FIFO_STS2_ADDR] |= FIELD_GET(0x700, count);
		data->reg[LSM6DSM_FIFO_STS1_ADDR] = FIELD_GET(0xff, count);
	}
	LOG_DBG("FIFO_STATUS (%d): 0x%02x %02x %02x %02x", count,
		data->reg[LSM6DSM_FIFO_STS1_ADDR],
		data->reg[LSM6DSM_FIFO_STS2_ADDR],
		data->reg[LSM6DSM_FIFO_STS3_ADDR],
		data->reg[LSM6DSM_FIFO_STS4_ADDR]);
}

static void lsm6dsm_emul_set_interrupt_pin(const struct emul *emul, bool active)
{
	const struct lsm6dsm_emul_cfg *config = emul->cfg;

	if (config->gpio_spec.port == NULL) {
		return;
	}

	int pin_value;
	if ((config->gpio_spec.dt_flags & GPIO_ACTIVE_LOW) != 0) {
		pin_value = active ? 0 : 1;
	} else {
		pin_value = active ? 1 : 0;
	}
	gpio_emul_input_set(config->gpio_spec.port, config->gpio_spec.pin,
			    pin_value);
}

void emul_lsm6dsm_append_sample(const struct emul *emul,
				enum motionsensor_type type, float x, float y,
				float z)
{
	static const uint32_t la_fs[] = { 2, 16, 4, 8 };
	static const uint32_t g_fs[] = { 250, 500, 1000, 2000 };
	struct lsm6dsm_emul_data *data = emul->data;

	__ASSERT_NO_MSG(type == MOTIONSENSE_TYPE_ACCEL ||
			type == MOTIONSENSE_TYPE_GYRO);

	/* Convert to register values. */
	int16_t reg_val;
	uint8_t data_reg_addr;
	uint32_t fs;
	if (type == MOTIONSENSE_TYPE_ACCEL) {
		fs = la_fs[FIELD_GET(LSM6DSM_ACCEL_FS_MASK,
				     data->reg[LSM6DSM_ACCEL_FS_ADDR])];
		data_reg_addr = LSM6DSM_OUTX_L_LA;
	} else {
		fs = g_fs[FIELD_GET(LSM6DSM_ACCEL_FS_MASK,
				    data->reg[LSM6DSM_ACCEL_FS_ADDR + 1])];
		data_reg_addr = LSM6DSM_OUTX_L_G;
	}

	__ASSERT_NO_MSG(ABS(x) < (float)fs);
	__ASSERT_NO_MSG(ABS(y) < (float)fs);
	__ASSERT_NO_MSG(ABS(z) < (float)fs);

	const bool append_to_fifo = data->reg[LSM6DSM_FIFO_CTRL5_ADDR] != 0;

	int current_fifo_count = emul_lsm6dsm_get_fifo_count(emul);
	int current_fifo_wm = emul_lsm6dsm_get_fifo_wm(emul);
	int fifo_pos = current_fifo_count * 2;

	LOG_DBG("Append to fifo? %d", append_to_fifo);
	LOG_DBG("Fifo state?     %d/%d", current_fifo_count, current_fifo_wm);
	LOG_DBG("Fifo write pos? %d", fifo_pos);

	reg_val = (int16_t)((x / fs) * INT16_MAX);
	data->fifo_head[fifo_pos++] = data->reg[data_reg_addr++] =
		FIELD_GET(0xff, reg_val);
	data->fifo_head[fifo_pos++] = data->reg[data_reg_addr++] =
		FIELD_GET(0xff00, reg_val);

	reg_val = (int16_t)((y / fs) * INT16_MAX);
	data->fifo_head[fifo_pos++] = data->reg[data_reg_addr++] =
		FIELD_GET(0xff, reg_val);
	data->fifo_head[fifo_pos++] = data->reg[data_reg_addr++] =
		FIELD_GET(0xff00, reg_val);

	reg_val = (int16_t)((z / fs) * INT16_MAX);
	data->fifo_head[fifo_pos++] = data->reg[data_reg_addr++] =
		FIELD_GET(0xff, reg_val);
	data->fifo_head[fifo_pos++] = data->reg[data_reg_addr++] =
		FIELD_GET(0xff00, reg_val);

	if (type == MOTIONSENSE_TYPE_ACCEL) {
		data->reg[LSM6DSM_STATUS_REG] |= LSM6DSM_STS_XLDA_UP;
	} else {
		data->reg[LSM6DSM_STATUS_REG] |= LSM6DSM_STS_GDA_UP;
	}

	if (append_to_fifo) {
		emul_lsm6dsm_set_fifo_count(emul, current_fifo_count + 3);
		if (current_fifo_count + 3 == current_fifo_wm) {
			/* We crossed the watermark, fire the GPIO */
			data->reg[LSM6DSM_FIFO_STS2_ADDR] |= BIT(7);
			lsm6dsm_emul_set_interrupt_pin(emul, true);
		}
	}
}

static void lsm6dsm_emul_reset(const struct emul *emul)
{
	struct lsm6dsm_emul_data *data = emul->data;

	memset(data->reg, 0, sizeof(data->reg));
	data->reg[LSM6DSM_WHO_AM_I_REG] = LSM6DSM_WHO_AM_I;
	data->fifo_head = data->fifo;
	emul_lsm6dsm_set_fifo_count(emul, 0);
	lsm6dsm_emul_set_interrupt_pin(emul, false);
}

static int lsm6dsm_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int bytes)
{
	struct lsm6dsm_emul_data *data = emul->data;

	LOG_DBG("Reading byte %d starting at register 0x%02x", bytes, reg);
	if (reg < 0 || reg + bytes >= sizeof(data->reg)) {
		return -ENOTSUP;
	}

	if (reg == LSM6DSM_FIFO_DATA_ADDR) {
		int fifo_count = emul_lsm6dsm_get_fifo_count(emul);
		if (fifo_count == 0) {
			LOG_ERR("Tried to read FIFO, but no entries are there");
			return -EINVAL;
		}
		*val = data->fifo_head[bytes];
		if ((bytes % 6) == 5) {
			data->fifo_head += 6;
			emul_lsm6dsm_set_fifo_count(emul, fifo_count - 3);
			if (fifo_count - 3 < emul_lsm6dsm_get_fifo_wm(emul)) {
				/* We crossed below the WM */
				lsm6dsm_emul_set_interrupt_pin(emul, false);
			}
			if (data->fifo_head ==
			    data->fifo + sizeof(data->fifo)) {
				/* Reset the head */
				data->fifo_head = data->fifo;
			}
		}
		return 0;
	}

	*val = data->reg[reg + bytes];

	return 0;
}

static int lsm6dsm_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	struct lsm6dsm_emul_data *data = emul->data;

	LOG_DBG("Writing byte %d starting at register 0x%02x: 0x%02x",
		bytes - 1, reg, val);
	if (reg < 0 || reg + bytes - 1 >= sizeof(data->reg)) {
		return -ENOTSUP;
	}

	/* Generic write, just assume there are no side-effects for now. */
	data->reg[reg + bytes - 1] = val;

	/* Software reset */
	if (reg == LSM6DSM_CTRL3_ADDR && (val & LSM6DSM_SW_RESET) != 0) {
		lsm6dsm_emul_reset(emul);
	}

	LOG_DBG("Write byte, FIFO_STATUS: 0x%02x %02x %02x %02x",
		data->reg[LSM6DSM_FIFO_STS1_ADDR],
		data->reg[LSM6DSM_FIFO_STS2_ADDR],
		data->reg[LSM6DSM_FIFO_STS3_ADDR],
		data->reg[LSM6DSM_FIFO_STS4_ADDR]);

	return 0;
}

static int lsm6dsm_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	const struct lsm6dsm_emul_cfg *config = emul->cfg;
	struct lsm6dsm_emul_data *data = emul->data;

	data->i2c.i2c = parent;
	i2c_common_emul_init(&data->i2c);
	lsm6dsm_emul_reset(emul);

	__ASSERT(config->gpio_spec.dt_flags & GPIO_ACTIVE_LOW,
		 "The lsm6dsm driver only supports the 'active low' "
		 "configuration, please make sure that the driver was updated "
		 "before removing this assert");

	return 0;
}

#define LSM6DSM_EMUL(inst)                                                    \
	static struct lsm6dsm_emul_data lsm6dsm_emul_data_##inst = {        \
		.i2c = {                                                    \
			.write_byte = lsm6dsm_emul_write_byte,              \
			.read_byte = lsm6dsm_emul_read_byte,                \
			.i2c = DEVICE_DT_GET(DT_INST_PARENT(inst)),         \
		},                                                          \
	}; \
	const static struct lsm6dsm_emul_cfg lsm6dsm_emul_cfg_##inst = {    \
		.i2c = {                                                    \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(inst)),  \
			.data = &lsm6dsm_emul_data_##inst.i2c,              \
			.addr = DT_INST_REG_ADDR(inst),                     \
		},                                                          \
		.gpio_spec = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {}), \
	}; \
	EMUL_DT_INST_DEFINE(inst, lsm6dsm_emul_init,                          \
			    &lsm6dsm_emul_data_##inst,                        \
			    &lsm6dsm_emul_cfg_##inst, &i2c_common_emul_api,   \
			    NULL);

DT_INST_FOREACH_STATUS_OKAY(LSM6DSM_EMUL)
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
