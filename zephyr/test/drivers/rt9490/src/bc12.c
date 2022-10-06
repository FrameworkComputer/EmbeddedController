/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include "charger.h"
#include "charge_manager.h"
#include "driver/charger/rt9490.h"
#include "driver/tcpm/tcpci.h"
#include "emul/emul_rt9490.h"
#include "emul/tcpc/emul_tcpci.h"
#include "i2c.h"
#include "test/drivers/test_state.h"
#include "timer.h"
#include "usb_charge.h"

static const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(rt9490));
static const struct emul *tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
static const int chgnum = CHARGER_SOLO;

void run_bc12_test(int reg_value, enum charge_supplier expected_result)
{
	int port = 0;

	/* simulate plug, expect bc12 detection starting. */
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_PRES |
				   TCPC_REG_POWER_STATUS_VBUS_DET);
	zassert_ok(tcpc_config[port].drv->init(port), NULL);
	zassert_true(tcpc_config[port].drv->check_vbus_level(port,
							     VBUS_PRESENT),
		     NULL);

	usb_charger_task_set_event(port, USB_CHG_EVENT_VBUS);
	msleep(1);
	zassert_true(rt9490_emul_peek_reg(emul, RT9490_REG_CHG_CTRL2) &
			     RT9490_BC12_EN,
		     NULL);

	/*
	 * simulate triggering interrupt on bc12 detection done, and verify the
	 * result.
	 */
	zassert_ok(rt9490_emul_write_reg(emul, RT9490_REG_CHG_IRQ_FLAG1,
					 RT9490_BC12_DONE_FLAG));
	zassert_ok(
		rt9490_emul_write_reg(emul, RT9490_REG_CHG_STATUS1, reg_value));
	rt9490_interrupt(port);
	/* wait for deferred task scheduled, this takes longer. */
	msleep(500);
	zassert_false(rt9490_emul_peek_reg(emul, RT9490_REG_CHG_CTRL2) &
			      RT9490_BC12_EN,
		      NULL);
	zassert_equal(charge_manager_get_supplier(), expected_result, NULL);

	/* simulate unplug */
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	zassert_ok(tcpc_config[port].drv->init(port), NULL);
	zassert_false(tcpc_config[port].drv->check_vbus_level(port,
							      VBUS_PRESENT),
		      NULL);

	usb_charger_task_set_event(port, USB_CHG_EVENT_VBUS);
	msleep(1);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_NONE,
		      NULL);
}

ZTEST(rt9490_bc12, test_detection_flow)
{
	int port = 0;

	/* make charge manager thinks port 0 is chargable */
	msleep(500);
	usb_charger_task_set_event(port, USB_CHG_EVENT_DR_UFP);
	charge_manager_update_dualrole(port, CAP_DEDICATED);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_NONE,
		      NULL);
	msleep(1);

	run_bc12_test(RT9490_DCP << RT9490_VBUS_STAT_SHIFT,
		      CHARGE_SUPPLIER_BC12_DCP);
	run_bc12_test(RT9490_CDP << RT9490_VBUS_STAT_SHIFT,
		      CHARGE_SUPPLIER_BC12_CDP);
	run_bc12_test(RT9490_SDP << RT9490_VBUS_STAT_SHIFT,
		      CHARGE_SUPPLIER_BC12_SDP);
	run_bc12_test(0xA, CHARGE_SUPPLIER_VBUS); /* unknown type */
}

static void reset_emul(void *fixture)
{
	rt9490_emul_reset_regs(emul);
	rt9490_drv.init(chgnum);
}

ZTEST_SUITE(rt9490_bc12, drivers_predicate_post_main, NULL, reset_emul, NULL,
	    NULL);
