/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/tcpm.h"
#include "ec_app_main.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "variant_db_detection.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define LOG_LEVEL 0
LOG_MODULE_REGISTER(npcx_usbc);

FAKE_VALUE_FUNC(enum corsola_db_type, corsola_get_db_type);
FAKE_VALUE_FUNC(bool, in_interrupt_context);
FAKE_VOID_FUNC(bmi3xx_interrupt);
FAKE_VOID_FUNC(hdmi_hpd_interrupt);
FAKE_VOID_FUNC(ps185_hdmi_hpd_mux_set);
FAKE_VALUE_FUNC(bool, ps8743_field_update, const struct usb_mux *, uint8_t,
		uint8_t, uint8_t);

#define FFF_FAKES_LIST(FAKE)         \
	FAKE(corsola_get_db_type)    \
	FAKE(in_interrupt_context)   \
	FAKE(bmi3xx_interrupt)       \
	FAKE(hdmi_hpd_interrupt)     \
	FAKE(ps185_hdmi_hpd_mux_set) \
	FAKE(ps8743_field_update)

struct kingler_usbc_fixture {
	int place_holder;
};

static void *kingler_usbc_setup(void)
{
	static struct kingler_usbc_fixture f;

	return &f;
}

static void kingler_usbc_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
}

ZTEST_RULE(kingler_usbc_reset_rule, kingler_usbc_reset_rule_before, NULL);
ZTEST_SUITE(kingler_usbc, NULL, kingler_usbc_setup, NULL, NULL, NULL);

ZTEST_F(kingler_usbc, test_power_supply)
{
	pd_power_supply_reset(0);
	zassert_equal(0, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	zassert_equal(EC_SUCCESS, pd_set_power_supply_ready(0));
	zassert_equal(1, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	pd_power_supply_reset(0);
	zassert_equal(0, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	/* TODO: test C1 port after resolve the PPC emulator always accessing
	 * the same one with different index.
	 */
}
