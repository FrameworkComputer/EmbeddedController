/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "fpsensor_detect.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static void *transport_setup(void)
{
	const struct device *transport_sel_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(transport_sel), gpios));
	const gpio_port_pins_t transport_sel_pin =
		DT_GPIO_PIN(DT_NODELABEL(transport_sel), gpios);

	/* Set the transport sel pin */
	gpio_emul_input_set(transport_sel_gpio, transport_sel_pin, 0);

	return NULL;
}

ZTEST_SUITE(transport, NULL, transport_setup, NULL, NULL, NULL);

ZTEST(transport, test_transport_type)
{
	zassert_equal(get_fp_transport_type(), FP_TRANSPORT_TYPE_UART,
		      "Incorrect transport type");
}
