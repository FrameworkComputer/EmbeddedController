/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/emul_syv682x.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT zephyr_syv682x_emul

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(syv682x);
#define EMUL_REG_COUNT (SYV682X_CONTROL_4_REG + 1)
#define EMUL_REG_IS_VALID(reg) (reg >= 0 && reg < EMUL_REG_COUNT)

struct syv682x_emul_data {
	struct i2c_common_emul_data common;
	/** GPIO ports connected to the PPC */
	const struct device *frs_en_gpio_port;
	gpio_pin_t frs_en_gpio_pin;
	const struct device *alert_gpio_port;
	gpio_pin_t alert_gpio_pin;
	/** Configuration information */
	const struct syv682x_emul_cfg *cfg;
	/** Current state of all emulated SYV682x registers */
	uint8_t reg[EMUL_REG_COUNT];
	/**
	 * Current state of conditions affecting interrupt bits, as distinct
	 * from the current values of those bits stored in reg.
	 */
	uint8_t status_cond;
	uint8_t control_4_cond;
	/**
	 * How many CONTROL_3 reads the busy bit should stay set. 0 means not
	 * busy.
	 */
	int busy_read_count;
};

/** Static configuration for the emulator */
struct syv682x_emul_cfg {
	const struct i2c_common_emul_cfg common;
};

/* Asserts or deasserts the interrupt signal to the EC. */
static void syv682x_emul_set_alert(struct syv682x_emul_data *data, bool alert)
{
	int res = gpio_emul_input_set(data->alert_gpio_port,
				      /* The signal is inverted. */
				      data->alert_gpio_pin, !alert);
	__ASSERT_NO_MSG(res == 0);
}

int syv682x_emul_set_reg(const struct emul *emul, int reg, uint8_t val)
{
	struct syv682x_emul_data *data = emul->data;

	if (!EMUL_REG_IS_VALID(reg))
		return -EIO;

	data->reg[reg] = val;

	return 0;
}

void syv682x_emul_set_condition(const struct emul *emul, uint8_t status,
				uint8_t control_4)
{
	uint8_t control_4_interrupt = control_4 & SYV682X_CONTROL_4_INT_MASK;
	struct syv682x_emul_data *data = emul->data;
	int frs_en_gpio = gpio_emul_output_get(data->frs_en_gpio_port,
					       data->frs_en_gpio_pin);

	__ASSERT_NO_MSG(frs_en_gpio >= 0);

	/* Only assert FRS status if FRS is enabled. */
	if (!frs_en_gpio)
		status &= ~SYV682X_STATUS_FRS;

	data->status_cond = status;
	data->reg[SYV682X_STATUS_REG] |= status;

	data->control_4_cond = control_4_interrupt;
	/* Only update the interrupting bits of CONTROL_4. */
	data->reg[SYV682X_CONTROL_4_REG] &= ~SYV682X_CONTROL_4_INT_MASK;
	data->reg[SYV682X_CONTROL_4_REG] |= control_4_interrupt;

	/* These conditions disable the power path. */
	if (status &
	    (SYV682X_STATUS_TSD | SYV682X_STATUS_OVP | SYV682X_STATUS_OC_HV)) {
		data->reg[SYV682X_CONTROL_1_REG] |= SYV682X_CONTROL_1_PWR_ENB;
	}

	/*
	 * Note: The description of CONTROL_4 suggests that setting VCONN_OC
	 * will turn off the VCONN channel. The "VCONN Channel Over Current
	 * Response" plot shows that VCONN the device will merely throttle VCONN
	 * current. The latter behavior is observed in practice, and this
	 * emulator does not currently model it.
	 */

	/* VBAT_OVP disconnects CC and VCONN. */
	if (control_4_interrupt & SYV682X_CONTROL_4_VBAT_OVP) {
		data->reg[SYV682X_CONTROL_4_REG] &= ~(
			SYV682X_CONTROL_4_CC1_BPS | SYV682X_CONTROL_4_CC2_BPS |
			SYV682X_CONTROL_4_VCONN1 | SYV682X_CONTROL_4_VCONN2);
	}

	syv682x_emul_set_alert(data, status | control_4_interrupt);
}

void syv682x_emul_set_busy_reads(const struct emul *emul, int reads)
{
	struct syv682x_emul_data *data = emul->data;
	data->busy_read_count = reads;
	if (reads)
		data->reg[SYV682X_CONTROL_3_REG] |= SYV682X_BUSY;
	else
		data->reg[SYV682X_CONTROL_3_REG] &= ~SYV682X_BUSY;
}

int syv682x_emul_get_reg(const struct emul *emul, int reg, uint8_t *val)
{
	struct syv682x_emul_data *data = emul->data;

	if (!EMUL_REG_IS_VALID(reg))
		return -EIO;

	*val = data->reg[reg];

	return 0;
}

static int syv682x_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	struct syv682x_emul_data *data = emul->data;

	zassert_equal(bytes, 1, "Write: bytes == %i at offset 0x%x", bytes,
		      reg);

	switch (reg) {
	case SYV682X_CONTROL_1_REG:
		/*
		 * If OVP or TSD is active, the power path stays
		 * disabled.
		 */
		if (data->status_cond &
		    (SYV682X_STATUS_TSD | SYV682X_STATUS_OVP))
			val |= SYV682X_CONTROL_1_PWR_ENB;
		break;
	case SYV682X_CONTROL_4_REG:
		/* Interrupt bits are read-only. */
		val &= ~SYV682X_CONTROL_4_INT_MASK;
		break;
	default:
		break;
	}

	return syv682x_emul_set_reg(emul, reg, val);
}

static int syv682x_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int bytes)
{
	struct syv682x_emul_data *data = emul->data;
	int ret = syv682x_emul_get_reg(emul, reg, val);

	zassert_equal(bytes, 0, "Read: bytes == %i at offset 0x%x", bytes, reg);

	switch (reg) {
	/*
	 * STATUS and the interrupt bits of CONTROL_4 are clear-on-read
	 * (if the underlying condition has cleared).
	 */
	case SYV682X_STATUS_REG:
		syv682x_emul_set_reg(emul, reg, data->status_cond);
		break;
	case SYV682X_CONTROL_3_REG:
		/* Update CONTROL_3[BUSY] based on the busy count. */
		if (data->busy_read_count > 0) {
			if (--data->busy_read_count == 0) {
				data->reg[SYV682X_CONTROL_3_REG] &=
					~SYV682X_BUSY;
			}
		}
		break;
	case SYV682X_CONTROL_4_REG:
		syv682x_emul_set_reg(emul, reg,
				     (*val & ~SYV682X_CONTROL_4_INT_MASK) |
					     data->control_4_cond);
		break;
	default:
		break;
	}

	return ret;
}

static void syv682x_emul_reset(const struct emul *emul)
{
	struct syv682x_emul_data *data = emul->data;

	memset(data->reg, 0, sizeof(data->reg));

	syv682x_emul_set_alert(data, false);
	data->reg[SYV682X_CONTROL_1_REG] =
		(SYV682X_HV_ILIM_3_30 << SYV682X_HV_ILIM_BIT_SHIFT) |
		(SYV682X_5V_ILIM_3_30 << SYV682X_5V_ILIM_BIT_SHIFT) |
		/* HV_DR = 0 */
		SYV682X_CONTROL_1_CH_SEL;
}

/**
 * @brief Set up a new SYV682x emulator
 *
 * This should be called for each SYV682x device that needs to be emulated. It
 * registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 on success or an error code on failure
 */
static int syv682x_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct syv682x_emul_data *data = emul->data;

	data->common.i2c = parent;
	i2c_common_emul_init(&data->common);

	syv682x_emul_reset(emul);
	return 0;
}

/* Device instantiation */
#define SYV682X_EMUL(n)                                                          \
	static struct syv682x_emul_data syv682x_emul_data_##n = {              \
		.common = {                                                    \
			.write_byte = syv682x_emul_write_byte,                 \
			.read_byte = syv682x_emul_read_byte,                   \
		},                                                             \
		.frs_en_gpio_port = DEVICE_DT_GET(DT_GPIO_CTLR(                \
					DT_INST_PROP(n, frs_en_gpio), gpios)), \
		.frs_en_gpio_pin = DT_GPIO_PIN(                                \
				DT_INST_PROP(n, frs_en_gpio), gpios),          \
		.alert_gpio_port = DEVICE_DT_GET(DT_GPIO_CTLR(                 \
					DT_INST_PROP(n, alert_gpio), gpios)),  \
		.alert_gpio_pin = DT_GPIO_PIN(                                 \
				DT_INST_PROP(n, alert_gpio), gpios),           \
	}; \
	static const struct syv682x_emul_cfg syv682x_emul_cfg_##n = {          \
		.common = {                                                    \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),        \
			.addr = DT_INST_REG_ADDR(n),                           \
		},                                                             \
	}; \
	EMUL_DT_INST_DEFINE(n, syv682x_emul_init, &syv682x_emul_data_##n,        \
			    &syv682x_emul_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(SYV682X_EMUL)

#ifdef CONFIG_ZTEST

#define SYV682X_EMUL_RESET_RULE_BEFORE(n) \
	syv682x_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)));

static void emul_syv682x_reset_before(const struct ztest_unit_test *test,
				      void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	DT_INST_FOREACH_STATUS_OKAY(SYV682X_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(emul_syv682x_reset, emul_syv682x_reset_before, NULL);
#endif /* CONFIG_ZTEST */

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *
emul_syv682x_get_i2c_common_data(const struct emul *emul)
{
	return emul->data;
}
