/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_amd_fp6.h"
#include "hooks.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_mux.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
#define AMD_FP6_NODE DT_NODELABEL(amd_fp6_emul0)

struct amd_fp6_usb_mux_fixture {
	const struct emul *amd_fp6_emul;
};

static void *amd_fp6_usb_mux_setup(void)
{
	static struct amd_fp6_usb_mux_fixture fix;

	fix.amd_fp6_emul = EMUL_DT_GET(AMD_FP6_NODE);

	return &fix;
}

static void amd_fp6_usb_mux_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Default to S0 for most cases */
	test_set_chipset_to_s0();
}

ZTEST_SUITE(amd_fp6_usb_mux, drivers_predicate_post_main, amd_fp6_usb_mux_setup,
	    amd_fp6_usb_mux_before, NULL, NULL);

ZTEST(amd_fp6_usb_mux, test_usb_mode_set)
{
	/* Test a basic set to USB mode */
	usb_mux_set(TEST_PORT, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 0);

	zassert_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_USB_ENABLED);
}

ZTEST(amd_fp6_usb_mux, test_dp_mode_set)
{
	/* Test a basic set to DP mode */
	usb_mux_set(TEST_PORT, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT, 0);

	zassert_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_DP_ENABLED);
}

ZTEST(amd_fp6_usb_mux, test_dock_mode_set)
{
	/* Test a basic set to docked mode */
	usb_mux_set(TEST_PORT, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT, 0);

	zassert_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_DOCK);
}

ZTEST(amd_fp6_usb_mux, test_safe_mode_set)
{
	/* Test a basic set to safe mode */
	usb_mux_set(TEST_PORT, USB_PD_MUX_SAFE_MODE, USB_SWITCH_CONNECT, 0);

	/* Note: this driver uses "none" and "safe" interchangeably */
	zassert_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_NONE);
}

ZTEST(amd_fp6_usb_mux, test_none_set)
{
	/* Test a basic set to none */
	usb_mux_set(TEST_PORT, USB_PD_MUX_NONE, USB_SWITCH_CONNECT, 0);

	zassert_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_NONE);
}

ZTEST(amd_fp6_usb_mux, test_dp_flipped_set)
{
	/* Test a basic set to DP mode but flipped */
	usb_mux_set(TEST_PORT, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT, 1);

	zassert_equal(usb_mux_get(TEST_PORT),
		      USB_PD_MUX_DP_ENABLED | USB_PD_MUX_POLARITY_INVERTED);
}

ZTEST(amd_fp6_usb_mux, test_hpd_unsupported)
{
	/* Try to set HPD on the mux */
	usb_mux_set(TEST_PORT, USB_PD_MUX_HPD_LVL, USB_SWITCH_CONNECT, 0);

	/* And observe it didn't work */
	zassert_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_NONE);
}

ZTEST_F(amd_fp6_usb_mux, test_mux_not_ready)
{
	/* Set the crossbar to not ready yet */
	amd_fp6_emul_set_xbar(fixture->amd_fp6_emul, false);

	/* Send a basic set to USB mode */
	usb_mux_set(TEST_PORT, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 0);

	k_sleep(K_MSEC(100));
	zassert_not_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_USB_ENABLED);

	/* Allow the crossbar to be ready now */
	amd_fp6_emul_set_xbar(fixture->amd_fp6_emul, true);
	/* Driver retry is 1 second, so sleep for twice that */
	k_sleep(K_SECONDS(2));

	zassert_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_USB_ENABLED);
}

ZTEST_F(amd_fp6_usb_mux, test_chipset_reset)
{
	/* Start with a set to dock mode but flipped */
	usb_mux_set(TEST_PORT, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT, 1);

	zassert_equal(usb_mux_get(TEST_PORT),
		      USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED);

	/* Now "reset the SoC" with a register clear and hook notify */
	amd_fp6_emul_reset_regs(fixture->amd_fp6_emul);
	zassert_equal(usb_mux_get(TEST_PORT), USB_PD_MUX_NONE);
	hook_notify(HOOK_CHIPSET_RESET);
	/* Driver retry is 1 second, so sleep for twice that */
	k_sleep(K_SECONDS(2));

	zassert_equal(usb_mux_get(TEST_PORT),
		      USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED);
}

ZTEST_F(amd_fp6_usb_mux, test_long_command)
{
	/* Allow the mux to take a while, like on real systems */
	amd_fp6_emul_set_delay(fixture->amd_fp6_emul, 100);

	/* Send a basic set to USB mode */
	usb_mux_set(TEST_PORT, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 1);

	k_sleep(K_MSEC(200));
	zassert_equal(usb_mux_get(TEST_PORT),
		      USB_PD_MUX_USB_ENABLED | USB_PD_MUX_POLARITY_INVERTED);
}
