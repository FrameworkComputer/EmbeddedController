/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <gpio.h>
#include <usbc_ppc.h>

FAKE_VOID_FUNC(nx20p348x_interrupt, int);
DEFINE_FAKE_VOID_FUNC(nx20p348x_interrupt, int);

static void ppc_config_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(nx20p348x_interrupt);
}

void ppc_interrupt(enum gpio_signal signal);

ZTEST_SUITE(ppc_config, NULL, NULL, ppc_config_before, NULL, NULL);

ZTEST(ppc_config, ppc_interrupt_c0)
{
	ppc_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	zassert_equal(nx20p348x_interrupt_fake.call_count, 1);
	/* port */
	zassert_equal(nx20p348x_interrupt_fake.arg0_val, 0);
}

ZTEST(ppc_config, ppc_interrupt_c1)
{
	ppc_interrupt(GPIO_USB_C1_PPC_INT_ODL);
	zassert_equal(nx20p348x_interrupt_fake.call_count, 1);
	/* port */
	zassert_equal(nx20p348x_interrupt_fake.arg0_val, 1);
}
