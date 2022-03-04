/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ioex_port
#define DT_DRV_COMPAT_CHIP cros_ioex_chip

#include <device.h>
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <errno.h>
#ifdef __REQUIRE_ZEPHYR_GPIOS__
#undef __REQUIRE_ZEPHYR_GPIOS__
#endif
#include "gpio.h"
#include <gpio/gpio_utils.h>
#include <init.h>
#include <kernel.h>
#include <logging/log.h>
#include <sys/byteorder.h>
#include <sys/util.h>
#include "common.h"
#include "config.h"
#include "i2c.h"
#include "ioexpander.h"

/* Include drivers if enabled */
#ifdef CONFIG_PLATFORM_EC_IOEX_CCGXXF
#include "driver/tcpm/ccgxxf.h"
#endif
#ifdef CONFIG_PLATFORM_EC_IOEX_IT8801
#include "driver/ioexpander/it8801.h"
#endif
#ifdef CONFIG_PLATFORM_EC_IOEX_NCT38XX
#include "driver/tcpm/nct38xx.h"
#endif
#ifdef CONFIG_PLATFORM_EC_IOEX_PCA9675
#include "driver/ioexpander/pca9675.h"
#endif
#ifdef CONFIG_PLATFORM_EC_IOEX_PCAL6408
#include "driver/ioexpander/pcal6408.h"
#endif
#ifdef CONFIG_PLATFORM_EC_IOEX_TCA64XXA
#include "driver/ioexpander/tca64xxa.h"
#endif

LOG_MODULE_REGISTER(cros_ioex_port, CONFIG_GPIO_LOG_LEVEL);

struct ioex_drv_data {
	const struct device *dev;
	int ioex;
	int port;

	sys_slist_t callbacks;
	struct k_work worker;

	const struct device *int_gpio_dev;
	gpio_pin_t int_gpio_pin;
	gpio_flags_t int_gpio_flags;
	struct gpio_callback int_gpio_callback;
	gpio_port_value_t cached_values;
	gpio_port_value_t pin_trig_edge_rising;
	gpio_port_value_t pin_trig_edge_falling;
	gpio_port_value_t pin_trig_level_zero;
	gpio_port_value_t pin_trig_level_one;
};

static int shim_ioex_pin_configure(const struct device *dev, gpio_pin_t pin,
				   gpio_flags_t flags)
{
	const struct ioexpander_config_t *cfg = dev->config;
	struct ioex_drv_data *drv_data = dev->data;
	int res;

	res = cfg->drv->set_flags_by_mask(drv_data->ioex, drv_data->port,
					  BIT(pin),
					  convert_from_zephyr_flags(flags));
	if (res)
		return -EIO;

	return 0;
}

static int shim_ioex_port_get_raw(const struct device *dev,
				  gpio_port_value_t *value)
{
	const struct ioexpander_config_t *cfg = dev->config;
	struct ioex_drv_data *drv_data = dev->data;
	int res;

	res = cfg->drv->get_port(drv_data->ioex, drv_data->port, value);
	if (res)
		return -EIO;

	return 0;
}

static int shim_ioex_port_set_masked_raw(const struct device *dev,
					 gpio_port_pins_t mask,
					 gpio_port_value_t value)
{
	const struct ioexpander_config_t *cfg = dev->config;
	struct ioex_drv_data *drv_data = dev->data;
	int res;

	res = cfg->drv->set_level(drv_data->ioex, drv_data->port, mask, value);
	if (res)
		return -EIO;

	return 0;
}

static int shim_ioex_port_set_bits_raw(const struct device *dev,
				       gpio_port_pins_t pins)
{
	const struct ioexpander_config_t *cfg = dev->config;
	struct ioex_drv_data *drv_data = dev->data;
	int res;

	res = cfg->drv->set_level(drv_data->ioex, drv_data->port, pins, 1);
	if (res)
		return -EIO;

	return 0;
}

static int shim_ioex_port_clear_bits_raw(const struct device *dev,
					 gpio_port_pins_t pins)
{
	const struct ioexpander_config_t *cfg = dev->config;
	struct ioex_drv_data *drv_data = dev->data;
	int res;

	res = cfg->drv->set_level(drv_data->ioex, drv_data->port, pins, 0);
	if (res)
		return -EIO;

	return 0;
}

static int shim_ioex_port_toggle_bits(const struct device *dev,
				      gpio_port_pins_t pins)
{
	const struct ioexpander_config_t *cfg = dev->config;
	struct ioex_drv_data *drv_data = dev->data;
	int to_set;
	int to_clr;
	int res;
	int val;

	res = cfg->drv->get_port(drv_data->ioex, drv_data->port, &val);
	if (res)
		return -EIO;

	to_set = (~val) & pins;
	to_clr = val & pins;

	res = cfg->drv->set_level(drv_data->ioex, drv_data->port, to_set, 1);
	if (res)
		return -EIO;

	res = cfg->drv->set_level(drv_data->ioex, drv_data->port, to_clr, 0);
	if (res)
		return -EIO;

	return 0;
}

static int shim_ioex_pin_interrupt_configure(const struct device *dev,
					     gpio_pin_t pin,
					     enum gpio_int_mode mode,
					     enum gpio_int_trig trig)
{
	const struct ioexpander_config_t *cfg = dev->config;
	struct ioex_drv_data *drv_data = dev->data;
	int flags;
	int res;

	if (!drv_data->int_gpio_dev) {
		LOG_ERR("Trying to enable interrupt on ioex %d without "
			"defined IO expander interrupt pin",
			drv_data->ioex);
		return -EIO;
	}

	res = cfg->drv->get_flags_by_mask(drv_data->ioex, drv_data->port,
					  BIT(pin), &flags);
	if (res)
		return -EIO;

	flags |= convert_from_zephyr_flags(mode | trig);

	res = cfg->drv->set_flags_by_mask(drv_data->ioex, drv_data->port,
					  BIT(pin), flags);
	if (res)
		return -EIO;

	if (!cfg->drv->enable_interrupt) {
		LOG_ERR("Trying to enable interrupt on ioex %d which doesn't "
			"support interrupts",
			drv_data->ioex);
		return -EIO;
	}

	res = cfg->drv->enable_interrupt(drv_data->ioex, drv_data->port,
					 BIT(pin), (mode & GPIO_INT_ENABLE));
	if (res)
		return -EIO;

	if (mode == GPIO_INT_MODE_DISABLED) {
		drv_data->pin_trig_edge_rising &= ~BIT(pin);
		drv_data->pin_trig_edge_falling &= ~BIT(pin);
		drv_data->pin_trig_level_zero &= ~BIT(pin);
		drv_data->pin_trig_level_one &= ~BIT(pin);
	} else if (mode == GPIO_INT_MODE_EDGE) {
		if (trig & GPIO_INT_LOW_0)
			drv_data->pin_trig_edge_falling |= BIT(pin);
		if (trig & GPIO_INT_HIGH_1)
			drv_data->pin_trig_edge_rising |= BIT(pin);
	} else {
		if (trig & GPIO_INT_LOW_0)
			drv_data->pin_trig_level_zero |= BIT(pin);
		if (trig & GPIO_INT_HIGH_1)
			drv_data->pin_trig_level_one |= BIT(pin);
	}

	return 0;
}

static void shim_ioex_isr(const struct device *dev,
			  struct gpio_callback *callback, gpio_port_pins_t pins)
{
	struct ioex_drv_data *drv_data =
		CONTAINER_OF(callback, struct ioex_drv_data, int_gpio_callback);

	k_work_submit(&drv_data->worker);
}

static void shim_ioex_worker(struct k_work *worker)
{
	struct ioex_drv_data *drv_data =
		CONTAINER_OF(worker, struct ioex_drv_data, worker);
	const struct ioexpander_drv *drv = ioex_config[drv_data->ioex].drv;
	int interrupted_pins_level = 0;
	int interrupted_pins_edge = 0;
	int interrupted_pins = 0;
	int current_values;
	int changed_pins;

	if (drv_data->ioex < 0) {
		LOG_ERR("Invalid int IOEX");
		return;
	}

	if (!drv->get_port) {
		LOG_ERR("IO expander doesn't support get_port function");
		return;
	}

	if (drv->get_port(drv_data->ioex, drv_data->port, &current_values)) {
		LOG_ERR("Couldn't get int ioex values");
		return;
	}

	changed_pins = current_values ^ drv_data->cached_values;

	/* Edge rising */
	interrupted_pins_edge |= (changed_pins & current_values) &
				 drv_data->pin_trig_edge_rising;
	/* Edge falling */
	interrupted_pins_edge |= (changed_pins & (~current_values)) &
				 drv_data->pin_trig_edge_falling;
	/* Level 1 */
	interrupted_pins_level |=
		(current_values & drv_data->pin_trig_level_one);
	/* Level 0 */
	interrupted_pins_level |=
		((~current_values) & drv_data->pin_trig_level_zero);

	interrupted_pins = (interrupted_pins_edge | interrupted_pins_level);
	gpio_fire_callbacks(&drv_data->callbacks, drv_data->dev,
			    interrupted_pins);

	drv_data->cached_values = current_values;

	/* Recalling this function will simulate interrupts triggered by
	 * logic level instead of level change (edge).
	 * Function will be called repeatedly until the level change to value
	 * not triggering the interrupt.
	 */
	if (interrupted_pins_level)
		k_work_submit(worker);
}

static int shim_ioex_init(const struct device *dev)
{
	struct ioex_drv_data *drv_data = dev->data;

	drv_data->dev = dev;

	/* IO expander may have specified GPIO pin that should trigger
	 * interrupt handling routines for signals on this IO expander.
	 * If this GPIO is specified, it should be configured as interrupt
	 * pin and should have callback assigned to it.
	 */
	if (drv_data->int_gpio_dev) {
		int res;

		res = gpio_pin_configure(drv_data->int_gpio_dev,
					 drv_data->int_gpio_pin,
					 drv_data->int_gpio_flags | GPIO_INPUT);
		if (res)
			return -EIO;

		gpio_init_callback(&drv_data->int_gpio_callback, shim_ioex_isr,
				   BIT(drv_data->int_gpio_pin));

		res = gpio_add_callback(drv_data->int_gpio_dev,
					&drv_data->int_gpio_callback);
		if (res)
			return -EIO;

		k_work_init(&drv_data->worker, shim_ioex_worker);
	}

	return 0;
}

static int shim_ioex_manage_callback(const struct device *dev,
				     struct gpio_callback *callback,
				     bool enable)
{
	struct ioex_drv_data *drv_data = dev->data;

	return gpio_manage_callback(&drv_data->callbacks, callback, enable);
}

static const struct gpio_driver_api api_table = {
	.pin_configure = shim_ioex_pin_configure,
	.port_get_raw = shim_ioex_port_get_raw,
	.port_set_masked_raw = shim_ioex_port_set_masked_raw,
	.port_set_bits_raw = shim_ioex_port_set_bits_raw,
	.port_clear_bits_raw = shim_ioex_port_clear_bits_raw,
	.port_toggle_bits = shim_ioex_port_toggle_bits,
	.pin_interrupt_configure = shim_ioex_pin_interrupt_configure,
	.manage_callback = shim_ioex_manage_callback,
};

#define IOEX_INIT_CONFIG_ELEM(id)                                    \
	{                                                            \
		.i2c_host_port = I2C_PORT(DT_PHANDLE(id, i2c_port)), \
		.i2c_addr_flags = DT_PROP(id, i2c_addr),             \
		.drv = &DT_STRING_TOKEN(id, drv),                    \
		.flags = DT_PROP(id, flags),                         \
	},

#define IOEX_INIT_DATA(idx)                                                  \
	{                                                                    \
		.ioex = IOEXPANDER_ID(DT_PARENT(DT_DRV_INST(idx))),          \
		.port = DT_REG_ADDR(DT_DRV_INST(idx)),                       \
		COND_CODE_1(                                                 \
			DT_NODE_HAS_PROP(DT_PARENT(DT_DRV_INST(idx)),        \
					 int_gpios),                         \
			(.int_gpio_dev = DEVICE_DT_GET(DT_PHANDLE(           \
				 DT_PARENT(DT_DRV_INST(idx)), int_gpios)),   \
			 .int_gpio_pin = DT_GPIO_PIN(                        \
				 DT_PARENT(DT_DRV_INST(idx)), int_gpios),    \
			 .int_gpio_flags = DT_GPIO_FLAGS(                    \
				 DT_PARENT(DT_DRV_INST(idx)), int_gpios), ), \
			())                                                  \
	}

struct ioexpander_config_t ioex_config[] = { DT_FOREACH_STATUS_OKAY(
	DT_DRV_COMPAT_CHIP, IOEX_INIT_CONFIG_ELEM) };

#define GPIO_PORT_INIT(idx)                                                  \
	static struct ioex_drv_data ioex_##idx##_data = IOEX_INIT_DATA(idx); \
	DEVICE_DT_INST_DEFINE(                                               \
		idx, shim_ioex_init, NULL, &ioex_##idx##_data,               \
		&ioex_config[IOEXPANDER_ID(DT_PARENT(DT_DRV_INST(idx)))],    \
		POST_KERNEL, CONFIG_PLATFORM_EC_IOEX_INIT_PRIORITY,          \
		&api_table);

DT_INST_FOREACH_STATUS_OKAY(GPIO_PORT_INIT)
