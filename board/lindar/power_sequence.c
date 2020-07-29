/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Volteer board-specific power sequencing
 * Power sequencing is largely done by the platform automatically.
 * However, if platform power sequencing is buggy or needs tuning,
 * resistors can be stuffed on the board to allow the EC full control over
 * the power sequencing.
 */

#include "assert.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)
#define GPIO_SET_VERBOSE(signal, value) \
	gpio_set_level_verbose(CC_CHIPSET, signal, value)

static void board_wakeup(void)
{
	CPRINTS("%s", __func__);
	/*
	 * PP5000_USB_AG - normally enabled automatically by EN_3300_AG which
	 * is connected to the PSL_OUT of the Nuvoton.
	 *
	 * Assert the signal high during wakeup, deassert at hibernate
	 */
	GPIO_SET_VERBOSE(GPIO_EN_PP5000_USB_AG, 1);
}
DECLARE_HOOK(HOOK_INIT, board_wakeup, HOOK_PRIO_DEFAULT);

__override void board_hibernate_late(void)
{
	CPRINTS("%s", __func__);
	/* Disable PP5000_USB_AG on hibernate */
	GPIO_SET_VERBOSE(GPIO_EN_PP5000_USB_AG, 0);
}

/* Called during S5 -> S3 transition */
static void board_chipset_startup(void)
{
	CPRINTS("%s", __func__);

	/*
	 *
	 */

	/*
	 * Power on 1.8V rail,
	 * tPCH06, minimum 200us from P-P3300_DSW stable to before
	 * VCCPRIM_1P8 starting up.
	 *
	 * The transition to S5 and S3 is gated by SLP_SUS#, which Tiger Lake
	 * internally delays a minimum of 95 ms from DSW_PWROK. So no delay
	 * needed here.
	 */
	GPIO_SET_VERBOSE(GPIO_EN_PP1800_A, 1);


	/*
	 * Power on bypass rails - must be turned on after VCCIN aux
	 *
	 * tPCH34, maximum 50 ms from SLP_SUS# de-assertion to completion of
	 * primary and bypass rail, no minimum specified.
	 */
	GPIO_SET_VERBOSE(GPIO_EN_PP1050_BYPASS, 1);

}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);


/* Called during S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	CPRINTS("%s", __func__);

	/*
	 * S0 to G3 sequence (non-Deep Sx)
	 *  TigerLake Name    Net Name
	 *  VCCPRIM_3P3       PP3300_A
	 *  VCCDSW_3P3        VCCDSW_3P3 (PP3300_A)
	 *  V5.0A             PP5000_A
	 *  VCCPRIM_1P8       PP1800_A
	 *  VCCIN_AUX         PPVAR_VCCIN_AUX
	 *  VNN_BYPASS        PPVAR_VNN_BYPASS
	 *  V1.05A_BYPASS     PP1050_A_BYPASS
	 */

	/* Ice Lake shutdown already sequences first 3 rails above. */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);

	GPIO_SET_VERBOSE(GPIO_EN_PP1800_A, 0);
	GPIO_SET_VERBOSE(GPIO_EN_PP1050_BYPASS, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);





