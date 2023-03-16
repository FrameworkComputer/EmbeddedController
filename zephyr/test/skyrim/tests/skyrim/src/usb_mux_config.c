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

#define ANX7483_EMUL0 EMUL_DT_GET(DT_NODELABEL(anx7483_port0))
#define ANX7483_EMUL1 EMUL_DT_GET(DT_NODELABEL(anx7483_port1))

int board_c1_ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state);
void setup_mux(void);

extern const struct anx7483_tuning_set anx7483_usb_enabled[];
extern const struct anx7483_tuning_set anx7483_dp_enabled[];
extern const struct anx7483_tuning_set anx7483_dock_noflip[];
extern const struct anx7483_tuning_set anx7483_dock_flip[];

extern const size_t anx7483_usb_enabled_count;
extern const size_t anx7483_dp_enabled_count;
extern const size_t anx7483_dock_noflip_count;
extern const size_t anx7483_dock_flip_count;

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

ZTEST(usb_mux_config, test_board_anx7483_c0_mux_set)
{
	int rv;

	usb_mux_init(0);

	usb_mux_set(0, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 0);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL0, anx7483_usb_enabled,
					  anx7483_usb_enabled_count);
	zexpect_ok(rv);

	usb_mux_set(0, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT, 0);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL0, anx7483_dp_enabled,
					  anx7483_dp_enabled_count);
	zexpect_ok(rv);

	usb_mux_set(0, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT, 0);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL0, anx7483_dock_noflip,
					  anx7483_dock_noflip_count);
	zexpect_ok(rv);

	usb_mux_set(0, USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED,
		    USB_SWITCH_CONNECT, 0);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL0, anx7483_dock_flip,
					  anx7483_dock_flip_count);
	zexpect_ok(rv);
}

ZTEST(usb_mux_config, test_board_anx7483_c1_mux_set)
{
	int rv;
	enum anx7483_eq_setting eq;

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
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	/* Test dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	/* Test flipped dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED,
		    USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_UTX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);
}

ZTEST(usb_mux_config, test_board_c1_ps8818_mux_set)
{
	const struct gpio_dt_spec *c1 =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_in_hpd);
	const struct usb_mux mux = {
		.usb_port = 1,
	};

	board_c1_ps8818_mux_set(&mux, USB_PD_MUX_USB_ENABLED);
	zassert_false(gpio_emul_output_get(c1->port, c1->pin));

	board_c1_ps8818_mux_set(&mux, USB_PD_MUX_DP_ENABLED);
	zassert_true(gpio_emul_output_get(c1->port, c1->pin));
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
