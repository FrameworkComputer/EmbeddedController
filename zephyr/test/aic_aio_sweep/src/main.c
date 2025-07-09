/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

static const struct gpio_dt_spec gpio[3] = { DT_FOREACH_PROP_ELEM_SEP(
	DT_NODELABEL(aic_pins), io_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, )) };

static enum {
	SWEEP_OUTPUT,
	SWEEP_0,
	SWEEP_1,
	SWEEP_2,
	SWEEP_INPUT,
	SWEEP_DONE,
} sweep_phase;

#define ALL_GPIO (BIT(0) | BIT(1) | BIT(2))

static void gpio_sweep_cb(const struct device *gpio_dev,
			  struct gpio_callback *cb, uint32_t pins)
{
	uint8_t gpios_out = 0;
	uint8_t gpios_dir = 0;
	gpio_flags_t flags;

	for (uint8_t i = 0; i < ARRAY_SIZE(gpio); i++) {
		if (gpio_emul_output_get(gpio[i].port, gpio[i].pin)) {
			gpios_out |= BIT(i);
		}

		gpio_emul_flags_get(gpio[i].port, gpio[i].pin, &flags);
		if (flags & GPIO_OUTPUT) {
			gpios_dir |= BIT(i);
		}
	}

	TC_PRINT("sweep gpio cb: phase=%d gpios_out=%02x gpios_dir=%02x\n",
		 sweep_phase, gpios_out, gpios_dir);

	switch (sweep_phase) {
	case SWEEP_OUTPUT:
		if (gpios_dir == ALL_GPIO) {
			sweep_phase = SWEEP_0;
		}
		return;
	case SWEEP_0:
		if (gpios_dir == ALL_GPIO && gpios_out == BIT(0)) {
			sweep_phase = SWEEP_1;
		}
		return;
	case SWEEP_1:
		if (gpios_dir == ALL_GPIO && gpios_out == BIT(1)) {
			sweep_phase = SWEEP_2;
		}
		return;
	case SWEEP_2:
		if (gpios_dir == ALL_GPIO && gpios_out == BIT(1)) {
			sweep_phase = SWEEP_INPUT;
		}
		return;
	case SWEEP_INPUT:
		if (gpios_dir == 0) {
			sweep_phase = SWEEP_DONE;
		}
		return;
	case SWEEP_DONE:
		return;
	}
}

ZTEST(ec_aic_sweep, test_sweep)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();
	static struct gpio_callback gpio_cb;

	gpio_init_callback(&gpio_cb, gpio_sweep_cb,
			   BIT(gpio[0].pin) | BIT(gpio[1].pin) |
				   BIT(gpio[2].pin));
	zassert_ok(gpio_add_callback_dt(&gpio[0], &gpio_cb));
	zassert_ok(gpio_add_callback_dt(&gpio[1], &gpio_cb));
	zassert_ok(gpio_add_callback_dt(&gpio[2], &gpio_cb));

	sweep_phase = SWEEP_OUTPUT;

	zassert_ok(shell_execute_cmd(sh, "io_sweep"));

	zassert_equal(sweep_phase, SWEEP_DONE);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);
}

ZTEST_SUITE(ec_aic_sweep, NULL, NULL, reset, reset, NULL);
