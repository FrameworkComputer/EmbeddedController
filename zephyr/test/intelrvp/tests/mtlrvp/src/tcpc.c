/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/retimer/bb_retimer_public.h"
#include "ec_commands.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"

#include <zephyr/devicetree/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <usbc/tcpc_generic_emul.h>

FAKE_VOID_FUNC(nct38xx_reset_notify, int);
FAKE_VALUE_FUNC(int, ccgxxf_reset, int);
FAKE_VALUE_FUNC(int, ioex_init, int);
FAKE_VALUE_FUNC(int, board_get_version);
FAKE_VALUE_FUNC(int, pd_snk_is_vbus_provided, int);
FAKE_VOID_FUNC(lid_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(io_expander_it8801_interrupt, enum gpio_signal);

struct gpio_int_config {
	void (*handler)(enum gpio_signal); /* Handler to call */
	gpio_flags_t flags; /* Flags */
	const struct device *port; /* GPIO device */
	gpio_pin_t pin; /* GPIO pin */
	enum gpio_signal signal; /* Signal associated with interrupt */
};

#define MTLP_LP5_RVP_SKU_BOARD_ID 0x02

static const struct gpio_dt_spec usb_c0_c1_rst = {
	.port = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(usb_c0_c1_tcpc_rst_odl), gpios)),
	.pin = DT_GPIO_PIN(DT_NODELABEL(usb_c0_c1_tcpc_rst_odl), gpios),
	.dt_flags = 0xFF &
		    DT_GPIO_FLAGS(DT_NODELABEL(usb_c0_c1_tcpc_rst_odl), gpios)
};

/* USB-C ports */
enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
#if defined(HAS_TASK_PD_C2)
	USBC_PORT_C2,
	USBC_PORT_C3,
#endif
	USBC_PORT_COUNT
};

/* Helper function to reset all fakes */
static void reset_test_fakes(void)
{
	RESET_FAKE(nct38xx_reset_notify);
	RESET_FAKE(ccgxxf_reset);
	RESET_FAKE(ioex_init);
	RESET_FAKE(board_get_version);
	RESET_FAKE(pd_snk_is_vbus_provided);
}

ZTEST_USER(mtlrvp_tcpc, test_board_reset_pd_mcu)
{
	gpio_pin_configure(usb_c0_c1_rst.port, usb_c0_c1_rst.pin,
			   (GPIO_OUTPUT | GPIO_ACTIVE_HIGH));
	/* Execute the function to test */
	board_reset_pd_mcu();

	int gpio_val =
		gpio_emul_output_get(usb_c0_c1_rst.port, usb_c0_c1_rst.pin);
	zassert_equal(gpio_val, 1, "gpio usb_c0_c1_tcpc_rst_odl not set");
	zassert_equal(2, nct38xx_reset_notify_fake.call_count,
		      "nct38xx_reset_notify call count mismatch");

#if defined(HAS_TASK_PD_C2)
	zassert_equal(1, ccgxxf_reset_fake.call_count,
		      "ccgxxf_reset call count mismatch");
	zassert_equal(1, ioex_init_fake.call_count,
		      "ioex_init call count mismatch");
#endif
}

ZTEST_USER(mtlrvp_tcpc, test_board_is_tbt_usb4_port0)
{
	board_get_version_fake.return_val = MTLP_LP5_RVP_SKU_BOARD_ID;
	zassert_false(board_is_tbt_usb4_port(0));
}

ZTEST_USER(mtlrvp_tcpc, test_board_is_tbt_usb4_port1)
{
	board_get_version_fake.return_val = MTLP_LP5_RVP_SKU_BOARD_ID;
	zassert_false(board_is_tbt_usb4_port(1));
}

ZTEST_USER(mtlrvp_tcpc, test_board_is_tbt_usb4_port_not_LP5_RVP)
{
	board_get_version_fake.return_val = 0;
	zassert_true(board_is_tbt_usb4_port(0));
}

ZTEST_USER(mtlrvp_tcpc, test_board_get_max_tbt_speed_port2)
{
	board_get_version_fake.return_val = MTLP_LP5_RVP_SKU_BOARD_ID;
	zassert_equal(TBT_SS_U32_GEN1_GEN2, board_get_max_tbt_speed(2));
}

ZTEST_USER(mtlrvp_tcpc, test_board_get_max_tbt_speed_port0)
{
	board_get_version_fake.return_val = MTLP_LP5_RVP_SKU_BOARD_ID;
	zassert_equal(TBT_SS_TBT_GEN3, board_get_max_tbt_speed(0));
}

ZTEST_USER(mtlrvp_tcpc, test_board_get_max_tbt_speed_port_not_LP5_RVP)
{
	board_get_version_fake.return_val = 0;
	zassert_equal(TBT_SS_TBT_GEN3, board_get_max_tbt_speed(0));
}

ZTEST_USER(mtlrvp_tcpc, test_pd_check_vbus_level_vbus_not_provided)
{
	pd_snk_is_vbus_provided_fake.return_val = 0;
	zassert_true(pd_check_vbus_level(USBC_PORT_C0, VBUS_REMOVED));
}

ZTEST_USER(mtlrvp_tcpc, test_pd_check_vbus_level_vbus_provided)
{
	pd_snk_is_vbus_provided_fake.return_val = 1;
	zassert_true(pd_check_vbus_level(USBC_PORT_C1, VBUS_PRESENT));
}

ZTEST_SUITE(mtlrvp_tcpc, NULL, NULL, reset_test_fakes, NULL, NULL);
