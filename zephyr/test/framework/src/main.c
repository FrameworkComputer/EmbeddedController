/* Copyright 2026 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include "gpio.h"
#include "gpio/gpio_int.h"
#include "uefi_app_mode.h"

/* Counter to track power button interrupt invocations */
static int power_button_interrupt_count;

/* Handler for the power button interrupt - required by device tree */
void board_power_button_interrupt(enum gpio_signal signal)
{
	power_button_interrupt_count++;
}

/* Helper to toggle GPIO and trigger edge interrupt */
static void toggle_power_button(const struct gpio_dt_spec *btn)
{
	int current = gpio_pin_get_dt(btn);

	gpio_emul_input_set(btn->port, btn->pin, !current);
}

static void uefi_app_mode_before(void *fixture)
{
	ARG_UNUSED(fixture);
	power_button_interrupt_count = 0;
}

ZTEST_SUITE(uefi_app_mode, NULL, NULL, uefi_app_mode_before, NULL, NULL);

ZTEST(uefi_app_mode, test_enable_disables_power_button_interrupt)
{
	const struct gpio_dt_spec *btn = GPIO_DT_FROM_NODELABEL(gpio_on_off_btn_l);

	/* First enable the interrupt so we have a known state */
	uefi_app_mode_setting(0);
	power_button_interrupt_count = 0;

	/* Verify interrupt fires when enabled */
	toggle_power_button(btn);
	zassert_equal(power_button_interrupt_count, 1,
		      "Interrupt should fire when UEFI app mode is disabled");

	/* Now enable UEFI app mode - should disable power button interrupt */
	uefi_app_mode_setting(1);

	/* Toggle GPIO - interrupt should NOT fire */
	toggle_power_button(btn);
	zassert_equal(power_button_interrupt_count, 1,
		      "Interrupt should not fire when UEFI app mode is enabled");
}

ZTEST(uefi_app_mode, test_disable_enables_power_button_interrupt)
{
	const struct gpio_dt_spec *btn = GPIO_DT_FROM_NODELABEL(gpio_on_off_btn_l);

	/* First disable the interrupt */
	uefi_app_mode_setting(1);
	power_button_interrupt_count = 0;

	/* Verify interrupt does not fire when disabled */
	toggle_power_button(btn);
	zassert_equal(power_button_interrupt_count, 0,
		      "Interrupt should not fire when UEFI app mode is enabled");

	/* Now disable UEFI app mode - should re-enable power button interrupt */
	uefi_app_mode_setting(0);

	/* Toggle GPIO - interrupt should fire */
	toggle_power_button(btn);
	zassert_equal(power_button_interrupt_count, 1,
		      "Interrupt should fire when UEFI app mode is disabled");
}

ZTEST(uefi_app_mode, test_button_status_returns_gpio_state)
{
	const struct gpio_dt_spec *btn = GPIO_DT_FROM_NODELABEL(gpio_on_off_btn_l);
	uint8_t status;

	/* Set button to not pressed (high = 1) */
	gpio_emul_input_set(btn->port, btn->pin, 1);
	status = uefi_app_btn_status();
	zassert_equal(status, 1, "Button status should be 1 when GPIO is high");

	/* Set button to pressed (low = 0) */
	gpio_emul_input_set(btn->port, btn->pin, 0);
	status = uefi_app_btn_status();
	zassert_equal(status, 0, "Button status should be 0 when GPIO is low");
}
