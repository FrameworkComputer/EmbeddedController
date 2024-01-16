/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usbc/ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);

static int fake_board_version;

static int fake_cbi_get_board_version(uint32_t *ver)
{
	*ver = fake_board_version;
	return 0;
}

FAKE_VOID_FUNC(ppc_chip_0_interrupt, int);
FAKE_VOID_FUNC(ppc_chip_alt_interrupt, int);
FAKE_VOID_FUNC(ppc_chip_1_interrupt, int);

ZTEST(ppc_woobat, test_ppc_init)
{
	const struct device *ppc_int_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(usb_c0_ppc_int_odl), gpios));
	const gpio_port_pins_t ppc_int_pin =
		DT_GPIO_PIN(DT_NODELABEL(usb_c0_ppc_int_odl), gpios);

	/* Board version 0, expect that main ppc is enabled. */
	RESET_FAKE(cbi_get_board_version);
	cbi_get_board_version_fake.custom_fake = fake_cbi_get_board_version;
	fake_board_version = 0;
	hook_notify(HOOK_INIT);
	zassert_ok(gpio_emul_input_set(ppc_int_gpio, ppc_int_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(ppc_int_gpio, ppc_int_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(ppc_chip_0_interrupt_fake.call_count, 1);
	zassert_equal(ppc_chip_alt_interrupt_fake.call_count, 0);
	zassert_equal(ppc_chip_1_interrupt_fake.call_count, 0);

	/* CBI access fail, fallback to board version 0 */
	RESET_FAKE(cbi_get_board_version);
	cbi_get_board_version_fake.return_val = -1;
	fake_board_version = 0;
	hook_notify(HOOK_INIT);
	zassert_ok(gpio_emul_input_set(ppc_int_gpio, ppc_int_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(ppc_int_gpio, ppc_int_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(ppc_chip_0_interrupt_fake.call_count, 2);
	zassert_equal(ppc_chip_alt_interrupt_fake.call_count, 0);
	zassert_equal(ppc_chip_1_interrupt_fake.call_count, 0);

	/* Board version 3, expect that alt ppc is enabled.
	 * Since PPC_ENABLE_ALTERNATE() is not reversible, we must test this
	 * after the board version 0 test.
	 */
	RESET_FAKE(cbi_get_board_version);
	cbi_get_board_version_fake.custom_fake = fake_cbi_get_board_version;
	fake_board_version = 3;
	hook_notify(HOOK_INIT);
	zassert_ok(gpio_emul_input_set(ppc_int_gpio, ppc_int_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(ppc_int_gpio, ppc_int_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(ppc_chip_0_interrupt_fake.call_count, 2);
	zassert_equal(ppc_chip_alt_interrupt_fake.call_count, 1);
	zassert_equal(ppc_chip_1_interrupt_fake.call_count, 0);
}

ZTEST(ppc_woobat, test_ppc_1_int)
{
	const struct device *x_ec_gpio2 = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_x_ec_gpio2), gpios));
	const gpio_port_pins_t x_ec_gpio2_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_x_ec_gpio2), gpios);

	zassert_ok(gpio_emul_input_set(x_ec_gpio2, x_ec_gpio2_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(x_ec_gpio2, x_ec_gpio2_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(ppc_chip_0_interrupt_fake.call_count, 0);
	zassert_equal(ppc_chip_alt_interrupt_fake.call_count, 0);
	zassert_equal(ppc_chip_1_interrupt_fake.call_count, 1);
}

static void *ppc_woobat_init(void)
{
	static struct ppc_drv fake_ppc_drv_0;
	static struct ppc_drv fake_ppc_drv_1;
	static struct ppc_drv fake_ppc_drv_alt;

	zassert_equal(ppc_cnt, 2);

	/* inject mocked interrupt handlers into ppc_drv and ppc_drv_alt */
	fake_ppc_drv_0 = *ppc_chips[0].drv;
	fake_ppc_drv_0.interrupt = ppc_chip_0_interrupt;
	ppc_chips[0].drv = &fake_ppc_drv_0;

	fake_ppc_drv_alt = *ppc_chips_alt[0].drv;
	fake_ppc_drv_alt.interrupt = ppc_chip_alt_interrupt;
	ppc_chips_alt[0].drv = &fake_ppc_drv_alt;

	fake_ppc_drv_1 = *ppc_chips[1].drv;
	fake_ppc_drv_1.interrupt = ppc_chip_1_interrupt;
	ppc_chips[1].drv = &fake_ppc_drv_1;

	return NULL;
}

static void ppc_woobat_before(void *fixture)
{
	RESET_FAKE(cbi_get_board_version);
	RESET_FAKE(ppc_chip_0_interrupt);
	RESET_FAKE(ppc_chip_alt_interrupt);
	RESET_FAKE(ppc_chip_1_interrupt);

	/* We have bypassed the db detection, so we force enabling the
	 * int_x_ec_gpio2.
	 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_x_ec_gpio2));
}
ZTEST_SUITE(ppc_woobat, NULL, ppc_woobat_init, ppc_woobat_before, NULL, NULL);
