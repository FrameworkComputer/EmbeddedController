/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "battery.h"
#include "charge_manager.h"
#include "driver/ppc/syv682x.h"
#include "driver/ppc/syv682x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_syv682x.h"
#include "fakes.h"
#include "i2c/i2c.h"
#include "test_state.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

static bool ppc_sink_enabled(int port)
{
	const struct emul *emul = (port == 0) ?
					  EMUL_DT_GET(DT_NODELABEL(ppc_port0)) :
					  EMUL_DT_GET(DT_NODELABEL(ppc_port1));
	uint8_t val = 0;

	syv682x_emul_get_reg(emul, SYV682X_CONTROL_1_REG, &val);

	return !(val & (SYV682X_CONTROL_1_PWR_ENB | SYV682X_CONTROL_1_HV_DR));
}

ZTEST(usbc_config, test_set_active_charge_port)
{
	/* reset ppc state */
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);

	/* sourcing port 0, expect port 0 not sinkable */
	zassert_ok(pd_set_power_supply_ready(0));
	zassert_not_equal(board_set_active_charge_port(0), 0, NULL);
	zassert_true(board_vbus_source_enabled(0), NULL);
	zassert_false(board_vbus_source_enabled(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);

	/* sinking port 1 */
	zassert_ok(board_set_active_charge_port(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);

	/*
	 * sinking an invalid port should return error and doesn't change
	 * any state
	 */
	zassert_not_equal(board_set_active_charge_port(2), 0, NULL);
	zassert_true(board_vbus_source_enabled(0), NULL);
	zassert_false(board_vbus_source_enabled(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);

	/* turn off sourcing, sinking port 0 */
	pd_power_supply_reset(0);
	zassert_ok(board_set_active_charge_port(0), NULL);
	zassert_true(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);

	/* sinking port 1 */
	zassert_ok(board_set_active_charge_port(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);

	/* back to port 0 */
	zassert_ok(board_set_active_charge_port(0), NULL);
	zassert_true(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);

	/* reset */
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE), NULL);
	zassert_false(board_vbus_source_enabled(0), NULL);
	zassert_false(board_vbus_source_enabled(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);
}

ZTEST(usbc_config, test_set_active_charge_port_fail)
{
	const struct emul *ppc0 = EMUL_DT_GET(DT_NODELABEL(ppc_port0));

	/* Verify that failure on ppc0 doesn't affect ppc1 */
	i2c_common_emul_set_write_fail_reg(
		emul_syv682x_get_i2c_common_data(ppc0),
		I2C_COMMON_EMUL_FAIL_ALL_REG);

	zassert_ok(board_set_active_charge_port(1), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);
	zassert_ok(board_set_active_charge_port(1), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);

	/* trying to enable ppc0 results in error */
	zassert_not_equal(board_set_active_charge_port(0), 0, NULL);
	zassert_false(ppc_sink_enabled(1), NULL);
}

ZTEST(usbc_config, test_adc_channel)
{
	zassert_equal(board_get_vbus_adc(0), ADC_VBUS_C0, NULL);
	zassert_equal(board_get_vbus_adc(1), ADC_VBUS_C1, NULL);
	zassert_equal(board_get_vbus_adc(99), ADC_VBUS_C0, NULL);
}

static void geralt_usbc_config_before(void *fixture)
{
	const struct emul *ppc0 = EMUL_DT_GET(DT_NODELABEL(ppc_port0));
	const struct emul *ppc1 = EMUL_DT_GET(DT_NODELABEL(ppc_port1));

	i2c_common_emul_set_write_fail_reg(
		emul_syv682x_get_i2c_common_data(ppc0),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_syv682x_get_i2c_common_data(ppc1),
		I2C_COMMON_EMUL_NO_FAIL_REG);

	board_set_active_charge_port(CHARGE_PORT_NONE);
}

ZTEST_SUITE(usbc_config, geralt_predicate_post_main, NULL,
	    geralt_usbc_config_before, NULL, NULL);
