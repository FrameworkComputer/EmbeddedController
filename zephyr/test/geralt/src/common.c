/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "battery.h"
#include "charge_manager.h"
#include "driver/ppc/syv682x.h"
#include "driver/ppc/syv682x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_syv682x.h"
#include "fakes.h"
#include "i2c/i2c.h"
#include "test_state.h"
#include "usb_pd.h"
#include "usb_pd_policy.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

ZTEST(geralt_common, test_none)
{
	/* placeholder test */
}

ZTEST(geralt_common, test_port_frs_disable_until_source_on)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/* all ports delay the FRS disabled pins */
		zassert_true(port_frs_disable_until_source_on(i));
	}
}

static void geralt_common_before(void *fixture)
{
}

ZTEST_SUITE(geralt_common, geralt_predicate_post_main, NULL,
	    geralt_common_before, NULL, NULL);
