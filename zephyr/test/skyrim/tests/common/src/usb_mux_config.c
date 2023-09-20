/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "chipset.h"
#include "emul/retimer/emul_anx7483.h"
#include "host_command.h"
#include "power.h"
#include "usbc/usb_muxes.h"
#include "ztest/usb_mux_config.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define ANX7483_EMUL0 EMUL_DT_GET(DT_NODELABEL(anx7483_port0))

extern const struct anx7483_tuning_set anx7483_usb_enabled[];
extern const struct anx7483_tuning_set anx7483_dp_enabled[];
extern const struct anx7483_tuning_set anx7483_dock_noflip[];
extern const struct anx7483_tuning_set anx7483_dock_flip[];

extern const size_t anx7483_usb_enabled_count;
extern const size_t anx7483_dp_enabled_count;
extern const size_t anx7483_dock_noflip_count;
extern const size_t anx7483_dock_flip_count;

int ioex_set_flip(int port, mux_state_t mux_state);
int board_c1_ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state);
int board_mux_set(const struct usb_mux *mux, mux_state_t state);

DEFINE_FAKE_VOID_FUNC(usb_mux_enable_alternative);

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return 0;
}

FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
FAKE_VALUE_FUNC(int, extpower_is_present);
FAKE_VALUE_FUNC(enum power_state, power_get_state);
FAKE_VOID_FUNC(host_set_single_event, enum host_event_code);

test_mockable int chipset_in_state(int mask)
{
	return mask & CHIPSET_STATE_ON;
}

static void test_reset(void)
{
	RESET_FAKE(power_get_state);
	RESET_FAKE(host_set_single_event);
}

static void usb_mux_config_before(void *fixture)
{
	ARG_UNUSED(fixture);
	test_reset();
}

ZTEST_SUITE(usb_mux_config_common, NULL, NULL, usb_mux_config_before, NULL,
	    NULL);

/*
 * Winterhold is the only board that this test doesn't have
 * board_c1_ps8818_mux_set.
 */
#ifndef CONFIG_TEST_BOARD_WINTERHOLD
ZTEST(usb_mux_config_common, test_board_c1_ps8818_mux_set)
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
#endif

#if defined(CONFIG_TEST_BOARD_SKYRIM) || defined(CONFIG_TEST_BOARD_WINTERHOLD)
ZTEST(usb_mux_config_common, test_board_anx7483_c0_mux_set)
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
#endif

ZTEST(usb_mux_config_common, test_ioex_set_flip)
{
	const struct gpio_dt_spec *c0 =
		GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip);
	const struct gpio_dt_spec *c1 =
		GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip);
	int rv;

	/* Value of the corresponding pin should match flipped status. */
	rv = ioex_set_flip(0, 0);
	zassert_ok(rv);
	zassert_equal(gpio_emul_output_get(c0->port, c0->pin), 0);

	rv = ioex_set_flip(0, USB_PD_MUX_POLARITY_INVERTED);
	zassert_ok(rv);
	zassert_equal(gpio_emul_output_get(c0->port, c0->pin), 1);

	rv = ioex_set_flip(1, 0);
	zassert_ok(rv);
	zassert_equal(gpio_emul_output_get(c1->port, c1->pin), 0);

	rv = ioex_set_flip(1, USB_PD_MUX_POLARITY_INVERTED);
	zassert_ok(rv);
	zassert_equal(gpio_emul_output_get(c1->port, c1->pin), 1);
}

static int board_mux_set_test(int port)
{
	const struct usb_mux me = { .usb_port = port };

	/* Validate behavior in S0, no host event should be set. */
	power_get_state_fake.return_val = POWER_S0;
	board_mux_set(&me, USB_PD_MUX_NONE);
	zassert_equal(host_set_single_event_fake.call_count, 0);

	board_mux_set(&me, USB_PD_MUX_DP_ENABLED);
	zassert_equal(host_set_single_event_fake.call_count, 0);

	board_mux_set(&me, USB_PD_MUX_DP_ENABLED);
	zassert_equal(host_set_single_event_fake.call_count, 0);

	board_mux_set(&me, USB_PD_MUX_NONE);
	zassert_equal(host_set_single_event_fake.call_count, 0);

	/*
	 * Validate behavior in S0ix, host events should only be set if there
	 * was a change in DP status.
	 */
	power_get_state_fake.return_val = POWER_S0ix;
	board_mux_set(&me, USB_PD_MUX_NONE);
	zassert_equal(host_set_single_event_fake.call_count, 0);

	board_mux_set(&me, USB_PD_MUX_DP_ENABLED);
	zassert_equal(host_set_single_event_fake.call_count, 1);
	zassert_true(host_set_single_event_fake.arg0_val &
		     EC_HOST_EVENT_USB_MUX);

	board_mux_set(&me, USB_PD_MUX_DP_ENABLED);
	zassert_equal(host_set_single_event_fake.call_count, 1);

	board_mux_set(&me, USB_PD_MUX_NONE);
	zassert_equal(host_set_single_event_fake.call_count, 2);
	zassert_true(host_set_single_event_fake.arg0_val &
		     EC_HOST_EVENT_USB_MUX);

	return 0;
}

ZTEST(usb_mux_config_common, test_board_mux_set)
{
	zassert_ok(board_mux_set_test(0));
	test_reset();

	zassert_ok(board_mux_set_test(1));
}
