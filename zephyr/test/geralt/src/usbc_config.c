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
#include "i2c/i2c.h"
#include "test_state.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(enum battery_present, battery_is_present);

ZTEST(usbc_config, test_none)
{
}

static void geralt_usbc_config_before(void *fixture)
{
}

ZTEST_SUITE(usbc_config, geralt_predicate_post_main, NULL,
	    geralt_usbc_config_before, NULL, NULL);
