/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/usb_muxes.h"
#include "ztest/usb_mux_config.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

int ioex_set_flip(int port, mux_state_t mux_state);
int board_c1_ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state);

DEFINE_FAKE_VOID_FUNC(usb_mux_enable_alternative);

ZTEST_SUITE(usb_mux_config_common, NULL, NULL, NULL, NULL, NULL);

ZTEST(usb_mux_config_common, test_ioex_set_flip)
{
	const struct gpio_dt_spec *c0 =
		GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip);
	const struct gpio_dt_spec *c1 =
		GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip);
	int rv;

	/* Value of the corresponding pin should match flipped status. */
	rv = ioex_set_flip(0, 0);
	zassert_ok(rv);
	zassert_equal(gpio_emul_output_get(c0->port, c0->pin), 0);

	rv = ioex_set_flip(0, USB_PD_MUX_POLARITY_INVERTED);
	zassert_ok(rv);
	zassert_equal(gpio_emul_output_get(c0->port, c0->pin), 1);

	rv = ioex_set_flip(1, 0);
	zassert_ok(rv);
	zassert_equal(gpio_emul_output_get(c1->port, c1->pin), 0);

	rv = ioex_set_flip(1, USB_PD_MUX_POLARITY_INVERTED);
	zassert_ok(rv);
	zassert_equal(gpio_emul_output_get(c1->port, c1->pin), 1);
}
