/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charger_profile_override.h"
#include "emul/retimer/emul_anx7483.h"
#include "usbc/usb_muxes.h"
#include "ztest/usb_mux_config.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define ANX7483_EMUL1 EMUL_DT_GET(DT_NODELABEL(anx7483_port1))

static int chipset_state;
int chipset_in_state(int mask)
{
	return mask & chipset_state;
}

ZTEST_SUITE(usb_mux_config, NULL, NULL, NULL, NULL, NULL);

ZTEST(usb_mux_config, test_board_anx7483_c1_mux_set)
{
	int rv;
	enum anx7483_eq_setting eq;
	enum anx7483_fg_setting fg;

	usb_mux_init(1);

	/* Test USB mux state. */
	usb_mux_set(1, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	/* Test DP mux state. */
	usb_mux_set(1, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_URX1, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_0_5DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_URX2, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_0_5DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_UTX1, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_0_5DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_UTX2, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_0_5DB);

	/* Test dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_URX2, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_0_5DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_UTX2, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_0_5DB);

	/* Test flipped dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED,
		    USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_8_4DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_URX1, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_0_5DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_UTX1, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_0_5DB);
}

ZTEST(usb_mux_config, test_charger_profile_override)
{
	int rv;
	const int requested_current_high = WINTERHOLD_CHARGE_CURRENT_MAX + 1;
	const int requested_current_low = WINTERHOLD_CHARGE_CURRENT_MAX - 1;
	struct charge_state_data data;

	/* Should do nothing when chipset is off. */
	data.requested_current = requested_current_high;
	chipset_state = CHIPSET_STATE_HARD_OFF;
	rv = charger_profile_override(&data);
	zassert_ok(rv);
	zassert_equal(data.requested_current, requested_current_high);

	data.requested_current = requested_current_high;
	chipset_state = CHIPSET_STATE_SOFT_OFF;
	rv = charger_profile_override(&data);
	zassert_ok(rv);
	zassert_equal(data.requested_current, requested_current_high);

	/* Should clamp to WINTERHOLD_CHARGE_CURRENT_MAX when on. */
	data.requested_current = requested_current_high;
	chipset_state = CHIPSET_STATE_ON;
	rv = charger_profile_override(&data);
	zassert_ok(rv);
	zassert_equal(data.requested_current, WINTERHOLD_CHARGE_CURRENT_MAX);

	data.requested_current = requested_current_low;
	chipset_state = CHIPSET_STATE_ON;
	rv = charger_profile_override(&data);
	zassert_ok(rv);
	zassert_equal(data.requested_current, requested_current_low);
}

ZTEST(usb_mux_config, test_charger_profile_override_get_param)
{
	zassert_equal(charger_profile_override_get_param(0, NULL),
		      EC_RES_INVALID_PARAM);
}

ZTEST(usb_mux_config, test_charger_profile_override_set_param)
{
	zassert_equal(charger_profile_override_get_param(0, 0),
		      EC_RES_INVALID_PARAM);
}
