/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "emul/emul_tcpci.h"
#include "emul/emul_smart_battery.h"
#include "emul/emul_charger.h"
#include <drivers/gpio/gpio_emul.h>
#include "battery_smart.h"
#include "tcpm/tcpci.h"
#include "ec_tasks.h"

#define TCPCI_EMUL_LABEL DT_NODELABEL(tcpci_emul)
#define BATTERY_ORD DT_DEP_ORD(DT_NODELABEL(battery))

#define GPIO_AC_OK_PATH DT_PATH(named_gpios, acok_od)
#define GPIO_AC_OK_PIN DT_GPIO_PIN(GPIO_AC_OK_PATH, gpios)

static void init_tcpm(void)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	struct i2c_emul *i2c_emul;
	struct sbat_emul_bat_data *bat;
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_AC_OK_PATH, gpios));

	set_test_runner_tid();
	zassert_ok(tcpci_tcpm_init(0), 0);
	pd_set_suspend(0, 0);
	/* Reset to disconnected state. */
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);

	/* Battery defaults to charging, so reset to not charging. */
	i2c_emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(i2c_emul);
	bat->cur = -5;

	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_AC_OK_PIN, 0), NULL);
}

static void remove_emulated_devices(void)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	/* TODO: This function should trigger gpios to signal there is nothing
	 * attached to the port.
	 */
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
}

static void test_attach_compliant_charger(void)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	struct i2c_emul *i2c_emul;
	uint16_t battery_status;
	struct charger_emul_data my_charger;
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_AC_OK_PATH, gpios));

	/* Verify battery not charging. */
	i2c_emul = sbat_emul_get_ptr(BATTERY_ORD);
	zassert_ok(sbat_emul_get_word_val(i2c_emul, SB_BATTERY_STATUS,
					  &battery_status),
		   NULL);
	zassert_not_equal(battery_status & STATUS_DISCHARGING, 0,
			  "Battery is not discharging: %d", battery_status);

	/* TODO? Send host command to verify PD_ROLE_DISCONNECTED. */

	/* Attach emulated charger. */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_AC_OK_PIN, 1), NULL);
	charger_emul_init(&my_charger);
	zassert_ok(charger_emul_connect_to_tcpci(&my_charger, tcpci_emul),
		   NULL);

	/* Wait for current ramp. */
	k_sleep(K_SECONDS(10));

	/* Verify battery charging. */
	zassert_ok(sbat_emul_get_word_val(i2c_emul, SB_BATTERY_STATUS,
					  &battery_status),
		   NULL);
	zassert_equal(battery_status & STATUS_DISCHARGING, 0,
		      "Battery is discharging: %d", battery_status);
	/* TODO: Also check voltage, current, etc. */
}

void test_suite_integration_usb(void)
{
	ztest_test_suite(integration_usb,
			 ztest_user_unit_test_setup_teardown(
				 test_attach_compliant_charger, init_tcpm,
				 remove_emulated_devices));
	ztest_run_test_suite(integration_usb);
}
