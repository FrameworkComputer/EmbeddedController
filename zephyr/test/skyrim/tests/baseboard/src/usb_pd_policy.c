/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <gpio.h>
#include <ioexpander.h>
#include <usb_pd.h>

FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
FAKE_VOID_FUNC(pd_send_host_event, int);
FAKE_VALUE_FUNC(bool, tcpm_get_src_ctrl, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);

int board_is_sourcing_vbus(int port);

ZTEST(usb_pd_policy, test_pd_check_vconn_swap)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5);

	/*
	 * The value of pd_check_vconn_swap should follow gpio_pg_pwr_s5
	 * for all ports.
	 */
	zassert_ok(gpio_emul_input_set(gpio->port, gpio->pin, true));
	zassert_true(pd_check_vconn_swap(0));
	zassert_true(pd_check_vconn_swap(1));

	zassert_ok(gpio_emul_input_set(gpio->port, gpio->pin, false));
	zassert_false(pd_check_vconn_swap(0));
	zassert_false(pd_check_vconn_swap(1));
}

ZTEST(usb_pd_policy, test_pd_power_supply_reset_c0_success)
{
	ppc_vbus_source_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;

	pd_power_supply_reset(0);

	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	/* enable */
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 0);

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE)) {
		zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
		/* port */
		zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
		/* enable */
		zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 1);
	}

	zassert_equal(pd_send_host_event_fake.call_count, 1);
}

ZTEST(usb_pd_policy, test_pd_power_supply_reset_c1_success)
{
	ppc_vbus_source_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;

	pd_power_supply_reset(1);

	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 1);
	/* enable */
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 0);

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE)) {
		zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
		/* port */
		zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 1);
		/* enable */
		zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 1);
	}

	zassert_equal(pd_send_host_event_fake.call_count, 1);
}

ZTEST(usb_pd_policy, test_pd_set_power_supply_ready_c0_success)
{
	ppc_vbus_source_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;

	zassert_equal(pd_set_power_supply_ready(0), EC_SUCCESS);

	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 0);
	/* enable */
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_val, 0);

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE)) {
		zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
		/* port */
		zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
		/* enable */
		zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 0);
	}

	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	/* enable */
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 1);

	zassert_equal(pd_send_host_event_fake.call_count, 1);
}

ZTEST(usb_pd_policy, test_pd_set_power_supply_ready_c1_success)
{
	ppc_vbus_source_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;

	zassert_equal(pd_set_power_supply_ready(1), EC_SUCCESS);

	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 1);
	/* enable */
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_val, 0);

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE)) {
		zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
		/* port */
		zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 1);
		/* enable */
		zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 0);
	}

	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 1);
	/* enable */
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 1);

	zassert_equal(pd_send_host_event_fake.call_count, 1);
}

ZTEST(usb_pd_policy, test_pd_set_power_supply_ready_c0_failure)
{
	/* Test with ppc_vbus_sink_enable_fake failing. */
	ppc_vbus_sink_enable_fake.return_val = EC_ERROR_INVAL;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 0);
	/* enable */
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_val, 0);

	/* Test with ppc_vbus_source_enable failing. */
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_source_enable_fake.return_val = EC_ERROR_INVAL;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	/* enable */
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 1);

	zassert_equal(pd_send_host_event_fake.call_count, 0);
}

ZTEST(usb_pd_policy, test_pd_set_power_supply_ready_c1_failure)
{
	/* Test with ppc_vbus_sink_enable_fake failing. */
	ppc_vbus_sink_enable_fake.return_val = EC_ERROR_INVAL;
	zassert_not_equal(pd_set_power_supply_ready(1), EC_SUCCESS);
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 1);
	/* enable */
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_val, 0);

	/* Test with ppc_vbus_source_enable failing. */
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_source_enable_fake.return_val = EC_ERROR_INVAL;
	zassert_not_equal(pd_set_power_supply_ready(1), EC_SUCCESS);
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	/* port */
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 1);
	/* enable */
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 1);

	zassert_equal(pd_send_host_event_fake.call_count, 0);
}

ZTEST(usb_pd_policy, test_board_pd_set_frs_enable)
{
	const struct gpio_dt_spec *c0 =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_fastsw_ctl_en);
	const struct gpio_dt_spec *c1 =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_tcpc_fastsw_ctl_en);

	/* Enables to each port should just change the corresponding GPIO. */
	zassert_equal(board_pd_set_frs_enable(0, true), EC_SUCCESS);
	zassert_equal(gpio_emul_output_get(c0->port, c0->pin), 1);

	zassert_equal(board_pd_set_frs_enable(0, false), EC_SUCCESS);
	zassert_equal(gpio_emul_output_get(c0->port, c0->pin), 0);

	zassert_equal(board_pd_set_frs_enable(1, true), EC_SUCCESS);
	zassert_equal(gpio_emul_output_get(c1->port, c1->pin), 1);

	zassert_equal(board_pd_set_frs_enable(1, false), EC_SUCCESS);
	zassert_equal(gpio_emul_output_get(c1->port, c1->pin), 0);
}

ZTEST(usb_pd_policy, test_board_is_sourcing_vbus_c0_true)
{
	tcpm_get_src_ctrl_fake.return_val = true;
	zassert_true(board_is_sourcing_vbus(0));
	zassert_equal(tcpm_get_src_ctrl_fake.call_count, 1);
	/* port */
	zassert_equal(tcpm_get_src_ctrl_fake.arg0_val, 0);
}

ZTEST(usb_pd_policy, test_board_is_sourcing_vbus_c0_false)
{
	tcpm_get_src_ctrl_fake.return_val = false;
	zassert_false(board_is_sourcing_vbus(0));
	zassert_equal(tcpm_get_src_ctrl_fake.call_count, 1);
	/* port */
	zassert_equal(tcpm_get_src_ctrl_fake.arg0_val, 0);
}

ZTEST(usb_pd_policy, test_board_is_sourcing_vbus_c1_true)
{
	tcpm_get_src_ctrl_fake.return_val = true;
	zassert_true(board_is_sourcing_vbus(1));
	zassert_equal(tcpm_get_src_ctrl_fake.call_count, 1);
	/* port */
	zassert_equal(tcpm_get_src_ctrl_fake.arg0_val, 1);
}

ZTEST(usb_pd_policy, test_board_is_sourcing_vbus_c1_false)
{
	tcpm_get_src_ctrl_fake.return_val = false;
	zassert_false(board_is_sourcing_vbus(1));
	zassert_equal(tcpm_get_src_ctrl_fake.call_count, 1);
	/* port */
	zassert_equal(tcpm_get_src_ctrl_fake.arg0_val, 1);
}

static void usb_pd_policy_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(ppc_vbus_source_enable);
	RESET_FAKE(pd_set_vbus_discharge);
	RESET_FAKE(pd_send_host_event);
	RESET_FAKE(tcpm_get_src_ctrl);
	RESET_FAKE(ppc_vbus_sink_enable);
}

ZTEST_SUITE(usb_pd_policy, NULL, NULL, usb_pd_policy_before, NULL, NULL);
