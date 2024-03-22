/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "usbc/usb_muxes.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(skyrim, CONFIG_SKYRIM_LOG_LEVEL);

/*
 * Provide weak definitons for interrupt handlers. This minimizes the number
 * of test-specific device tree overrides needed.
 */

test_mockable void baseboard_soc_pcore_ocp(enum gpio_signal signal)
{
}

test_mockable void baseboard_soc_thermtrip(enum gpio_signal signal)
{
}

test_mockable int bmi3xx_interrupt(enum gpio_signal signal)
{
	return -EINVAL;
}

test_mockable int bma4xx_interrupt(enum gpio_signal signal)
{
	return -EINVAL;
}

test_mockable int board_anx7483_c0_mux_set(const struct usb_mux *me,
					   mux_state_t mux_state)
{
	return -EINVAL;
}

test_mockable int board_anx7483_c1_mux_set(const struct usb_mux *me,
					   mux_state_t mux_state)
{
	return -EINVAL;
}

test_mockable int power_interrupt_handler(const struct gpio_dt_spec *dt)
{
	return -EINVAL;
}

test_mockable void throttle_ap_prochot_input_interrupt(enum gpio_signal signal)
{
}

test_mockable void ppc_interrupt(enum gpio_signal signal)
{
}

test_mockable void bc12_interrupt(enum gpio_signal signal)
{
}

test_mockable void sbu_fault_interrupt(enum gpio_signal signal)
{
}

test_mockable void usb_fault_interrupt(enum gpio_signal signal)
{
}

test_mockable void test_nct38xx0_interrupt(enum gpio_signal signal)
{
}

test_mockable void test_nct38xx1_interrupt(enum gpio_signal signal)
{
}
