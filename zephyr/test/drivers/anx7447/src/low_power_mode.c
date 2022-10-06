/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/anx7447_public.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "usb_pd.h"

#define ANX7447_NODE DT_NODELABEL(anx7447_emul)

static const int tcpm_anx7447_port = USBC_PORT_C0;

ZTEST_SUITE(low_power_mode, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST(low_power_mode, enter_low_power_in_source_mode)
{
	uint16_t reg_val = 0;
	const struct emul *anx7447_emul = EMUL_DT_GET(ANX7447_NODE);

	pd_set_dual_role(tcpm_anx7447_port, PD_DRP_FORCE_SOURCE);
	tcpci_emul_set_reg(anx7447_emul, TCPC_REG_ROLE_CTRL, 0);

	zassert_ok(anx7447_tcpm_drv.enter_low_power_mode(tcpm_anx7447_port),
		   "Cannot enter low power mode");
	zassert_ok(tcpci_emul_get_reg(anx7447_emul, TCPC_REG_ROLE_CTRL,
				      &reg_val),
		   "Cannot get TCPC Role Register value");
	zassert_equal(
		reg_val,
		TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, TYPEC_RP_USB, TYPEC_CC_RP,
				       TYPEC_CC_RP),
		"Role register value is not as expected while entering low power mode");
}

ZTEST(low_power_mode, enter_low_power_not_in_source_mode)
{
	uint16_t reg_val = 0;
	const struct emul *anx7447_emul = EMUL_DT_GET(ANX7447_NODE);

	pd_set_dual_role(tcpm_anx7447_port, PD_DRP_FORCE_SINK);
	tcpci_emul_set_reg(anx7447_emul, TCPC_REG_ROLE_CTRL, 0);

	zassert_ok(anx7447_tcpm_drv.enter_low_power_mode(tcpm_anx7447_port),
		   "Cannot enter low power mode");
	zassert_ok(tcpci_emul_get_reg(anx7447_emul, TCPC_REG_ROLE_CTRL,
				      &reg_val),
		   "Cannot get TCPC Role Register value");
	zassert_equal(reg_val, 0, "Role register value is changed");
}
