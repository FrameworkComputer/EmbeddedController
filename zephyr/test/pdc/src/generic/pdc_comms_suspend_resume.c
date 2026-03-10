/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "chipset.h"
#include "drivers/intel_altmode.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "hooks.h"
#include "test/util.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys_clock.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(pdc_comms_suspend_resume, LOG_LEVEL_INF);

#define RTS5453P_NODE DT_NODELABEL(pdc_emul1)

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);

#define TEST_PORT 0

static void *pdc_comms_suspend_resume_setup(void)
{
	zassert_equal(2, CONFIG_USB_PD_PORT_MAX_COUNT,
		      "Expecting two USB-C ports");

	return NULL;
}

static void pdc_comms_suspend_resume_before(void *fixture)
{
	emul_pdc_reset(emul);
	emul_pdc_set_response_delay(emul, 0);
	emul_pdc_disconnect(emul);
	emul_pdc_reset_feature_flags(emul);
}

static void pdc_comms_suspend_resume_after(void *fixture)
{
}

ZTEST_SUITE(pdc_comms_suspend_resume_api, NULL, pdc_comms_suspend_resume_setup,
	    pdc_comms_suspend_resume_before, pdc_comms_suspend_resume_after,
	    NULL);

static bool pdc_state_machine_is_resumed(uint8_t state)
{
	switch (state) {
	case PDC_UNATTACHED:
	case PDC_SNK_ATTACHED:
	case PDC_SRC_ATTACHED:
	case PDC_SNK_TYPEC_ONLY:
	case PDC_SRC_TYPEC_ONLY:
		return true;
	default:
		return false;
	}
}

ZTEST(pdc_comms_suspend_resume_api, test_suspend_resume)
{
	/* Will block until all ports are suspended */
	zassert_ok(pdc_power_mgmt_set_comms_state(false));

	/* Verify ports are suspended */
	for (int p = 0; p < pdc_power_mgmt_get_usb_pd_port_count(); p++) {
		zassert_equal(PDC_SUSPENDED, pdc_power_mgmt_get_task_state(p),
			      "Port %d is not suspended. Actual state: %s", p,
			      pdc_power_mgmt_get_task_state_name(p));
	}

	/* Enable */
	zassert_ok(pdc_power_mgmt_set_comms_state(true));

	/* Verify ports are resumed */
	for (int p = 0; p < pdc_power_mgmt_get_usb_pd_port_count(); p++) {
		zassert_true(WAIT_FOR(pdc_state_machine_is_resumed(
					      pdc_power_mgmt_get_task_state(p)),
				      500000, k_msleep(10)));
	}
}

static bool check_pdc_sbu_mux_mode(enum pdc_sbu_mux_mode mode)
{
	int ret;
	enum pdc_sbu_mux_mode curr_mode = PDC_SBU_MUX_MODE_INVALID;

	ret = pdc_power_mgmt_get_sbu_mux_mode(&curr_mode, NULL);
	if (ret) {
		return false;
	}

	return curr_mode == mode;
}

ZTEST(pdc_comms_suspend_resume_api, test_suspend_resume_sbumux_debug)
{
	enum pdc_sbu_mux_mode mode;

	/* Ensure that TEST_PORT is also the CCD port */
	zassert_equal(TEST_PORT, pdc_power_mgmt_get_ccd_port());

	/* Enable an emulator feature flag to support SBU mux mode override */
	zassert_ok(emul_pdc_set_feature_flag(
		emul, EMUL_PDC_FEATURE_SBU_MUX_OVERRIDE));

	/* Force the CCD port's SBU mux into debug mode */
	zassert_ok(pdc_power_mgmt_set_sbu_mux_mode(PDC_SBU_MUX_MODE_FORCE_DBG));

	zassert_true(
		WAIT_FOR(check_pdc_sbu_mux_mode(PDC_SBU_MUX_MODE_FORCE_DBG),
			 2000000, k_msleep(50)));

	/* Will block until all ports are suspended */
	zassert_ok(pdc_power_mgmt_set_comms_state(false));

	/* Verify ports are suspended */
	for (int p = 0; p < pdc_power_mgmt_get_usb_pd_port_count(); p++) {
		zassert_equal(PDC_SUSPENDED, pdc_power_mgmt_get_task_state(p),
			      "Port %d is not suspended. Actual state: %s", p,
			      pdc_power_mgmt_get_task_state_name(p));
	}

	/* Simulate a PDC reset. This will wipe the SBU mux mode. */
	emul_pdc_reset(emul);
	zassert_ok(emul_pdc_set_feature_flag(
		emul, EMUL_PDC_FEATURE_SBU_MUX_OVERRIDE));

	/* Ensure the SBU mux is back on normal mode following the PDC reset */
	zassert_ok(emul_pdc_get_sbu_mux_mode(emul, &mode));
	zassert_equal(PDC_SBU_MUX_MODE_NORMAL, mode,
		      "SBU mux mode revert to normal after PDC reset");

	/* Enable */
	zassert_ok(pdc_power_mgmt_set_comms_state(true));

	/* Verify ports are resumed */
	for (int p = 0; p < pdc_power_mgmt_get_usb_pd_port_count(); p++) {
		zassert_true(WAIT_FOR(pdc_state_machine_is_resumed(
					      pdc_power_mgmt_get_task_state(p)),
				      2000000, k_msleep(10)));
	}

	/* Verify the CCD port goes back to force debug mode */
	zassert_true(
		WAIT_FOR(check_pdc_sbu_mux_mode(PDC_SBU_MUX_MODE_FORCE_DBG),
			 2000000, k_msleep(50)),
		"CCD port did not go back to force-debug mode");
}
