/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <gpio.h>
#include <throttle_ap.h>
#include <timer.h>

#define PROCHOT_ASSERTED \
	!IS_ENABLED(PLATFORM_EC_POWERSEQ_CPU_PROCHOT_ACTIVE_LOW)

/* Waits for the debouncing logic to finish. */
static void debounce_wait(void)
{
	k_usleep(PROCHOT_IN_DEBOUNCE_US);
	k_msleep(1);
}

static bool was_asserted;
static void callback(bool asserted, void *data)
{
	was_asserted = asserted;
}

const struct prochot_cfg cfg = {
	.gpio_prochot_in = GPIO_CPU_PROCHOT,
	.callback = &callback,

};

ZTEST_USER(throttle_ap, test_interrupts)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_prochot_l), gpios));
	const gpio_port_pins_t pin =
		DT_GPIO_PIN(DT_ALIAS(gpio_prochot_l), gpios);

	/* Start the test with the interrupt deasserted. */
	zassert_ok(gpio_emul_input_set(dev, pin, !PROCHOT_ASSERTED));

	throttle_ap_config_prochot(&cfg);
	gpio_enable_interrupt(cfg.gpio_prochot_in);

	zassert_ok(gpio_emul_input_set(dev, pin, PROCHOT_ASSERTED));
	debounce_wait();
	zassert_true(was_asserted);

	zassert_ok(gpio_emul_input_set(dev, pin, !PROCHOT_ASSERTED));
	debounce_wait();
	zassert_false(was_asserted);
}
