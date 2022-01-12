/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <devicetree.h>
#include <init.h>
#include <kernel.h>
#include <logging/log.h>
#include "gpio.h"
#include "gpio/gpio.h"
#include "i2c.h"
#include "ioexpander.h"
#include "system.h"
#include "util.h"

#if DT_NODE_EXISTS(DT_PATH(named_ioexes))

LOG_MODULE_REGISTER(ioex_shim, LOG_LEVEL_ERR);

struct ioex_gpio_config {
	/* IOEX signal name */
	const char *name;
	/* Device pointer to GPIO driver */
	const struct device *dev;
	/* Bit number of the pin on the IOEX port */
	gpio_pin_t pin;
	/* From DTS, excludes interrupts flags */
	gpio_flags_t init_flags;
	/*
	 * Index of CrOS IO expander chip
	 * If IO expander uses CrOS EC driver, this value will be one
	 * of the possible from enum ioexpander_id
	 * otherwise, if using the Zephyr GPIO driver, this will be -1
	 */
	int cros_drv_index;
	/* Port of IO expander. Valid only if ioex field is not -1 */
	int port;
};

#ifdef CONFIG_PLATFORM_EC_IOEX_CROS_DRV
#define IOEX_IS_CROS_DRV(config) (config->cros_drv_index >= 0)
#else
/*
 * If no legacy cros-ec IOEX drivers are used, we need a stub
 * symbol for ioex_config[].  Set the IOEX_IS_CROS_DRV to constant 0
 * which will cause all these checks to compile out.
 */
#define IOEX_IS_CROS_DRV(config) 0
struct ioexpander_config_t ioex_config[0];
#endif

struct ioex_int_config {
	const enum ioex_signal signal;
	const gpio_flags_t flags;

	void (*const handler)(enum gpio_signal);
	struct gpio_callback callback;
};

/* Check IOEX interrupts flags */
#define IOEX_INT(sig, f, cb)                       \
	BUILD_ASSERT(VALID_GPIO_INTERRUPT_FLAG(f), \
		     STRINGIFY(sig) " is not using Zephyr interrupt flags");
#ifdef EC_CROS_IOEX_INTERRUPTS
EC_CROS_IOEX_INTERRUPTS
#endif
#undef IOEX_INT

/* Declare handlers */
#ifdef EC_CROS_IOEX_INTERRUPTS
#define IOEX_INT(arg_signal, arg_flags, arg_handler) \
	void arg_handler(enum gpio_signal);
EC_CROS_IOEX_INTERRUPTS
#undef IOEX_INT
#endif /* EC_CROS_IOEX_INTERRUPTS */

#define IOEX_INT(arg_signal, arg_flags, arg_handler) \
{ \
	.signal = arg_signal, \
	.flags = arg_flags, \
	.handler = arg_handler, \
},

struct ioex_int_config ioex_int_configs[] = {
#ifdef EC_CROS_IOEX_INTERRUPTS
	EC_CROS_IOEX_INTERRUPTS
#endif
};
#undef IOEX_INT

#define CHIP_FROM_GPIO(id) DT_PARENT(DT_GPIO_CTLR(id, gpios))

#define IOEX_GPIO_CONFIG(id)                                                \
	{                                                                   \
		.name = DT_LABEL(id),                                       \
		.dev = DEVICE_DT_GET(DT_PHANDLE(id, gpios)),                \
		.pin = DT_GPIO_PIN(id, gpios),                              \
		.init_flags = DT_GPIO_FLAGS(id, gpios),                     \
		.cros_drv_index =                                           \
			COND_CODE_1(DT_NODE_HAS_COMPAT(CHIP_FROM_GPIO(id),  \
				cros_ioex_chip),                            \
			(IOEXPANDER_ID(CHIP_FROM_GPIO(id)), ),              \
			(-1,))                                              \
		.port = DT_REG_ADDR(DT_GPIO_CTLR(id, gpios))                \
	},

#define IOEX_INIT_FLAGS(id) 0,

static const struct ioex_gpio_config ioex_gpio_configs[] = {
	DT_FOREACH_CHILD(DT_PATH(named_ioexes), IOEX_GPIO_CONFIG)
};

static gpio_flags_t ioex_signals_flags[] = {
	DT_FOREACH_CHILD(DT_PATH(named_ioexes), IOEX_INIT_FLAGS)
};
BUILD_ASSERT(ARRAY_SIZE(ioex_signals_flags) == IOEX_COUNT);

int signal_is_ioex(int signal)
{
	return ((signal >= IOEX_SIGNAL_START) && (signal < IOEX_SIGNAL_END));
}

static struct ioex_int_config *get_interrupt_from_signal(
	enum ioex_signal signal)
{
	for (size_t i = 0; i < ARRAY_SIZE(ioex_int_configs); i++) {
		if (ioex_int_configs[i].signal == signal)
			return &ioex_int_configs[i];
	}

	LOG_ERR("No interrupt defined for GPIO %s",
		ioex_gpio_configs[signal - IOEX_SIGNAL_START].name);

	return NULL;
}

static const struct ioex_gpio_config *ioex_get_signal_info(
	enum ioex_signal signal)
{
	const struct ioex_gpio_config *g;

	ASSERT(signal_is_ioex(signal));

	g = ioex_gpio_configs + signal - IOEX_SIGNAL_START;

	if (IOEX_IS_CROS_DRV(g) &&
	    !(ioex_config[g->cros_drv_index].flags & IOEX_FLAGS_INITIALIZED)) {
		LOG_ERR("ioex %s disabled", g->name);
		return NULL;
	}

	return g;
}

int ioex_enable_interrupt(enum ioex_signal signal)
{
	struct ioex_int_config *cfg = get_interrupt_from_signal(signal);
	int offset = (signal - IOEX_SIGNAL_START);
	int res;

	if (!cfg)
		return EC_ERROR_PARAM1;

	res = gpio_pin_interrupt_configure(ioex_gpio_configs[offset].dev,
					   ioex_gpio_configs[offset].pin,
					   (cfg->flags | GPIO_INT_ENABLE)
						& ~GPIO_INT_DISABLE);

	if (res)
		LOG_ERR("Can't enable interrupt on %s",
			ioex_gpio_configs[offset].name);

	return res;
}

int ioex_disable_interrupt(enum ioex_signal signal)
{
	struct ioex_int_config *cfg = get_interrupt_from_signal(signal);
	int offset = (signal - IOEX_SIGNAL_START);
	int res;

	if (!cfg)
		return EC_ERROR_PARAM1;

	res = gpio_pin_interrupt_configure(ioex_gpio_configs[offset].dev,
					   ioex_gpio_configs[offset].pin,
					   GPIO_INT_DISABLE);

	if (res)
		LOG_ERR("Can't disable interrupt on %s",
			ioex_gpio_configs[offset].name);

	return res;
}

int ioex_get_flags(enum ioex_signal signal, int *flags)
{
	if (!signal_is_ioex(signal))
		return EC_ERROR_INVAL;

	*flags = convert_from_zephyr_flags(
		ioex_signals_flags[signal - IOEX_SIGNAL_START]);

	return EC_SUCCESS;
}

int ioex_set_flags(enum ioex_signal signal, int flags)
{
	const struct ioex_gpio_config *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_INVAL;

	if (gpio_pin_configure(g->dev,
			       g->pin,
			       convert_to_zephyr_flags(flags)) < 0) {
		return EC_ERROR_UNKNOWN;
	}

	ioex_signals_flags[signal - IOEX_SIGNAL_START] =
						convert_to_zephyr_flags(flags);

	return EC_SUCCESS;
}

int ioex_get_level(enum ioex_signal signal, int *val)
{
	const struct ioex_gpio_config *g = ioex_get_signal_info(signal);
	int res;

	if (g == NULL)
		return EC_ERROR_INVAL;

	res = gpio_pin_get_raw(g->dev, g->pin);
	if (res < 0)
		return EC_ERROR_UNKNOWN;

	*val = res;

	return EC_SUCCESS;
}

int ioex_set_level(enum ioex_signal signal, int value)
{
	const struct ioex_gpio_config *g = ioex_get_signal_info(signal);
	int res;

	if (g == NULL)
		return EC_ERROR_INVAL;

	res = gpio_pin_set_raw(g->dev, g->pin, value);
	if (res)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int ioex_get_port(int ioex, int port, int *val)
{
	return EC_ERROR_UNIMPLEMENTED;
}

static void ioex_isr(const struct device *port,
		     struct gpio_callback *cb,
		     gpio_port_pins_t pins)
{
	struct ioex_int_config *cfg =
			CONTAINER_OF(cb, struct ioex_int_config, callback);

	cfg->handler(cfg->signal);
}

int ioex_init(int ioex)
{
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_IOEX_CROS_DRV))
		return EC_SUCCESS;

	const struct ioexpander_drv *drv = ioex_config[ioex].drv;
	int rv;

	if (ioex_config[ioex].flags & IOEX_FLAGS_INITIALIZED)
		return EC_SUCCESS;

	if (drv->init != NULL) {
		rv = drv->init(ioex);
		if (rv != EC_SUCCESS)
			return rv;
	}

	ioex_config[ioex].flags |= IOEX_FLAGS_INITIALIZED;

	return EC_SUCCESS;
}

static int ioex_init_default(const struct device *unused)
{
	int ret;
	int i;

	ARG_UNUSED(unused);

	for (i = 0; i < CONFIG_IO_EXPANDER_PORT_COUNT; i++) {
		/* IO Expander has been initialized, skip re-initializing */
		if (ioex_config[i].flags & (IOEX_FLAGS_INITIALIZED |
					IOEX_FLAGS_DEFAULT_INIT_DISABLED))
			continue;

		ret = ioex_init(i);
		if (ret)
			LOG_ERR("Can't initialize ioex %d", i);
	}

	/*
	 * Set all IO expander GPIOs to default flags according to the setting
	 * in device tree
	 */
	for (i = 0; i < IOEX_COUNT; i++) {
		const struct ioex_gpio_config *g =
				ioex_get_signal_info(IOEX_SIGNAL_START + i);
		int flags;

		if (!g)
			continue;

		flags = g->init_flags;
		/* Late-sysJump should not set the output levels */
		if (system_jumped_late())
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		ret = gpio_pin_configure(g->dev, g->pin, flags);
		if (ret)
			LOG_ERR("Can't configure %s", g->name);

		ioex_signals_flags[i] = g->init_flags;
	}

	/* Init interrupts */
	for (i = 0; i < ARRAY_SIZE(ioex_int_configs); i++) {
		int offset = ioex_int_configs[i].signal - IOEX_SIGNAL_START;

		gpio_init_callback(&ioex_int_configs[i].callback,
				   ioex_isr,
				   BIT(ioex_gpio_configs[offset].pin));
		ret = gpio_add_callback(ioex_gpio_configs[offset].dev,
					&ioex_int_configs[i].callback);
		if (ret)
			LOG_ERR("Can't add callback to %s",
				ioex_gpio_configs[offset].name);
	}

	return 0;
}
SYS_INIT(ioex_init_default, POST_KERNEL, CONFIG_PLATFORM_EC_IOEX_INIT_PRIORITY);

const char *ioex_get_name(enum ioex_signal signal)
{
	const struct ioex_gpio_config *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return NULL;

	return g->name;
}

int ioex_get_ioex_flags(enum ioex_signal signal, int *val)
{
	const struct ioex_gpio_config *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_INVAL;

	if (!IOEX_IS_CROS_DRV(g)) {
		/* Zephyr gpio drivers are initialized by internal subsystem */
		*val = IOEX_FLAGS_INITIALIZED;
		return EC_SUCCESS;
	}

	*val = ioex_config[g->cros_drv_index].flags;

	return EC_SUCCESS;
}

#endif
