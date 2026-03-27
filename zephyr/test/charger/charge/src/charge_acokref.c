/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charger.h"
#include "charger_test.h"
#include "driver/charger/isl9241.h"
#include "emul/emul_isl9241.h"
#include "emul/emul_pdc.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

BUILD_ASSERT(!IS_ENABLED(CONFIG_PLATFORM_EC_OCPC),
	     "This test suite does not support OCPC");

#define CHARGER_NODE DT_NODELABEL(charger)
const struct emul *isl9241_emul = EMUL_DT_GET(CHARGER_NODE);

extern int extpower_present;

#define RTS5453P_NODE DT_NODELABEL(pdc_emul1)
static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);

static void test_before(void *fixture)
{
	emul_pdc_disconnect(emul);
	pdc_power_mgmt_wait_for_sync(0, -1);
}

ZTEST_SUITE(charge_acokref, charger_predicate_post_main, NULL, test_before,
	    NULL, NULL);

#ifdef CONFIG_PLATFORM_EC_CHARGER_SET_ACOKREF
ZTEST(charge_acokref, test_reset_charger_acokref)
{
	uint16_t reg_val;
	union connector_status_t connector_status = {};

	charge_manager_leave_safe_mode();

	/* Expect the default setting (0) for ACOKREF after reset. */
	reg_val = isl9241_emul_peek(isl9241_emul, ISL9241_REG_ACOK_REFERENCE);
	zassert_equal(reg_val, 0, "ACOKREF was not reset after AC disconnect");

	/* Default best PDO is 20 volts */
	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	extpower_present = 1;
	pdc_power_mgmt_wait_for_sync(0, -1);

	/* Verify ACOKREF set to non zero. This test requires the ISL9241
	 * charger and emulator.
	 */
	reg_val = isl9241_emul_peek(isl9241_emul, ISL9241_REG_ACOK_REFERENCE);

	zassert_not_equal(reg_val, 0,
			  "ACOKREF (%d) not set after connecting 20V charger",
			  reg_val);

	/*
	 * Verify that the charger's ACOKREF is reset to a default value when
	 * AC is disconnected.
	 */
	emul_pdc_disconnect(emul);
	extpower_present = 0;
	pdc_power_mgmt_wait_for_sync(0, -1);

	/* charge_state.c module should reset ACOKREF to 5V.  For the
	 * ISL9241 driver, this should clear the ACOK_REFERENCE register.
	 */
	reg_val = isl9241_emul_peek(isl9241_emul, ISL9241_REG_ACOK_REFERENCE);
	zassert_equal(reg_val, 0, "ACOKREF was not reset after AC disconnect");
}

ZTEST(charge_acokref, test_charger_acokref_params)
{
	/* Verify invalid voltage settings rejected. */
	zassert_equal(charger_set_acokref(0, -1), EC_ERROR_INVAL);
	zassert_equal(charger_set_acokref(0,
					  CONFIG_USB_PD_MAX_VOLTAGE_MV + 1000),
		      EC_ERROR_INVAL);

	/* Verify invalid charger number rejected. */
	zassert_equal(charger_set_acokref(-1, 5000), EC_ERROR_INVAL);
	zassert_equal(charger_set_acokref(board_get_charger_chip_count(), 5000),
		      EC_ERROR_INVAL);
}

#else /* CONFIG_PLATFORM_EC_CHARGER_SET_ACOKREF */
ZTEST(charge_acokref, test_charger_acokref_disabled)
{
	/* When ACOKREF is disabled, charger_set_acokref should always
	 * succeed.
	 */
	zassert_equal(charger_set_acokref(-1, 5000), EC_SUCCESS);
}
#endif /* CONFIG_PLATFORM_EC_CHARGER_SET_ACOKREF */
