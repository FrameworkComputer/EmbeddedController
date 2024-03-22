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

FAKE_VOID_FUNC(ppc_chip_0_interrupt, int);

ZTEST(ppc_woobat, test_ppc_init)
{
	const struct device *ppc_int_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(usb_c0_ppc_int_odl), gpios));
	const gpio_port_pins_t ppc_int_pin =
		DT_GPIO_PIN(DT_NODELABEL(usb_c0_ppc_int_odl), gpios);

	hook_notify(HOOK_INIT);
	zassert_ok(gpio_emul_input_set(ppc_int_gpio, ppc_int_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(ppc_int_gpio, ppc_int_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(ppc_chip_0_interrupt_fake.call_count, 1);
}

static void *ppc_woobat_init(void)
{
	static struct ppc_drv fake_ppc_drv_0;

	zassert_equal(ppc_cnt, 1);

	/* inject mocked interrupt handlers into ppc_drv */
	fake_ppc_drv_0 = *ppc_chips[0].drv;
	fake_ppc_drv_0.interrupt = ppc_chip_0_interrupt;
	ppc_chips[0].drv = &fake_ppc_drv_0;

	return NULL;
}

static void ppc_woobat_before(void *fixture)
{
	RESET_FAKE(ppc_chip_0_interrupt);
}
ZTEST_SUITE(ppc_woobat, NULL, ppc_woobat_init, ppc_woobat_before, NULL, NULL);
