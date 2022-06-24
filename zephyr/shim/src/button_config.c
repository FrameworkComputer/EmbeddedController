/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "include/button.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "button_config.h"

LOG_MODULE_REGISTER(button_cfg, LOG_LEVEL_ERR);

/* LCOV_EXCL_START */
#ifdef TEST_BUILD
test_mockable int stub_gpio_pin_get(const struct device *d, gpio_pin_t p)
{
	LOG_DBG("Calling %s\n", __func__);
	return gpio_pin_get(d, p);
}
test_mockable int stub_gpio_pin_get_raw(const struct device *d, gpio_pin_t p)
{
	LOG_DBG("Calling %s\n", __func__);
	return gpio_pin_get_raw(d, p);
}

#define gpio_pin_get stub_gpio_pin_get
#define gpio_pin_get_raw stub_gpio_pin_get_raw
#endif
/* LCOV_EXCL_STOP */

#define BUTTON_HANDLER_DECLARE(node)                            \
	extern void DT_STRING_TOKEN(DT_PHANDLE(node, gpio_int), \
				    handler)(enum gpio_signal);

#define BUTTON_CFG_DEF(node)                                               \
	{ .name = DT_PROP(node, button_name),                              \
	  .type = DT_PROP_OR(node, button_type, 0),                        \
	  .gpio = GPIO_SIGNAL(DT_PHANDLE(node, spec)),                     \
	  .spec = CROS_EC_GPIO_DT_SPEC_GET(DT_PHANDLE(node, spec), gpios), \
	  .gpio_int_handler =                                              \
		  DT_STRING_TOKEN(DT_PHANDLE(node, gpio_int), handler),    \
	  .gpio_int_flags = DT_PROP(DT_PHANDLE(node, gpio_int), flags),    \
	  .debounce_us = DT_PROP(node, debounce_us),                       \
	  .button_flags = DT_PROP(node, flags) },

#if DT_NODE_EXISTS(DT_BUTTON_CFG_NODE)

DT_FOREACH_CHILD(DT_BUTTON_CFG_NODE, BUTTON_HANDLER_DECLARE)

static const struct button_config_v2 button_configs[] = { DT_FOREACH_CHILD(
	DT_BUTTON_CFG_NODE, BUTTON_CFG_DEF) };

static struct gpio_callback int_cb_data[BUTTON_CFG_COUNT];

static bool button_is_valid(enum button_cfg_type type)
{
	return (type >= 0 && type < BUTTON_CFG_COUNT);
}

const struct button_config_v2 *button_cfg_get(enum button_cfg_type type)
{
	const struct button_config_v2 *cfg = NULL;

	if (button_is_valid(type)) {
		cfg = &button_configs[type];
	}

	return cfg;
}

const char *button_get_name(enum button_cfg_type type)
{
	const char *name = "NULL";
	const struct button_config_v2 *cfg = button_cfg_get(type);

	if (cfg) {
		name = cfg->name;
	}

	return name;
}

int button_get_debounce_us(enum button_cfg_type type)
{
	int debounce_time = 0;
	const struct button_config_v2 *cfg = button_cfg_get(type);

	if (cfg) {
		debounce_time = cfg->debounce_us;
	}

	return debounce_time;
}

void button_cb_handler(const struct device *dev, struct gpio_callback *cbdata,
		       uint32_t pins)
{
	const struct button_config_v2 *cfg =
		button_cfg_get(cbdata - &int_cb_data[0]);

	if (cfg) {
		cfg->gpio_int_handler(cfg->gpio);
	}
}

int button_enable_interrupt(enum button_cfg_type type)
{
	int retval = EC_ERROR_INVAL;
	const struct button_config_v2 *cfg = button_cfg_get(type);
	struct gpio_callback *cb;
	gpio_flags_t flags;

	if (cfg) {
		if (cfg->gpio_int_handler) {
			cb = &int_cb_data[type];
			gpio_init_callback(cb, button_cb_handler,
					   BIT(cfg->spec.pin));
			gpio_add_callback(cfg->spec.port, cb);
		}
		flags = (cfg->gpio_int_flags | GPIO_INT_ENABLE) &
			~GPIO_INT_DISABLE;

		retval = gpio_pin_interrupt_configure(cfg->spec.port,
						      cfg->spec.pin, flags);
	}

	return retval;
}

int button_disable_interrupt(enum button_cfg_type type)
{
	int retval = EC_ERROR_INVAL;
	const struct button_config_v2 *cfg = button_cfg_get(type);

	if (cfg) {
		retval = gpio_pin_interrupt_configure(
			cfg->spec.port, cfg->spec.pin, GPIO_INT_DISABLE);
	}

	return retval;
}

static int is_pressed(enum button_cfg_type type,
		      int (*gpio_pin_get_fn)(const struct device *, gpio_pin_t))
{
	int pressed = 0;
	const struct button_config_v2 *cfg = button_cfg_get(type);

	if (cfg) {
		pressed = gpio_pin_get_fn(cfg->spec.port, cfg->spec.pin);
		if (pressed < 0) {
			LOG_ERR("Cannot read %s (%d)", cfg->name, type);
			pressed = 0;
		}
	}

	return pressed;
}

int button_is_pressed(enum button_cfg_type type)
{
	return is_pressed(type, gpio_pin_get);
}

int button_is_pressed_raw(enum button_cfg_type type)
{
	return is_pressed(type, gpio_pin_get_raw);
}

#endif /* DT_NODE_EXISTS(DT_BUTTON_CFG_NODE) */
