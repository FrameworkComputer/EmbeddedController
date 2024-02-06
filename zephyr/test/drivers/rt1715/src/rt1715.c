/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/rt1715.h"
#include "driver/tcpm/rt1715_public.h"
#include "driver/tcpm/tcpci.h"
#include "emul/tcpc/emul_rt1715.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define RT1715_PORT 1
#define RT1715_NODE DT_NODELABEL(rt1715_emul)

const struct emul *rt1715_emul = EMUL_DT_GET(RT1715_NODE);

ZTEST(tcpc_rt1715, test_check_vendor)
{
	int v;

	zassert_ok(tcpc_read16(RT1715_PORT, TCPC_REG_VENDOR_ID, &v));
	zassert_equal(v, RT1715_VENDOR_ID);

	tcpm_dump_registers(RT1715_PORT);
}

ZTEST(tcpc_rt1715, test_enter_l_p_m)
{
	zassert_ok(tcpm_enter_low_power_mode(RT1715_PORT));
}

ZTEST(tcpc_rt1715, test_set_vconn)
{
	zassert_ok(tcpm_set_vconn(RT1715_PORT, 0));
	zassert_ok(tcpm_set_vconn(RT1715_PORT, 1));
	zassert_ok(tcpm_set_vconn(RT1715_PORT, 0));
}

ZTEST(tcpc_rt1715, test_set_polarity)
{
	zassert_ok(tcpm_set_polarity(RT1715_PORT, POLARITY_CC1));

	zassert_ok(
		tcpci_emul_set_reg(rt1715_emul, TCPC_REG_CC_STATUS,
				   TCPC_REG_CC_STATUS_SET(0, TYPEC_CC_VOLT_RA,
							  TYPEC_CC_VOLT_OPEN)));

	zassert_ok(tcpm_set_polarity(RT1715_PORT, POLARITY_CC1));
}

static void rt1715_test_before(void *data)
{
	zassert_ok(
		tcpci_emul_set_reg(rt1715_emul, TCPC_REG_CC_STATUS,
				   TCPC_REG_CC_STATUS_SET(0, TYPEC_CC_VOLT_OPEN,
							  TYPEC_CC_VOLT_OPEN)));
}

ZTEST_SUITE(tcpc_rt1715, drivers_predicate_post_main, NULL, rt1715_test_before,
	    NULL, NULL);
