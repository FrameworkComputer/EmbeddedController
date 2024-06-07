/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/emul_touchpad_elan.h"

#include <zephyr/device.h>

#define DT_DRV_COMPAT elan_ekth3000

#define NO_REG -1
#define ETP_I2C_STAND_CMD 0x0005
#define ETP_I2C_PATTERN_CMD 0x0100
#define ETP_I2C_IC_TYPE_CMD 0x0103
#define ETP_I2C_XY_TRACENUM_CMD 0x0105
#define ETP_I2C_MAX_X_AXIS_CMD 0x0106
#define ETP_I2C_MAX_Y_AXIS_CMD 0x0107
#define ETP_I2C_RESOLUTION_CMD 0x0108
#define ETP_I2C_PRESSURE_CMD 0x010A
#define ETP_I2C_IAP_VERSION_CMD 0x0110
#define ETP_I2C_SET_CMD 0x300
#define ETP_I2C_POWER_CMD 0x0307

#define ETP_I2C_REPORT_LEN 34

struct touchpad_elan_emul_data {
	struct i2c_common_emul_data common_data;
	int reg16;
	uint16_t val16;
	uint8_t raw_report[ETP_I2C_REPORT_LEN];

	uint16_t reg_power;
	uint16_t reg_set;
	uint16_t reg_stand;
};

void touchpad_elan_emul_set_raw_report(const struct emul *emul,
				       const uint8_t *report)
{
	struct touchpad_elan_emul_data *emul_data = emul->data;

	memcpy(emul_data->raw_report, report, ETP_I2C_REPORT_LEN);
}

static int touchpad_elan_emul_read(const struct emul *emul, int reg,
				   uint8_t *val, int bytes, void *unused_data)
{
	struct touchpad_elan_emul_data *emul_data = emul->data;
	uint16_t val16;

	switch (emul_data->reg16) {
	case NO_REG: /* read report */
		if (bytes < 0 || bytes > ETP_I2C_REPORT_LEN) {
			return -1;
		}
		*val = emul_data->raw_report[bytes];
		return 0;
	case ETP_I2C_STAND_CMD:
		val16 = emul_data->reg_stand;
		break;
	case ETP_I2C_PATTERN_CMD:
		val16 = 0x0100;
		break;
	case ETP_I2C_IC_TYPE_CMD:
		val16 = 0x1000;
		break;
	case ETP_I2C_XY_TRACENUM_CMD:
		val16 = 0x0B15;
		break;
	case ETP_I2C_IAP_VERSION_CMD:
		val16 = 0x0100;
		break;
	case ETP_I2C_MAX_X_AXIS_CMD:
		val16 = 2644;
		break;
	case ETP_I2C_MAX_Y_AXIS_CMD:
		val16 = 1440;
		break;
	case ETP_I2C_RESOLUTION_CMD:
		val16 = 0x0101;
		break;
	case ETP_I2C_PRESSURE_CMD:
		val16 = 0x12;
		break;
	case ETP_I2C_SET_CMD:
		val16 = emul_data->reg_set;
		break;
	case ETP_I2C_POWER_CMD:
		val16 = emul_data->reg_power;
		break;
	default:
		return -1;
	}

	if (bytes == 0) {
		*val = val16 & 0xFF;
		return 0;
	}

	if (bytes == 1) {
		*val = val16 >> 8;
		return 0;
	}

	return -1;
}

static int touchpad_elan_emul_write(const struct emul *emul, int reg,
				    uint8_t val, int bytes, void *unused_data)
{
	struct touchpad_elan_emul_data *emul_data = emul->data;

	switch (bytes) {
	case 1:
		emul_data->reg16 = reg | (val << 8);
		break;
	case 2:
		emul_data->val16 = val;
		break;
	case 3:
		emul_data->val16 |= (val << 8);
		break;
	default:
		return -1;
	}

	if (bytes >= 4) {
		return -1;
	}

	return 0;
}

static int touchpad_elan_emul_finish_write(const struct emul *emul, int reg,
					   int bytes)
{
	struct touchpad_elan_emul_data *emul_data = emul->data;

	/* write */
	if (bytes == 4) {
		uint16_t reg = emul_data->reg16;
		uint16_t val = emul_data->val16;

		emul_data->reg16 = NO_REG;

		switch (reg) {
		case ETP_I2C_STAND_CMD:
			emul_data->reg_stand = val;
			return 0;
		case ETP_I2C_SET_CMD:
			emul_data->reg_set = val;
			return 0;
		case ETP_I2C_POWER_CMD:
			emul_data->reg_power = val;
			return 0;
		default:
			return -1;
		}
	}

	if (bytes != 2) {
		emul_data->reg16 = NO_REG;

		return -1;
	}

	/* read */
	return 0;
}

static int touchpad_elan_emul_finish_read(const struct emul *emul, int reg,
					  int bytes)
{
	struct touchpad_elan_emul_data *emul_data = emul->data;

	emul_data->reg16 = NO_REG;

	return 0;
}
static int elan_touchpad_emul_init(const struct emul *emul,
				   const struct device *parent)
{
	struct touchpad_elan_emul_data *emul_data = emul->data;
	struct i2c_common_emul_data *common_data = &emul_data->common_data;

	i2c_common_emul_init(common_data);
	common_data->finish_write = touchpad_elan_emul_finish_write;
	common_data->finish_read = touchpad_elan_emul_finish_read;
	i2c_common_emul_set_read_func(common_data, touchpad_elan_emul_read,
				      NULL);
	i2c_common_emul_set_write_func(common_data, touchpad_elan_emul_write,
				       NULL);

	return 0;
}

#define INIT_ELAN_TOUCHPAD_EMUL(n)                                       \
	static struct touchpad_elan_emul_data emul_data_##n;             \
	static struct i2c_common_emul_cfg common_cfg_##n = {             \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),          \
		.data = &emul_data_##n.common_data,                      \
		.addr = DT_INST_REG_ADDR(n),                             \
	};                                                               \
	static struct touchpad_elan_emul_data emul_data_##n = {        \
		.common_data = { .cfg = &common_cfg_##n, },            \
		.reg_power = 1, /* power on by default */              \
	}; \
	EMUL_DT_INST_DEFINE(n, elan_touchpad_emul_init, &emul_data_##n,  \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_ELAN_TOUCHPAD_EMUL);

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
