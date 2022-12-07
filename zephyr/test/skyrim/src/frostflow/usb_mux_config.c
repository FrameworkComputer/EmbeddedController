/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <gpio.h>
#include <usbc/usb_muxes.h>

int board_c0_amd_fp6_mux_set(const struct usb_mux *me, mux_state_t mux_state);
int board_c1_ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state);

ZTEST_SUITE(usb_mux_config, NULL, NULL, NULL, NULL, NULL);

ZTEST(usb_mux_config, board_c0_amd_fp6_mux_set)
{
	const struct gpio_dt_spec *c0 =
		GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip);
	const struct gpio_dt_spec *c1 =
		GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip);
	struct usb_mux mux;
	int rv;

	/* Output for each port should match inverted status. */
	mux.usb_port = 0;
	rv = board_c0_amd_fp6_mux_set(&mux, 0);
	zassert_equal(rv, EC_SUCCESS);
	zassert_equal(gpio_emul_output_get(c0->port, c0->pin), 0);

	rv = board_c0_amd_fp6_mux_set(&mux, USB_PD_MUX_POLARITY_INVERTED);
	zassert_equal(rv, EC_SUCCESS);
	zassert_equal(gpio_emul_output_get(c0->port, c0->pin), 1);

	mux.usb_port = 1;
	rv = board_c0_amd_fp6_mux_set(&mux, 0);
	zassert_equal(rv, EC_SUCCESS);
	zassert_equal(gpio_emul_output_get(c1->port, c1->pin), 0);

	rv = board_c0_amd_fp6_mux_set(&mux, USB_PD_MUX_POLARITY_INVERTED);
	zassert_equal(rv, EC_SUCCESS);
	zassert_equal(gpio_emul_output_get(c1->port, c1->pin), 1);
}

ZTEST(usb_mux_config, board_c1_ps8818_mux_set)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_in_hpd);
	struct usb_mux mux;

	/* gpio_usb_c1_in_hpd should match if DP is enabled. */
	mux.usb_port = 0;
	zassert_ok(board_c1_ps8818_mux_set(&mux, 0));
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);

	mux.usb_port = 1;
	zassert_ok(board_c1_ps8818_mux_set(&mux, USB_PD_MUX_DP_ENABLED));
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}
