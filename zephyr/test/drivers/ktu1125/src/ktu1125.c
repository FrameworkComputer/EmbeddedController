/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/ktu1125.h"
#include "emul/emul_ktu1125.h"
#include "test/drivers/test_state.h"
#include "usbc_ppc.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, ppc_get_alert_status, int);

#define FFF_FAKES_LIST(FAKE) FAKE(ppc_get_alert_status)

#define INVALID_PORT 99

#define KTU1125_PORT 1
#define KTU1125_NODE DT_NODELABEL(ktu1125_emul)

const struct emul *ktu1125_emul = EMUL_DT_GET(KTU1125_NODE);

ZTEST(ppc_ktu1125, test_cover_set_frs_enable)
{
	ktu1125_drv.set_frs_enable(KTU1125_PORT, true);
	ktu1125_drv.set_frs_enable(KTU1125_PORT, false);
}

ZTEST(ppc_ktu1125, test_cover_set_vconn)
{
	ktu1125_drv.set_vconn(KTU1125_PORT, true);
	ktu1125_drv.set_vconn(KTU1125_PORT, false);
}

ZTEST(ppc_ktu1125, test_cover_vbus_sink_enable)
{
	ktu1125_drv.vbus_sink_enable(KTU1125_PORT, 0);
	ktu1125_drv.vbus_sink_enable(KTU1125_PORT, 1);
	ktu1125_drv.vbus_sink_enable(KTU1125_PORT, 0);

	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_CTRL_SW_CFG,
					KTU1125_SW_AB_EN));
	ktu1125_drv.vbus_sink_enable(KTU1125_PORT, 1);

	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_CTRL_SW_CFG,
					KTU1125_SW_AB_EN | KTU1125_POW_MODE));
	ktu1125_drv.vbus_sink_enable(KTU1125_PORT, 0);
}

ZTEST(ppc_ktu1125, test_cover_vbus_source_enable)
{
	ktu1125_drv.vbus_source_enable(KTU1125_PORT, 0);
	ktu1125_drv.vbus_source_enable(KTU1125_PORT, 1);
	ktu1125_drv.vbus_source_enable(KTU1125_PORT, 0);
}

ZTEST(ppc_ktu1125, test_cover_set_polarity)
{
	ktu1125_drv.set_polarity(KTU1125_PORT, POLARITY_CC1);
	ktu1125_drv.set_polarity(KTU1125_PORT, POLARITY_CC2);
}

ZTEST(ppc_ktu1125, test_cover_set_sbu)
{
	ktu1125_drv.set_sbu(KTU1125_PORT, 0);
	ktu1125_drv.set_sbu(KTU1125_PORT, 1);
}

ZTEST(ppc_ktu1125, test_cover_set_vbus_source_current_limit)
{
	ktu1125_drv.set_vbus_source_current_limit(KTU1125_PORT, TYPEC_RP_USB);
	ktu1125_drv.set_vbus_source_current_limit(KTU1125_PORT, TYPEC_RP_1A5);
	ktu1125_drv.set_vbus_source_current_limit(KTU1125_PORT, TYPEC_RP_3A0);
}

ZTEST(ppc_ktu1125, test_cover_discharge_vbus)
{
	ktu1125_drv.discharge_vbus(KTU1125_PORT, 0);
	ktu1125_drv.discharge_vbus(KTU1125_PORT, 1);
}

ZTEST(ppc_ktu1125, test_cover_reg_dump)
{
	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_INT_SNK, 0xff));
	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_INT_SRC, 0xff));
	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_INT_DATA, 0xff));
	ktu1125_drv.reg_dump(KTU1125_PORT);
}

ZTEST(ppc_ktu1125, test_sticky_interrupt)
{
	/*
	 * The ktu1125 interrupt handler takes evasive action after 10
	 * attempts to clear chip interrupts. Verify evasive aciton is
	 * called.
	 */
	int return_vals[] = { [0 ... 11] = 0xff, 0, 0 };

	SET_RETURN_SEQ(ppc_get_alert_status, return_vals,
		       ARRAY_SIZE(return_vals));

	ktu1125_emul_assert_irq(ktu1125_emul, true);
	ktu1125_emul_assert_irq(ktu1125_emul, false);

	/* Wait for deferred irq handler to run. */
	k_sleep(K_SECONDS(1));
}

ZTEST(ppc_ktu1125, test_normal_interrupt)
{
	int return_vals[2] = { 0xff, 0 };

	SET_RETURN_SEQ(ppc_get_alert_status, return_vals,
		       ARRAY_SIZE(return_vals));

	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_INT_SNK, 0xff));
	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_INT_SRC, 0xff));
	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_INT_DATA, 0xff));

	ktu1125_emul_assert_irq(ktu1125_emul, true);
	ktu1125_emul_assert_irq(ktu1125_emul, false);

	/* Wait for deferred irq handler to run. */
	k_sleep(K_SECONDS(1));
}

ZTEST(ppc_ktu1125, test_cover_init)
{
	zassert_equal(ktu1125_drv.init(KTU1125_PORT), EC_SUCCESS);
	zassert_not_equal(ktu1125_drv.init(INVALID_PORT), EC_SUCCESS);

	/*
	 * Verify unexpected chip ID is rejected.
	 */
	zassert_ok(ktu1125_emul_set_reg(ktu1125_emul, KTU1125_ID, 0xff));
	zassert_not_equal(ktu1125_drv.init(KTU1125_PORT), EC_SUCCESS);
}

static void ktu1125_test_before(void *data)
{
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();

	ktu1125_emul_reset(ktu1125_emul);
}

ZTEST_SUITE(ppc_ktu1125, drivers_predicate_post_main, NULL, ktu1125_test_before,
	    NULL, NULL);
