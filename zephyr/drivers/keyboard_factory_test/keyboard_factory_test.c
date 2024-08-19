/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_keyboard_factory_test

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <host_command.h>
#include <keyboard_scan.h>
#include <system.h>

LOG_MODULE_REGISTER(keyboard_factory_test, LOG_LEVEL_INF);

PINCTRL_DT_DEFINE(DT_INST_PARENT(0));

static const struct pinctrl_dev_config *pcfg =
	PINCTRL_DT_DEV_CONFIG_GET(DT_INST_PARENT(0));

#define KBD_FACTORY_SCAN_GPIO(index)                                          \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(0, pin##index##_gpios),              \
		   ([index - 1] = &(                                          \
			    (const struct gpio_dt_spec)GPIO_DT_SPEC_INST_GET( \
				    0, pin##index##_gpios)), ))

const struct gpio_dt_spec *scan_gpios[] = {
	/* clang-format off */
	KBD_FACTORY_SCAN_GPIO(1)
	KBD_FACTORY_SCAN_GPIO(2)
	KBD_FACTORY_SCAN_GPIO(3)
	KBD_FACTORY_SCAN_GPIO(4)
	KBD_FACTORY_SCAN_GPIO(5)
	KBD_FACTORY_SCAN_GPIO(6)
	KBD_FACTORY_SCAN_GPIO(7)
	KBD_FACTORY_SCAN_GPIO(8)
	KBD_FACTORY_SCAN_GPIO(9)
	KBD_FACTORY_SCAN_GPIO(10)
	KBD_FACTORY_SCAN_GPIO(11)
	KBD_FACTORY_SCAN_GPIO(12)
	KBD_FACTORY_SCAN_GPIO(13)
	KBD_FACTORY_SCAN_GPIO(14)
	KBD_FACTORY_SCAN_GPIO(15)
	KBD_FACTORY_SCAN_GPIO(16)
	KBD_FACTORY_SCAN_GPIO(17)
	KBD_FACTORY_SCAN_GPIO(18)
	KBD_FACTORY_SCAN_GPIO(19)
	KBD_FACTORY_SCAN_GPIO(20)
	KBD_FACTORY_SCAN_GPIO(21)
	KBD_FACTORY_SCAN_GPIO(22)
	KBD_FACTORY_SCAN_GPIO(23)
	KBD_FACTORY_SCAN_GPIO(24)
	KBD_FACTORY_SCAN_GPIO(25)
	KBD_FACTORY_SCAN_GPIO(26)
	KBD_FACTORY_SCAN_GPIO(27)
	KBD_FACTORY_SCAN_GPIO(28)
	KBD_FACTORY_SCAN_GPIO(29)
	KBD_FACTORY_SCAN_GPIO(30)
	KBD_FACTORY_SCAN_GPIO(31)
	KBD_FACTORY_SCAN_GPIO(32)
	/* clang-format on */
};

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_col_gpio)
static const struct gpio_dt_spec kso02_gpios =
	GPIO_DT_SPEC_GET(DT_INST(0, cros_ec_col_gpio), col_gpios);
#else
static const struct gpio_dt_spec kso02_gpios;
#endif

#define PIN_SETTLE_TIME_MS 1
#define KBD_SHUTDOWN_TIME_MS 100

static int keyboard_factory_test_scan(void)
{
	uint16_t shorted = 0;
	int ret;

	/* Disable keyboard scan while testing */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_CLOSED);

	/* Give the keyboard driver some time to shut down. */
	k_sleep(K_MSEC(KBD_SHUTDOWN_TIME_MS));

	ret = pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
	if (ret < 0) {
		LOG_ERR("pinctrl_apply_state failed: %d", ret);
		goto done;
	}

	/* Set all of KSO/KSI pins to internal pull-up and input */
	for (uint8_t i = 0; i < ARRAY_SIZE(scan_gpios); i++) {
		const struct gpio_dt_spec *gpio = scan_gpios[i];

		if (gpio == NULL) {
			continue;
		}

		gpio_pin_configure_dt(gpio, GPIO_INPUT | GPIO_PULL_UP);
	}

	k_sleep(K_MSEC(PIN_SETTLE_TIME_MS));

	/*
	 * Set start pin to output low, then check other pins going to low
	 * level, it indicate the two pins are shorted.
	 */
	for (uint8_t i = 0; i < ARRAY_SIZE(scan_gpios); i++) {
		const struct gpio_dt_spec *gpio = scan_gpios[i];

		if (gpio == NULL) {
			continue;
		}

		gpio_pin_configure_dt(gpio, GPIO_OUTPUT_INACTIVE);

		k_sleep(K_MSEC(PIN_SETTLE_TIME_MS));

		for (int j = 0; j < ARRAY_SIZE(scan_gpios); j++) {
			const struct gpio_dt_spec *gpio_in = scan_gpios[j];

			if (gpio_in == NULL || i == j)
				continue;

			if (gpio_pin_get_dt(gpio_in) == 0) {
				shorted = (i + 1) << 8 | (j + 1);
				goto done;
			}
		}

		gpio_pin_configure_dt(gpio, GPIO_INPUT | GPIO_PULL_UP);
	}

done:
	ret = pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("pinctrl_apply_state failed: %d", ret);
		return -1;
	}

	if (kso02_gpios.port != NULL) {
		gpio_pin_configure_dt(&kso02_gpios, GPIO_OUTPUT_INACTIVE);
	}

	keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_CLOSED);

	return shorted;
}

static enum ec_status keyboard_factory_test(struct host_cmd_handler_args *args)
{
	struct ec_response_keyboard_factory_test *r = args->response;

	/* Only available on unlocked systems */
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	r->shorted = keyboard_factory_test_scan();

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_KEYBOARD_FACTORY_TEST, keyboard_factory_test,
		     EC_VER_MASK(0));

static int command_kbfactorytest(int argc, const char **argv)
{
	uint16_t shorted;

	shorted = keyboard_factory_test_scan();

	ccprintf("Keyboard factory test: shorted=%04x (%d, %d)\n", shorted,
		 shorted >> 8, shorted & 0xff);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(kbfactorytest, command_kbfactorytest, "kbfactorytest",
			"Run the keyboard factory test");
