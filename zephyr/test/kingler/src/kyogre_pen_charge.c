/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

extern volatile int pen_timer;
extern int pen_charge_state;
extern uint8_t flags;
extern void pen_charge(void);

enum {
	STATE_ERROR, /* Stopped charging for ERR_TIME */
	STATE_CHARGE, /* Started Charging for CHG_TIME */
	STATE_STOP, /* Stopped charging for STP_TIME */
};

#define PEN_FAULT_DETECT BIT(0)

#define CHG_TIME 43200 /* 12 hours */
#define STP_TIME 10 /* 10 seconds */
#define ERR_TIME 600 /* 10 minutes */

FAKE_VOID_FUNC(x_ec_interrupt);
FAKE_VOID_FUNC(lsm6dso_interrupt);
FAKE_VOID_FUNC(lis2dw12_interrupt);

static void *kyogre_pen_charge_setup(void)
{
	hook_notify(HOOK_INIT);
	return NULL;
}

void pen_fault_interrupt(enum gpio_signal signal)
{
	flags |= PEN_FAULT_DETECT;
}

ZTEST_SUITE(main_pen_charge, NULL, kyogre_pen_charge_setup, NULL, NULL, NULL);

ZTEST(main_pen_charge, test_main_pen_charge)
{
	int i;

	const struct device *pen_fault_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(pen_fault_od), gpios));
	const gpio_port_pins_t pen_fault_pin =
		DT_GPIO_PIN(DT_NODELABEL(pen_fault_od), gpios);

	/* Verify the initial state is STOP */
	zassert_equal(pen_charge_state, STATE_STOP, "pen_charge_state=%d",
		      pen_charge_state);

	/* Verify the state is changed from STOP to CHARGE when the timer is
	 * expired */
	for (i = 0; i < STP_TIME; i++) {
		zassert_equal(pen_charge_state, STATE_STOP,
			      "pen_charge_state=%d", pen_charge_state);
		pen_charge();
	}
	zassert_equal(pen_charge_state, STATE_CHARGE, "pen_charge_state=%d",
		      pen_charge_state);

	/* Verif the state is changed from CHARGE to STOP when the timer is
	 * expired */
	for (i = 0; i < CHG_TIME; i++) {
		zassert_equal(pen_charge_state, STATE_CHARGE,
			      "pen_charge_state=%d", pen_charge_state);
		pen_charge();
	}
	zassert_equal(pen_charge_state, STATE_STOP, "pen_charge_state=%d",
		      pen_charge_state);

	/* Verify the state is changed from STOP to ERROR when pen fault is
	 * detected*/
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	pen_charge();
	zassert_equal(pen_charge_state, STATE_ERROR, "pen_charge_state=%d",
		      pen_charge_state);

	/* Verify the state is changed from ERROR to CHARGE when the timer is
	 * expired */
	for (i = 0; i < ERR_TIME - 1; i++) {
		zassert_equal(pen_charge_state, STATE_ERROR,
			      "pen_charge_state=%d", pen_charge_state);
		pen_charge();
	}
	zassert_equal(pen_charge_state, STATE_CHARGE, "pen_charge_state=%d",
		      pen_charge_state);

	/* Verify the state is changed from CHARGE to ERROR when pen fault is
	 * detected*/
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	pen_charge();
	zassert_equal(pen_charge_state, STATE_ERROR, "pen_charge_state=%d",
		      pen_charge_state);

	/* Verify the state is changed from ERROR to CHARGE when the timer is
	 * expired */
	for (i = 0; i < ERR_TIME - 1; i++) {
		zassert_equal(pen_charge_state, STATE_ERROR,
			      "pen_charge_state=%d", pen_charge_state);
		pen_charge();
	}
	zassert_equal(pen_charge_state, STATE_CHARGE, "pen_charge_state=%d",
		      pen_charge_state);

	/* Verify the pen_timer is not reset to ERR_TIME even if multiple pen
	 * faults are detected during ERROR state */
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	pen_charge(); // pen_timer = ERR_TIME-1
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	pen_charge(); // pen_timer = ERR_TIME-2
	zassert_equal(pen_timer, ERR_TIME - 2, "pen_charge_state=%d",
		      pen_timer);
}
