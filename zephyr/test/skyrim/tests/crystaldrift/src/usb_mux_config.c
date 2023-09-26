/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "emul/retimer/emul_anx7483.h"
#include "usbc/usb_muxes.h"
#include "ztest/usb_mux_config.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define ANX7483_EMUL1 EMUL_DT_GET(DT_NODELABEL(anx7483_port1))

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

static bool alt_retimer;
static int cros_cbi_get_fw_config_mock(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	if (field_id != FW_IO_DB)
		return -EINVAL;

	*value = alt_retimer ? FW_IO_DB_PS8811_PS8818 : FW_IO_DB_NONE_ANX7483;
	return 0;
}

static void usb_mux_config_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(cros_cbi_get_fw_config);

	cros_cbi_get_fw_config_fake.custom_fake = cros_cbi_get_fw_config_mock;
}

ZTEST_SUITE(usb_mux_config, NULL, NULL, usb_mux_config_before, NULL, NULL);

ZTEST(usb_mux_config, test_board_anx7483_c1_mux_set)
{
	int rv;
	enum anx7483_eq_setting eq;
	enum anx7483_fg_setting fg;

	alt_retimer = false;
	setup_mux();

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
	zassert_equal(eq, ANX7483_EQ_SETTING_10_3DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_10_3DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_10_3DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_10_3DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_URX1, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_1_2DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_URX2, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_1_2DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_UTX1, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_1_2DB);

	rv = anx7483_emul_get_fg(ANX7483_EMUL1, ANX7483_PIN_UTX2, &fg);
	zassert_ok(rv);
	zassert_equal(fg, ANX7483_FG_SETTING_1_2DB);

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

ZTEST(usb_mux_config, test_setup_mux)
{
	alt_retimer = false;
	setup_mux();
	zassert_equal(usb_mux_enable_alternative_fake.call_count, 0);

	alt_retimer = true;
	setup_mux();
	zassert_equal(usb_mux_enable_alternative_fake.call_count, 1);
}
