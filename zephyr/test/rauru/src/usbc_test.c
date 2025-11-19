/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "battery.h"
#include "charge_manager.h"
#include "chipset.h"
#include "console.h"
#include "driver/ppc/syv682x.h"
#include "driver/ppc/syv682x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_syv682x.h"
#include "hooks.h"
#include "i2c/i2c.h"
#include "power.h"
#include "test_state.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/devicetree/io-channels.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, battery_wait_for_stable);
FAKE_VALUE_FUNC(int, adc_read_channel, enum adc_channel);
FAKE_VALUE_FUNC(int, ppc_is_sourcing_vbus, int);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
FAKE_VOID_FUNC(pd_send_host_event, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);

ZTEST(usbc_test, test_board_reset_pd_mcu)
{
	/*
	 * No assertion required as board_reset_pd_mcu is a no-op for now
	 * It’s supposed to reset the PD MCU, but this functionality
	 * is handled in chip code (it83xx/intc.c).
	 */
	board_reset_pd_mcu();
}

ZTEST(usbc_test, test_board_pd_vconn_ctrl)
{
	/*
	 * No assertion required as board_pd_vconn_ctrl is a no-op for now
	 * VCONN control is handled by the PPC driver via PD state machine.
	 */
	board_pd_vconn_ctrl(0, USBPD_CC_PIN_1, 1);
	board_pd_vconn_ctrl(1, USBPD_CC_PIN_2, 0);
}

ZTEST(usbc_test, test_pd_set_power_supply_ready_success)
{
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_source_enable_fake.return_val = EC_SUCCESS;

	zassert_ok(pd_set_power_supply_ready(0));

	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_val, 0);

	zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
	zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
	zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 0);

	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 1);

	k_msleep(1);
	zassert_equal(pd_send_host_event_fake.call_count, 1);
}

ZTEST(usbc_test, test_pd_set_power_supply_ready_sink_fail)
{
	ppc_vbus_sink_enable_fake.return_val = EC_ERROR_INVAL;

	int rv = pd_set_power_supply_ready(0);

	zassert_equal(rv, EC_ERROR_INVAL);

	/* Sink attempted */
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);

	/* Must NOT proceed further */
	zassert_equal(pd_set_vbus_discharge_fake.call_count, 0);
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 0);
}

ZTEST(usbc_test, test_pd_set_power_supply_ready_source_fail)
{
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_source_enable_fake.return_val = EC_ERROR_BUSY;

	int rv = pd_set_power_supply_ready(0);

	zassert_equal(rv, EC_ERROR_BUSY);

	/* Sink disable still happens */
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);

	/* Discharge happens before source enable */
	zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);

	/* Source enable attempted */
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
}

ZTEST(usbc_test, test_pd_power_supply_reset_integration)
{
	pd_set_power_supply_ready(0);
	k_msleep(1);

	pd_power_supply_reset(0);
	k_msleep(1);

	/* After reset, VBUS off */
	zassert_false(board_vbus_source_enabled(0));
}

ZTEST(usbc_test, test_pd_power_supply_reset_not_sourcing)
{
	ppc_is_sourcing_vbus_fake.return_val = 0;

	pd_power_supply_reset(0);

	zassert_equal(pd_set_vbus_discharge_fake.call_count, 0);
}

ZTEST(usbc_test, test_pd_check_vconn_swap)
{
	const int port = 0;
	/* suspend */
	power_set_state(POWER_S3);
	zassert_true(pd_check_vconn_swap(port));

	/* s0 */
	power_set_state(POWER_S0);
	zassert_true(pd_check_vconn_swap(port));

	/* softoff */
	power_set_state(POWER_S5);
	zassert_false(pd_check_vconn_swap(port));

	/* hardoff */
	power_set_state(POWER_G3);
	zassert_false(pd_check_vconn_swap(port));
}

ZTEST(usbc_test, test_set_active_charge_port_none)
{
	/* Don't return error even disable failed */
	ppc_vbus_sink_enable_fake.return_val = 1;
	zassert_equal(EC_SUCCESS,
		      board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(2, ppc_vbus_sink_enable_fake.call_count);
	/* C0 */
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg0_history[0]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[0]);
	/* C1 */
	zassert_equal(1, ppc_vbus_sink_enable_fake.arg0_history[1]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[1]);
}

ZTEST(usbc_test, test_set_active_charge_port_invalid_port)
{
	zassert_equal(board_set_active_charge_port(3), EC_ERROR_INVAL,
		      "port 3 doesn't exist, should return error");
}

ZTEST(usbc_test, test_set_active_charge_port)
{
	/* We can successfully start sinking on a port */
	zassert_ok(board_set_active_charge_port(0));

	/* Requested charging stop initially */
	/* Sinking on the other port was disabled */
	zassert_equal(1, ppc_vbus_sink_enable_fake.arg0_history[0]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[0]);
	/* Sinking was enabled on the new port */
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg0_history[1]);
	zassert_equal(1, ppc_vbus_sink_enable_fake.arg1_history[1]);
}

ZTEST(usbc_test, test_set_active_charge_port_enable_fail)
{
	ppc_vbus_sink_enable_fake.return_val = 1;
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);
}

ZTEST(usbc_test, test_vbus_adc_channel)
{
	zassert_equal(board_get_vbus_adc(0), ADC_VBUS_C0);
	zassert_equal(board_get_vbus_adc(1), ADC_VBUS_C1);
	zassert_equal(board_get_vbus_adc(99), ADC_VBUS_C0);
}

static void set_vbus_adc(int voltage)
{
	/* Set input voltage */
	adc_read_channel_fake.return_val = voltage;
}

ZTEST(usbc_test, test_pd_check_vbus_level)
{
	/* SAFE0V true */
	set_vbus_adc(PD_V_SAFE0V_MAX - 10);
	zassert_true(pd_check_vbus_level(0, VBUS_SAFE0V));

	/* SAFE0V false */
	set_vbus_adc(PD_V_SAFE0V_MAX + 100);
	zassert_false(pd_check_vbus_level(0, VBUS_SAFE0V));

	/* PRESENT true */
	set_vbus_adc(PD_V_SAFE5V_MIN + 200);
	zassert_true(pd_check_vbus_level(0, VBUS_PRESENT));

	/* PRESENT false */
	set_vbus_adc(PD_V_SAFE5V_MIN - 200);
	zassert_false(pd_check_vbus_level(0, VBUS_PRESENT));

	/* REMOVED true */
	set_vbus_adc(PD_V_SINK_DISCONNECT_MAX - 50);
	zassert_true(pd_check_vbus_level(0, VBUS_REMOVED));

	/* REMOVED false */
	set_vbus_adc(PD_V_SINK_DISCONNECT_MAX + 200);
	zassert_false(pd_check_vbus_level(0, VBUS_REMOVED));

	/* Unknown level */
	zassert_false(pd_check_vbus_level(0, 99));
}

ZTEST(usbc_test, test_pd_snk_is_vbus_provided)
{
	set_vbus_adc(PD_V_SAFE5V_MIN + 100);
	zassert_true(pd_snk_is_vbus_provided(0));

	set_vbus_adc(PD_V_SAFE5V_MIN - 500);
	zassert_false(pd_snk_is_vbus_provided(0));
}

static void before(void *data)
{
	RESET_FAKE(adc_read_channel);
	RESET_FAKE(ppc_is_sourcing_vbus);
	RESET_FAKE(pd_set_vbus_discharge);
	RESET_FAKE(pd_send_host_event);
	RESET_FAKE(ppc_vbus_sink_enable);
	RESET_FAKE(ppc_vbus_source_enable);
}

ZTEST_SUITE(usbc_test, rauru_predicate_post_main, NULL, before, NULL, NULL);
