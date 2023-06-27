/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(not_ready, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(not_ready, test_bad_tcpc)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		gpio_flags_t flags;

		zassert_ok(gpio_pin_get_config_dt(&tcpc_config[i].irq_gpio,
						  &flags),
			   "error accessing tcpc port %i", i);

		zassert_false(
			flags & GPIO_INT_ENABLE,
			"error port %i flag should not be enabled but is 0x%X",
			i, flags);
	}
}
