/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "ec_commands.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "gpio.h"
#include "include/power.h"
#include "led.h"
#include "led_common.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#include <zephyr/ztest.h>

#include <dt-bindings/battery.h>

#define BATTERY_NODE DT_NODELABEL(battery)

#define VERIFY_LED_COLOR(color, led_id)                                    \
	{                                                                  \
		const struct led_pins_node_t *pin_node =                   \
			led_get_node(color, led_id);                       \
		for (int j = 0; j < pin_node->pins_count; j++) {           \
			int val = gpio_pin_get_dt(gpio_get_dt_spec(        \
				pin_node->gpio_pins[j].signal));           \
			int expecting = pin_node->gpio_pins[j].val;        \
			zassert_equal(expecting, val, "[%d]: %d != %d", j, \
				      expecting, val);                     \
		}                                                          \
	}

ZTEST(led_driver, test_led_control)
{
	test_set_chipset_to_power_level(POWER_S5);

	/* Exercise valid led_id, set to RESET state */
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_RESET);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_SYSRQ_DEBUG_LED);

	/* Exercise valid led_id, set to OFF state.
	 * Verify matches OFF color defined in device tree
	 */
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_OFF);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_SYSRQ_DEBUG_LED);

	/* Exercise valid led_id, set to ON state.
	 * Verify matches ON color defined in device tree
	 */
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_ON);
	VERIFY_LED_COLOR(LED_BLUE, EC_LED_ID_SYSRQ_DEBUG_LED);

	/* Exercise invalid led_id -- no change to led color */
	led_control(EC_LED_ID_LEFT_LED, LED_STATE_RESET);
	VERIFY_LED_COLOR(LED_BLUE, EC_LED_ID_SYSRQ_DEBUG_LED);
}

ZTEST(led_driver, test_led_brightness)
{
	uint8_t brightness[EC_LED_COLOR_COUNT] = { -1 };

	/* Verify LED set to OFF */
	led_set_brightness(EC_LED_ID_SYSRQ_DEBUG_LED, brightness);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_SYSRQ_DEBUG_LED);

	/* Verify LED colors defined in device tree are reflected in the
	 * brightness array.
	 */
	led_get_brightness_range(EC_LED_ID_SYSRQ_DEBUG_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_BLUE], 1);
	zassert_equal(brightness[EC_LED_COLOR_WHITE], 1);

	/* Verify LED set to WHITE */
	led_set_brightness(EC_LED_ID_SYSRQ_DEBUG_LED, brightness);
	VERIFY_LED_COLOR(LED_WHITE, EC_LED_ID_SYSRQ_DEBUG_LED);
}

ZTEST(led_driver, test_get_chipset_state)
{
	enum power_state pwr_state;

	test_set_chipset_to_g3();
	pwr_state = get_chipset_state();
	zassert_equal(pwr_state, POWER_S5, "expected=%d, returned=%d", POWER_S5,
		      pwr_state);

	test_set_chipset_to_s0();
	pwr_state = get_chipset_state();
	zassert_equal(pwr_state, POWER_S0, "expected=%d, returned=%d", POWER_S0,
		      pwr_state);

	test_set_chipset_to_power_level(POWER_S3);
	pwr_state = get_chipset_state();
	zassert_equal(pwr_state, POWER_S3, "expected=%d, returned=%d", POWER_S3,
		      pwr_state);
}

ZTEST(led_driver, test_separated_led_policies)
{
	led_auto_control(EC_LED_ID_SYSRQ_DEBUG_LED, 1);
	led_auto_control(EC_LED_ID_BATTERY_LED, 1);

	set_ac_enabled(false);
	test_set_chipset_to_power_level(POWER_S0);
	VERIFY_LED_COLOR(LED_BLUE, EC_LED_ID_SYSRQ_DEBUG_LED);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_BATTERY_LED);

	test_set_chipset_to_power_level(POWER_S3);
	VERIFY_LED_COLOR(LED_WHITE, EC_LED_ID_SYSRQ_DEBUG_LED);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_BATTERY_LED);

	test_set_chipset_to_power_level(POWER_S5);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_SYSRQ_DEBUG_LED);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_BATTERY_LED);

	set_ac_enabled(true);
	test_set_chipset_to_power_level(POWER_S0);
	VERIFY_LED_COLOR(LED_BLUE, EC_LED_ID_SYSRQ_DEBUG_LED);
	VERIFY_LED_COLOR(LED_WHITE, EC_LED_ID_BATTERY_LED);

	test_set_chipset_to_power_level(POWER_S3);
	VERIFY_LED_COLOR(LED_WHITE, EC_LED_ID_SYSRQ_DEBUG_LED);
	VERIFY_LED_COLOR(LED_WHITE, EC_LED_ID_BATTERY_LED);

	test_set_chipset_to_power_level(POWER_S5);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_SYSRQ_DEBUG_LED);
	VERIFY_LED_COLOR(LED_WHITE, EC_LED_ID_BATTERY_LED);
}

struct led_driver_fixture {
	struct tcpci_partner_data source_20v_3a;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static inline void connect_charger_to_port(struct led_driver_fixture *fixture)
{
	set_ac_enabled(true);
	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->source_20v_3a,
						  fixture->tcpci_emul),
		   NULL);
	isl923x_emul_set_adc_vbus(fixture->charger_emul,
				  PDO_FIXED_GET_VOLT(fixture->src_ext.pdo[1]));
	k_sleep(K_SECONDS(10));
}

static inline void
disconnect_charger_from_port(struct led_driver_fixture *fixture)
{
	set_ac_enabled(false);
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

static void *led_driver_setup(void)
{
	static struct led_driver_fixture fixture;

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	/* Initialized the source to supply 20V and 3A */
	tcpci_partner_init(&fixture.source_20v_3a, PD_REV20);
	fixture.source_20v_3a.extensions = tcpci_src_emul_init(
		&fixture.src_ext, &fixture.source_20v_3a, NULL);
	fixture.src_ext.pdo[1] =
		PDO_FIXED(20000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &fixture;
}

static void led_driver_before(void *data)
{
	connect_charger_to_port((struct led_driver_fixture *)data);
}

static void led_driver_after(void *data)
{
	disconnect_charger_from_port((struct led_driver_fixture *)data);
}

ZTEST_SUITE(led_driver, drivers_predicate_post_main, led_driver_setup,
	    led_driver_before, led_driver_after, NULL);

ZTEST(led_driver, test_get_battery_state)
{
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	uint16_t battery_status;

	zassert_ok(sbat_emul_get_word_val(emul, SB_BATTERY_STATUS,
					  &battery_status),
		   NULL);
	zassert_equal(battery_status & SB_STATUS_DISCHARGING, 0,
		      "Battery is discharging: %d", battery_status);
}
