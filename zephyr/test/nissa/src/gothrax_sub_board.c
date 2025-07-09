/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Unit tests for program/nissa/src/sub_board.c.
 */
#include "cros_cbi.h"
#include "gothrax_sub_board.h"
#include "hooks.h"
#include "nissa_hdmi.h"
#include "usb_pd.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(usb_interrupt_c1, enum gpio_signal);

/*
 * Private bits of board code that are visible for testing.
 *
 * The cached sub-board ID needs to be cleared by tests so we can run multiple
 * tests per process, and usb_pd_count_init() needs to run following each update
 * of reported sub-board.
 */
extern enum gothrax_sub_board_type cached_sub_board;
void board_usb_pd_count_init(void);

/* Shim GPIO initialization from devicetree. */
int init_gpios(const struct device *unused);

static uint32_t fw_config_value;

/** Set the value of the CBI fw_config field returned by the fake. */
static void set_fw_config_value(uint32_t value)
{
	fw_config_value = value;
	board_usb_pd_count_init();
}

/** Custom fake for cros_cgi_get_fw_config(). */
static int get_fake_fw_config_field(enum cbi_fw_config_field_id field_id,
				    uint32_t *value)
{
	*value = fw_config_value;
	return 0;
}

#define ASSERT_GPIO_FLAGS(spec, expected)                                  \
	do {                                                               \
		gpio_flags_t flags;                                        \
		zassert_ok(gpio_emul_flags_get((spec)->port, (spec)->pin,  \
					       &flags));                   \
		zassert_equal(flags, expected,                             \
			      "actual value was %#x; expected %#x", flags, \
			      expected);                                   \
	} while (0)

static int get_gpio_output(const struct gpio_dt_spec *const spec)
{
	return gpio_emul_output_get(spec->port, spec->pin);
}

struct gothrax_sub_board_fixture {
	const struct gpio_dt_spec *sb_1;
	const struct gpio_dt_spec *sb_2;
	const struct gpio_dt_spec *sb_3;
	const struct gpio_dt_spec *sb_4;
};

static void *suite_setup_fn()
{
	struct gothrax_sub_board_fixture *fixture =
		k_malloc(sizeof(struct gothrax_sub_board_fixture));

	zassume_not_null(fixture);
	fixture->sb_1 = GPIO_DT_FROM_NODELABEL(gpio_sb_1);
	fixture->sb_2 = GPIO_DT_FROM_NODELABEL(gpio_sb_2);
	fixture->sb_3 = GPIO_DT_FROM_NODELABEL(gpio_sb_3);
	fixture->sb_4 = GPIO_DT_FROM_NODELABEL(gpio_sb_4);

	return fixture;
}

static void test_before_fn(void *fixture_)
{
	struct gothrax_sub_board_fixture *fixture = fixture_;

	/* Reset cached global state. */
	cached_sub_board = GOTHRAX_SB_UNKNOWN;
	fw_config_value = -1;

	/* Return the fake fw_config value. */
	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.custom_fake = get_fake_fw_config_field;

	/* Unconfigure sub-board GPIOs. */
	gpio_pin_configure_dt(fixture->sb_1, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(fixture->sb_2, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(fixture->sb_3, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(fixture->sb_4, GPIO_DISCONNECTED);
	/* Reset C1 interrupt to deasserted. */
	gpio_emul_input_set(fixture->sb_1->port, fixture->sb_1->pin, 1);

	RESET_FAKE(usb_interrupt_c1);
}

ZTEST_SUITE(gothrax_sub_board, NULL, suite_setup_fn, test_before_fn, NULL,
	    NULL);

ZTEST_F(gothrax_sub_board, test_usb_c_a)
{
	/* Set the sub-board, reported configuration is correct. */
	set_fw_config_value(FW_SUB_BOARD_1);
	zassert_equal(gothrax_get_sb_type(), GOTHRAX_SB_C_A);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	/* Should have fetched CBI exactly once, asking for the sub-board. */
	zassert_equal(cros_cbi_get_fw_config_fake.call_count, 1);
	zassert_equal(cros_cbi_get_fw_config_fake.arg0_history[0],
		      FW_SUB_BOARD);

	/* Run IO configuration in init. */
	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	/* Check that the sub-board GPIOs are configured correctly. */
	ASSERT_GPIO_FLAGS(fixture->sb_2 /* A1 VBUS enable */, GPIO_OUTPUT);
	ASSERT_GPIO_FLAGS(fixture->sb_1 /* C1 interrupt */,
			  GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_EDGE_FALLING);

	/* USB-C1 interrupt is handled. */
	RESET_FAKE(usb_interrupt_c1);
	gpio_emul_input_set(fixture->sb_1->port, fixture->sb_1->pin, 0);
	zassert_equal(usb_interrupt_c1_fake.call_count, 1,
		      "usb_interrupt was called %d times",
		      usb_interrupt_c1_fake.call_count);
}

ZTEST_F(gothrax_sub_board, test_usb_c_lte)
{
	set_fw_config_value(FW_SUB_BOARD_2);
	zassert_equal(gothrax_get_sb_type(), GOTHRAX_SB_C_A_LTE);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	/* USB-A controls are enabled. */
	ASSERT_GPIO_FLAGS(fixture->sb_2 /* A1 VBUS enable */, GPIO_OUTPUT);

	ASSERT_GPIO_FLAGS(fixture->sb_1 /* C1 interrupt */,
			  GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_EDGE_FALLING);

	/* USB interrupt is handled. */
	RESET_FAKE(usb_interrupt_c1);
	gpio_emul_input_set(fixture->sb_1->port, fixture->sb_1->pin, 0);
	zassert_equal(usb_interrupt_c1_fake.call_count, 1,
		      "usb_interrupt was called %d times",
		      usb_interrupt_c1_fake.call_count);

#if DT_NODE_EXISTS(DT_ALIAS(gpio_en_sub_s5_rails))
	/* LTE power gets enabled on S5. */
	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	zassert_equal(get_gpio_output(fixture->sb_2), 1);

	/* And disabled on G3. */
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_equal(get_gpio_output(fixture->sb_2), 0);
#endif
}

ZTEST_F(gothrax_sub_board, test_usb_a_hdmi)
{
	set_fw_config_value(FW_SUB_BOARD_3);
	zassert_equal(gothrax_get_sb_type(), GOTHRAX_SB_HDMI_A_LTE);
	zassert_equal(board_get_usb_pd_port_count(), 1);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	/* USB-A controls are enabled. */
	ASSERT_GPIO_FLAGS(fixture->sb_2 /* A1 VBUS enable */, GPIO_OUTPUT);

	/*
	 * HDMI IOs configured as expected. The HDMI power enable and DDC select
	 * pins are impossible to test because emulated GPIOs don't support
	 * open-drain mode, so this only checks HPD.
	 */
	ASSERT_GPIO_FLAGS(fixture->sb_4,
			  GPIO_INPUT | GPIO_ACTIVE_LOW | GPIO_INT_EDGE_BOTH);

	/* Power events adjust HDMI port power as expected. */
	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	zassert_equal(get_gpio_output(GPIO_DT_FROM_NODELABEL(gpio_hdmi_sel)),
		      1);
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_equal(get_gpio_output(GPIO_DT_FROM_NODELABEL(gpio_hdmi_sel)),
		      0);

	/* HPD input gets copied through to the output, and inverted. */
	gpio_emul_input_set(fixture->sb_4->port, fixture->sb_4->pin, 1);
	zassert_equal(
		get_gpio_output(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_hdmi_hpd)),
		0);
	gpio_emul_input_set(fixture->sb_4->port, fixture->sb_4->pin, 0);
	zassert_equal(
		get_gpio_output(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_hdmi_hpd)),
		1);
}

ZTEST(gothrax_sub_board, test_unset_board)
{
	/* fw_config with an unset sub-board means none is present. */
	set_fw_config_value(0);
	zassert_equal(gothrax_get_sb_type(), GOTHRAX_SB_NONE);
	zassert_equal(board_get_usb_pd_port_count(), 1);
}

static int get_fw_config_error(enum cbi_fw_config_field_id field,
			       uint32_t *value)
{
	return EC_ERROR_UNKNOWN;
}

ZTEST(gothrax_sub_board, test_cbi_error)
{
	/*
	 * Reading fw_config from CBI returns an error, so sub-board is treated
	 * as absent.
	 */
	cros_cbi_get_fw_config_fake.custom_fake = get_fw_config_error;
	zassert_equal(gothrax_get_sb_type(), GOTHRAX_SB_NONE);
}

/** Override default HDMI configuration to exercise the power enable as well. */
__override void nissa_configure_hdmi_power_gpios(void)
{
	nissa_configure_hdmi_rails();
	nissa_configure_hdmi_vcc();
}
