/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Driver file for PD task to configure USB-C Alternate modes on Intel SoC.
 * Elaborate details can be found in respective SoC's "Platform Power
 * Delivery Controller Interface for SoC and Retimer" document.
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/intel_altmode.h>

#define DT_DRV_COMPAT intel_pd_altmode

LOG_MODULE_REGISTER(INTEL_ALTMODE, LOG_LEVEL_ERR);

struct pd_altmode_config {
	/* I2C config */
	struct i2c_dt_spec i2c;
	/*
	 * PD interrupt to wake the task to configure alternate modes. There
	 * can be individual Interrupt pin for each PD port or all the PD
	 * interrupts can be muxed to single GPIO. This helps to keep common
	 * code for single port / dual port PD solutions offered by different
	 * PD vendors.
	 */
	struct gpio_dt_spec int_gpio;
	/* Shared interrupt pin in dual port solution */
	bool shared_irq;
};

struct pd_altmode_data {
	const struct device *dev;
	struct k_work work;
	struct gpio_callback gpio_cb;
	intel_altmode_callback isr_cb;
};

static int intel_altmode_read_status(const struct device *dev,
				     union data_status_reg *data)
{
	const struct pd_altmode_config *cfg = dev->config;
	uint8_t buf[INTEL_ALTMODE_DATA_STATUS_REG_LEN + 1];
	int rv;

	/*
	 * Read sequence
	 * DEV_ADDR - REG_ID - DEV_ADDR - READ_LEN - DATA0 .. DATAn
	 */
	rv = i2c_burst_read_dt(&cfg->i2c, INTEL_ALTMODE_REG_DATA_STATUS, buf,
			       INTEL_ALTMODE_DATA_STATUS_REG_LEN + 1);
	if (rv)
		return rv;
	if (buf[0] != INTEL_ALTMODE_DATA_STATUS_REG_LEN)
		return -EIO;

	memcpy(data, &buf[1], INTEL_ALTMODE_DATA_STATUS_REG_LEN);

	return 0;
}

static int intel_altmode_write_control(const struct device *dev,
				       union data_control_reg *data)
{
	uint8_t buf[INTEL_ALTMODE_DATA_CONTROL_REG_LEN + 2];
	const struct pd_altmode_config *cfg = dev->config;
	struct i2c_msg msg;

	/*
	 * Write sequence
	 * DEV_ADDR - REG_ID - DATA_LEN - DATA0 .. DATAn
	 */
	buf[0] = INTEL_ALTMODE_REG_DATA_CONTROL;
	buf[1] = INTEL_ALTMODE_DATA_CONTROL_REG_LEN;
	memcpy(&buf[2], data->raw_value, INTEL_ALTMODE_DATA_CONTROL_REG_LEN);

	msg.buf = (uint8_t *)&buf;
	msg.len = INTEL_ALTMODE_DATA_CONTROL_REG_LEN + 2;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, &msg, 1);
}

static bool intel_altmode_is_interrupted(const struct device *dev)
{
	const struct pd_altmode_config *cfg = dev->config;

	return gpio_pin_get_dt(&cfg->int_gpio);
}

static void intel_altmode_set_result_cb(const struct device *dev,
					intel_altmode_callback cb)
{
	struct pd_altmode_data *data = dev->data;
	const struct pd_altmode_config *cfg = data->dev->config;

	if (!cfg->shared_irq) {
		data->isr_cb = cb;
	}
}

static const struct intel_altmode_driver_api intel_pd_altmode_driver_api = {
	.read_status = intel_altmode_read_status,
	.write_control = intel_altmode_write_control,
	.is_interrupted = intel_altmode_is_interrupted,
	.set_result_cb = intel_altmode_set_result_cb,
};

static void pd_altmode_gpio_callback(const struct device *dev,
				     struct gpio_callback *cb, uint32_t pins)
{
	struct pd_altmode_data *data =
		CONTAINER_OF(cb, struct pd_altmode_data, gpio_cb);
	const struct pd_altmode_config *cfg = data->dev->config;

	if (!cfg->shared_irq) {
		k_work_submit(&data->work);
	}
}

static void pd_altmode_isr_work(struct k_work *item)
{
	struct pd_altmode_data *data =
		CONTAINER_OF(item, struct pd_altmode_data, work);
	const struct pd_altmode_config *cfg = data->dev->config;

	/*
	 * Trigger ISR callback on non-shared interrupt port only
	 * and only after the application has registered the callback.
	 */
	if (!cfg->shared_irq && data->isr_cb) {
		data->isr_cb();
	}
}

static int intel_altmode_init(const struct device *dev)
{
	const struct pd_altmode_config *cfg = dev->config;
	struct pd_altmode_data *data = dev->data;
	int rv;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C is not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->int_gpio)) {
		LOG_ERR("GPIO is not ready");
		return -ENODEV;
	}

	data->dev = dev;

	/* Configure interrupt for the primary port in case of a dual port PD */
	if (!cfg->shared_irq) {
		rv = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
		if (rv < 0) {
			LOG_ERR("Unable to configure GPIO");
			return rv;
		}

		gpio_init_callback(&data->gpio_cb, pd_altmode_gpio_callback,
				   BIT(cfg->int_gpio.pin));

		k_work_init(&data->work, pd_altmode_isr_work);

		rv = gpio_add_callback(cfg->int_gpio.port, &data->gpio_cb);
		if (rv < 0) {
			LOG_ERR("Unable to add callback");
			return rv;
		}

		rv = gpio_pin_interrupt_configure_dt(&cfg->int_gpio,
						     GPIO_INT_LEVEL_LOW);
		if (rv < 0) {
			LOG_ERR("Unable to configure interrupt");
			return rv;
		}
	}

	return 0;
}

#define INTEL_ALTMODE_DEFINE(inst)                                        \
	static struct pd_altmode_data pd_altmode_data_##inst;             \
                                                                          \
	static const struct pd_altmode_config pd_altmode_config##inst = { \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                        \
		.int_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),       \
		.shared_irq = DT_INST_PROP(inst, irq_shared),             \
	};                                                                \
                                                                          \
	DEVICE_DT_INST_DEFINE(inst, intel_altmode_init, NULL,             \
			      &pd_altmode_data_##inst,                    \
			      &pd_altmode_config##inst, POST_KERNEL,      \
			      CONFIG_APPLICATION_INIT_PRIORITY,           \
			      &intel_pd_altmode_driver_api);

DT_INST_FOREACH_STATUS_OKAY(INTEL_ALTMODE_DEFINE)
