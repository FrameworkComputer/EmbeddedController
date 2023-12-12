/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio.h"
#include "soc_gpio.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(shim_cros_gpio, LOG_LEVEL_ERR);

static const struct unused_pin_config unused_pin_configs[] = {
	UNUSED_GPIO_CONFIG_LIST
};

int gpio_config_unused_pins(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(unused_pin_configs); ++i) {
		int rv;
		int flags;
		const struct device *dev =
			device_get_binding(unused_pin_configs[i].dev_name);

		if (dev == NULL) {
			LOG_ERR("Not found (%s)",
				unused_pin_configs[i].dev_name);
			return -ENOTSUP;
		}

		/*
		 * Set the default setting for the floating IOs. The floating
		 * IOs cause the leakage current. Set unused pins as input with
		 * internal PU to prevent extra power consumption.
		 */
		if (unused_pin_configs[i].flags == 0)
			flags = GPIO_INPUT | GPIO_PULL_UP;
		else
			flags = unused_pin_configs[i].flags;

		rv = gpio_pin_configure(dev, unused_pin_configs[i].pin, flags);

		if (rv < 0) {
			LOG_ERR("Config failed %s-%d (%d)",
				unused_pin_configs[i].dev_name,
				unused_pin_configs[i].pin, rv);
			return rv;
		}
	}

	return 0;
}

int gpio_configure_port_pin(int port, int id, int flags)
{
	const struct device *dev = npcx_get_gpio_dev(port);

	return gpio_pin_configure(dev, id, flags);
}

#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_GPIODBG
/*
 * IO information about each GPIO that is configured in the `named_gpios` and
 *` unused_pins` device tree nodes.
 */
struct npcx_io_info {
	/* A npcx gpio port device */
	const struct device *dev;
	/* A npcx gpio port number */
	int port;
	/* Bit number of pin within a npcx gpio port */
	gpio_pin_t pin;
	/* GPIO net name */
	const char *name;
	/* Enable flag of npcx gpio input buffer */
	bool enable;
};

#define NAMED_GPIO_INFO(node)                                      \
	{                                                          \
		.dev = DEVICE_DT_GET(DT_GPIO_CTLR(node, gpios)),   \
		.port = DT_PROP(DT_GPIO_CTLR(node, gpios), index), \
		.pin = DT_GPIO_PIN(node, gpios),                   \
		.name = DT_NODE_FULL_NAME(node),                   \
		.enable = true,                                    \
	},

#define UNUSED_GPIO_INFO(node, prop, idx)                                     \
	{                                                                     \
		.dev = DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(node, prop, idx)),   \
		.port = DT_PROP(DT_GPIO_CTLR_BY_IDX(node, prop, idx), index), \
		.pin = DT_GPIO_PIN_BY_IDX(node, prop, idx),                   \
		.name = "unused pin",                                         \
		.enable = true,                                               \
	},

#define NAMED_GPIO_IS_ON_CHIP_GPIO(node)                          \
	COND_CODE_1(DT_NODE_HAS_COMPAT(DT_GPIO_CTLR(node, gpios), \
				       nuvoton_npcx_gpio),        \
		    (NAMED_GPIO_INFO(node)), ())

#define NAMED_GPIO_INIT(node)                      \
	COND_CODE_1(DT_NODE_HAS_PROP(node, gpios), \
		    (NAMED_GPIO_IS_ON_CHIP_GPIO(node)), ())

static struct npcx_io_info gpio_info[] = {
#if DT_NODE_EXISTS(NAMED_GPIOS_NODE)
	DT_FOREACH_CHILD(NAMED_GPIOS_NODE, NAMED_GPIO_INIT)
#endif
#if DT_NODE_EXISTS(UNUSED_GPIOS_NODE)
		DT_FOREACH_PROP_ELEM(UNUSED_GPIOS_NODE, unused_gpios,
				     UNUSED_GPIO_INFO)
#endif
};

static int get_index_from_arg(const struct shell *sh, char **argv, int *index)
{
	char *end_ptr;
	int num = strtol(argv[1], &end_ptr, 0);
	const int gpio_cnt = ARRAY_SIZE(gpio_info);

	if (*end_ptr != '\0') {
		shell_error(sh, "Failed to parse %s", argv[1]);
		return -EINVAL;
	}

	if (num >= gpio_cnt) {
		shell_error(sh, "Index shall be less than %u, was %u", gpio_cnt,
			    num);
		return -EINVAL;
	}

	*index = num;

	return 0;
}

static int cmd_gpio_list_all(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/* Print header */
	shell_print(sh, "IDX|ON| GPIO | Name");
	shell_print(sh, "---+--+------+----------");

	/* List all GPIOs in 'named-gpios' and 'unused_pins' DT nodes */
	for (int i = 0; i < ARRAY_SIZE(gpio_info); i++) {
		shell_print(sh, "%02d |%s | io%x%x | %s", i,
			    gpio_info[i].enable ? "*" : " ", gpio_info[i].port,
			    gpio_info[i].pin, gpio_info[i].name);
	}

	return 0;
}

static int cmd_gpio_turn_on(const struct shell *sh, size_t argc, char **argv)
{
	int index;
	int res = get_index_from_arg(sh, argv, &index);

	if (res < 0) {
		return res;
	}

	/* Turn on GPIO's input buffer by index */
	gpio_info[index].enable = true;
	npcx_gpio_enable_io_pads(gpio_info[index].dev, gpio_info[index].pin);

	return 0;
}

static int cmd_gpio_turn_off(const struct shell *sh, size_t argc, char **argv)
{
	int index;
	int res = get_index_from_arg(sh, argv, &index);

	if (res < 0) {
		return res;
	}

	/* Turn off GPIO's input buffer by index */
	gpio_info[index].enable = false;
	npcx_gpio_disable_io_pads(gpio_info[index].dev, gpio_info[index].pin);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_gpiodbg,
	SHELL_CMD_ARG(list, NULL, "List all GPIOs used on platform by index",
		      cmd_gpio_list_all, 1, 0),
	SHELL_CMD_ARG(on, NULL, "<index_in_list> Turn on GPIO's input buffer",
		      cmd_gpio_turn_on, 2, 0),
	SHELL_CMD_ARG(off, NULL, "<index_in_list> Turn off GPIO's input buffer",
		      cmd_gpio_turn_off, 2, 0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_ARG_REGISTER(gpiodbg, &sub_gpiodbg,
		       "Commands for power consumption "
		       "investigation",
		       NULL, 2, 0);

#endif /* CONFIG_PLATFORM_EC_CONSOLE_CMD_GPIODBG */
