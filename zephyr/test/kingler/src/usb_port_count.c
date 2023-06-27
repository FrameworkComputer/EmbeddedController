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
LOG_MODULE_REGISTER(usb_port_count);

FAKE_VALUE_FUNC(enum corsola_db_type, corsola_get_db_type);
FAKE_VALUE_FUNC(bool, in_interrupt_context);
FAKE_VOID_FUNC(bmi3xx_interrupt);
FAKE_VOID_FUNC(hdmi_hpd_interrupt);
FAKE_VOID_FUNC(ps185_hdmi_hpd_mux_set);
FAKE_VALUE_FUNC(bool, ps8743_field_update, const struct usb_mux *, uint8_t,
		uint8_t, uint8_t);
FAKE_VOID_FUNC(pd_set_dual_role, int, enum pd_dual_role_states);
FAKE_VALUE_FUNC(int, tc_is_attached_src, int);

#define FFF_FAKES_LIST(FAKE)         \
	FAKE(corsola_get_db_type)    \
	FAKE(in_interrupt_context)   \
	FAKE(bmi3xx_interrupt)       \
	FAKE(hdmi_hpd_interrupt)     \
	FAKE(ps185_hdmi_hpd_mux_set) \
	FAKE(ps8743_field_update)    \
	FAKE(pd_set_dual_role)       \
	FAKE(tc_is_attached_src)

struct usb_port_count_fixture {
	int place_holder;
};

static void *usb_port_count_setup(void)
{
	static struct usb_port_count_fixture f;

	return &f;
}

static void usb_port_count_reset_rule_before(const struct ztest_unit_test *test,
					     void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
}

ZTEST_RULE(usb_port_count_reset_rule, usb_port_count_reset_rule_before, NULL);
ZTEST_SUITE(usb_port_count, NULL, usb_port_count_setup, NULL, NULL, NULL);

ZTEST_F(usb_port_count, test_detect_db)
{
	struct {
		enum corsola_db_type test_type;
		int expected_board_get_port_count;
		int expected_board_get_adjusted_port_count;
	} testdata[] = { { CORSOLA_DB_UNINIT, CONFIG_USB_PD_PORT_MAX_COUNT,
			   CONFIG_USB_PD_PORT_MAX_COUNT - 1 },
			 { CORSOLA_DB_NO_DETECTION,
			   CONFIG_USB_PD_PORT_MAX_COUNT,
			   CONFIG_USB_PD_PORT_MAX_COUNT },
			 { CORSOLA_DB_NONE, CONFIG_USB_PD_PORT_MAX_COUNT - 1,
			   CONFIG_USB_PD_PORT_MAX_COUNT - 1 },
			 { CORSOLA_DB_TYPEC, CONFIG_USB_PD_PORT_MAX_COUNT,
			   CONFIG_USB_PD_PORT_MAX_COUNT },
			 { CORSOLA_DB_HDMI, CONFIG_USB_PD_PORT_MAX_COUNT - 1,
			   CONFIG_USB_PD_PORT_MAX_COUNT - 1 } };
	for (int i = 0; i < ARRAY_SIZE(testdata); i++) {
		corsola_get_db_type_fake.return_val = testdata[i].test_type;
		zassert_equal(board_get_usb_pd_port_count(),
			      testdata[i].expected_board_get_port_count);
		zassert_equal(
			board_get_adjusted_usb_pd_port_count(),
			testdata[i].expected_board_get_adjusted_port_count);
	}
}
