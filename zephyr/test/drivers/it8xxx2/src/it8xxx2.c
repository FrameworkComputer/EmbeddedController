/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/tcpci.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define IT8XXX2_PORT 1

ZTEST(tcpc_it8xxx2, test_enter_l_p_m)
{
	zassert_ok(tcpm_enter_low_power_mode(IT8XXX2_PORT));
}

ZTEST(tcpc_it8xxx2, test_set_vconn)
{
	zassert_ok(tcpm_set_vconn(IT8XXX2_PORT, 0));
	zassert_ok(tcpm_set_vconn(IT8XXX2_PORT, 1));
	zassert_ok(tcpm_set_vconn(IT8XXX2_PORT, 0));
}

ZTEST_SUITE(tcpc_it8xxx2, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
