/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/sn5s330.h"
#include "driver/ppc/sn5s330_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_sn5s330.h"
#include "emul/emul_stub_device.h"
#include "i2c.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#define DT_DRV_COMPAT cros_sn5s330_emul

LOG_MODULE_REGISTER(sn5s330_emul, CONFIG_SN5S330_EMUL_LOG_LEVEL);

struct sn5s330_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
	/** Emulated int-pin port */
	const struct device *gpio_int_port;
	/** Emulated int-pin pin */
	gpio_pin_t gpio_int_pin;
	/** Emulated FUNC_SET1 register */
	uint8_t func_set1_reg;
	/** Emulated FUNC_SET2 register */
	uint8_t func_set2_reg;
	/** Emulated FUNC_SET3 register */
	uint8_t func_set3_reg;
	/** Emulated FUNC_SET4 register */
	uint8_t func_set4_reg;
	/** Emulated FUNC_SET5 register */
	uint8_t func_set5_reg;
	/** Emulated FUNC_SET6 register */
	uint8_t func_set6_reg;
	/** Emulated FUNC_SET7 register */
	uint8_t func_set7_reg;
	/** Emulated FUNC_SET8 register */
	uint8_t func_set8_reg;
	/** Emulated FUNC_SET9 register */
	uint8_t func_set9_reg;
	/** Emulated FUNC_SET10 register */
	uint8_t func_set10_reg;
	/** Emulated FUNC_SET11 register */
	uint8_t func_set11_reg;
	/** Emulated FUNC_SET12 register */
	uint8_t func_set12_reg;
	/** Emulated INT_STATUS_REG1 register */
	uint8_t int_status_reg1;
	/** Emulated INT_STATUS_REG2 register */
	uint8_t int_status_reg2;
	/** Emulated INT_STATUS_REG3 register */
	uint8_t int_status_reg3;
	/** Emulated INT_STATUS_REG4 register */
	/*
	 * TODO(b/205754232): Register name discrepancy
	 */
	uint8_t int_status_reg4;
	/*
	 * TODO(b/203364783): For all falling edge registers, implement
	 * interrupt and bit change to correspond to change in interrupt status
	 * registers.
	 */
	/** Emulated INT_MASK_RISE_REG1 register */
	uint8_t int_mask_rise_reg1;
	/** Emulated INT_MASK_RISE_REG2 register */
	uint8_t int_mask_rise_reg2;
	/** Emulated INT_MASK_RISE_REG3 register */
	uint8_t int_mask_rise_reg3;
	/** Emulated INT_MASK_FALL_REG1 register */
	uint8_t int_mask_fall_reg1;
	/** Emulated INT_MASK_FALL_REG2 register */
	uint8_t int_mask_fall_reg2;
	/** Emulated INT_MASK_FALL_REG3 register */
	uint8_t int_mask_fall_reg3;
	/** Emulated INT_TRIP_RISE_REG1 register */
	uint8_t int_trip_rise_reg1;
	/** Emulated INT_TRIP_RISE_REG2 register */
	uint8_t int_trip_rise_reg2;
	/** Emulated INT_TRIP_RISE_REG3 register */
	uint8_t int_trip_rise_reg3;
	/** Emulated INT_TRIP_FALL_REG1 register */
	uint8_t int_trip_fall_reg1;
	/** Emulated INT_TRIP_FALL_REG2 register */
	uint8_t int_trip_fall_reg2;
	/** Emulated INT_TRIP_FALL_REG3 register */
	uint8_t int_trip_fall_reg3;
};

struct sn5s330_emul_cfg {
	/** Common I2C config */
	const struct i2c_common_emul_cfg common;
};

test_mockable_static void sn5s330_emul_interrupt_set_stub(void)
{
	/* Stub to be used by fff fakes during test */
}

/* Workhorse for mapping i2c reg to internal emulator data access */
static uint8_t *sn5s330_emul_get_reg_ptr(struct sn5s330_emul_data *data,
					 int reg)
{
	switch (reg) {
	case SN5S330_FUNC_SET1:
		return &(data->func_set1_reg);
	case SN5S330_FUNC_SET2:
		return &(data->func_set2_reg);
	case SN5S330_FUNC_SET3:
		return &(data->func_set3_reg);
	case SN5S330_FUNC_SET4:
		return &(data->func_set4_reg);
	case SN5S330_FUNC_SET5:
		return &(data->func_set5_reg);
	case SN5S330_FUNC_SET6:
		return &(data->func_set6_reg);
	case SN5S330_FUNC_SET7:
		return &(data->func_set7_reg);
	case SN5S330_FUNC_SET8:
		return &(data->func_set8_reg);
	case SN5S330_FUNC_SET9:
		return &(data->func_set9_reg);
	case SN5S330_FUNC_SET10:
		return &(data->func_set10_reg);
	case SN5S330_FUNC_SET11:
		return &(data->func_set11_reg);
	case SN5S330_FUNC_SET12:
		return &(data->func_set12_reg);
	case SN5S330_INT_STATUS_REG1:
		return &(data->int_status_reg1);
	case SN5S330_INT_STATUS_REG2:
		return &(data->int_status_reg2);
	case SN5S330_INT_STATUS_REG3:
		return &(data->int_status_reg3);
	case SN5S330_INT_STATUS_REG4:
		return &(data->int_status_reg4);
	case SN5S330_INT_MASK_RISE_REG1:
		return &(data->int_mask_rise_reg1);
	case SN5S330_INT_MASK_RISE_REG2:
		return &(data->int_mask_rise_reg2);
	case SN5S330_INT_MASK_RISE_REG3:
		return &(data->int_mask_rise_reg3);
	case SN5S330_INT_MASK_FALL_REG1:
		return &(data->int_mask_fall_reg1);
	case SN5S330_INT_MASK_FALL_REG2:
		return &(data->int_mask_fall_reg2);
	case SN5S330_INT_MASK_FALL_REG3:
		return &(data->int_mask_fall_reg3);
	case SN5S330_INT_TRIP_RISE_REG1:
		return &(data->int_trip_rise_reg1);
	case SN5S330_INT_TRIP_RISE_REG2:
		return &(data->int_trip_rise_reg2);
	case SN5S330_INT_TRIP_RISE_REG3:
		return &(data->int_trip_rise_reg3);
	case SN5S330_INT_TRIP_FALL_REG1:
		return &(data->int_trip_fall_reg1);
	case SN5S330_INT_TRIP_FALL_REG2:
		return &(data->int_trip_fall_reg2);
	case SN5S330_INT_TRIP_FALL_REG3:
		return &(data->int_trip_fall_reg3);
	default:
		__ASSERT(false, "Unimplemented Register Access Error on 0x%x",
			 reg);
		/* Statement never reached, required for compiler warnings */
		return NULL;
	}
}

void sn5s330_emul_peek_reg(const struct emul *emul, uint32_t reg, uint8_t *val)
{
	struct sn5s330_emul_data *data = emul->data;
	uint8_t *data_reg = sn5s330_emul_get_reg_ptr(data, reg);

	*val = *data_reg;
}

static void sn5s330_emul_set_int_pin(const struct emul *emul, bool val)
{
	struct sn5s330_emul_data *data = emul->data;
	int res = gpio_emul_input_set(data->gpio_int_port, data->gpio_int_pin,
				      val);
	ARG_UNUSED(res);
	__ASSERT_NO_MSG(res == 0);
}

void sn5s330_emul_assert_interrupt(const struct emul *emul)
{
	sn5s330_emul_interrupt_set_stub();
	sn5s330_emul_set_int_pin(emul, false);
}

void sn5s330_emul_deassert_interrupt(const struct emul *emul)
{
	sn5s330_emul_set_int_pin(emul, true);
}

static int sn5s330_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int bytes)
{
	struct sn5s330_emul_data *data = emul->data;
	uint8_t *reg_to_read = sn5s330_emul_get_reg_ptr(data, reg);

	__ASSERT(bytes == 0, "bytes 0x%x != 0x0 on reg 0x%x", bytes, reg);
	*val = *reg_to_read;

	return 0;
}

static int sn5s330_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	struct sn5s330_emul_data *data = emul->data;
	uint8_t *reg_to_write;
	bool deassert_int = false;

	__ASSERT(bytes == 1, "bytes 0x%x != 0x1 on reg 0x%x", bytes, reg);

	/* Specially check for read-only reg */
	switch (reg) {
	case SN5S330_INT_TRIP_RISE_REG1:
		__fallthrough;
	case SN5S330_INT_TRIP_RISE_REG2:
		__fallthrough;
	case SN5S330_INT_TRIP_RISE_REG3:
		__fallthrough;
	case SN5S330_INT_TRIP_FALL_REG1:
		__fallthrough;
	case SN5S330_INT_TRIP_FALL_REG2:
		__fallthrough;
	case SN5S330_INT_TRIP_FALL_REG3:
		reg_to_write = sn5s330_emul_get_reg_ptr(data, reg);
		/* Clearing any bit deasserts /INT interrupt signal */
		deassert_int = (*reg_to_write & val) != 0;
		/* Write 0 is noop and 1 clears the bit. */
		val = (~val & *reg_to_write);
		*reg_to_write = val;
		break;
	case SN5S330_INT_STATUS_REG1:
		__fallthrough;
	case SN5S330_INT_STATUS_REG2:
		__fallthrough;
	case SN5S330_INT_STATUS_REG3:
		__ASSERT(false,
			 "Write to an unverified-as-safe read-only register on "
			 "0x%x",
			 reg);
		__fallthrough;
	default:
		reg_to_write = sn5s330_emul_get_reg_ptr(data, reg);
		*reg_to_write = val;
	}

	if (deassert_int)
		sn5s330_emul_deassert_interrupt(emul);

	return 0;
}

void sn5s330_emul_make_vbus_overcurrent(const struct emul *emul)
{
	struct sn5s330_emul_data *data = emul->data;

	data->int_status_reg1 |= SN5S330_ILIM_PP1_MASK;
	data->int_trip_rise_reg1 |= SN5S330_ILIM_PP1_MASK;

	/* driver disabled this interrupt trigger */
	if (data->int_mask_rise_reg1 & SN5S330_ILIM_PP1_MASK)
		return;

	sn5s330_emul_assert_interrupt(emul);
}

void sn5s330_emul_lower_vbus_below_minv(const struct emul *emul)
{
	struct sn5s330_emul_data *data = emul->data;

	data->int_status_reg4 |= SN5S330_VSAFE0V_STAT;

	/* driver disabled this interrupt trigger */
	if (data->int_status_reg4 & SN5S330_VSAFE0V_MASK)
		return;

	sn5s330_emul_assert_interrupt(emul);
}

void sn5s330_emul_reset(const struct emul *emul)
{
	struct sn5s330_emul_data *data = emul->data;
	struct i2c_common_emul_data common = data->common;
	const struct device *gpio_int_port = data->gpio_int_port;
	gpio_pin_t gpio_int_pin = data->gpio_int_pin;

	sn5s330_emul_deassert_interrupt(emul);

	/*
	 * TODO(b/203364783): Some registers reset with set bits; this should be
	 * reflected in the emul_reset function.
	 */

	/* Only Reset the sn5s330 Register Data */
	memset(data, 0, sizeof(struct sn5s330_emul_data));
	data->common = common;
	data->gpio_int_port = gpio_int_port;
	data->gpio_int_pin = gpio_int_pin;
}

static int emul_sn5s330_init(const struct emul *emul,
			     const struct device *parent)
{
	struct sn5s330_emul_data *data = emul->data;

	sn5s330_emul_deassert_interrupt(emul);

	data->common.i2c = parent;
	i2c_common_emul_init(&data->common);

	return 0;
}

#define SN5S330_GET_GPIO_INT_PORT(n) \
	DEVICE_DT_GET(DT_GPIO_CTLR(DT_INST_PROP(n, int_pin), gpios))

#define SN5S330_GET_GPIO_INT_PIN(n) DT_GPIO_PIN(DT_INST_PROP(n, int_pin), gpios)

#define INIT_SN5S330(n)                                                          \
	static struct sn5s330_emul_data sn5s330_emul_data_##n = { \
		.common = {                                                    \
			.write_byte = sn5s330_emul_write_byte,                 \
			.read_byte = sn5s330_emul_read_byte,                   \
		},                                                             \
		.gpio_int_port = SN5S330_GET_GPIO_INT_PORT(n),		       \
		.gpio_int_pin = SN5S330_GET_GPIO_INT_PIN(n),		       \
	};              \
	static struct sn5s330_emul_cfg sn5s330_emul_cfg_##n = {                \
		.common = {                                                    \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),        \
			.addr = DT_INST_REG_ADDR(n),                           \
		},                                                             \
	}; \
	EMUL_DT_INST_DEFINE(n, emul_sn5s330_init, &sn5s330_emul_data_##n,        \
			    &sn5s330_emul_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_SN5S330)
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *
emul_sn5s330_get_i2c_common_data(const struct emul *emul)
{
	return emul->data;
}
