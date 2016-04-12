/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "usb_api.h"

#define CCD_PHY USB_SEL_PHY1
#define AP_PHY USB_SEL_PHY0

uint16_t ccd_detect;

static int debug_cable_is_detected(void)
{
	uint8_t cc1 = GREAD_FIELD(RDD, INPUT_PIN_VALUES, CC1);
	uint8_t cc2 = GREAD_FIELD(RDD, INPUT_PIN_VALUES, CC2);

	return (cc1 == cc2 && (cc1 == 3 || cc1 == 1));
}

void rdd_interrupt(void)
{
	if (debug_cable_is_detected()) {
		ccprintf("Debug Accessory connected\n");
		/* Detect when debug cable is disconnected */
		GWRITE(RDD, PROG_DEBUG_STATE_MAP, ~ccd_detect);

		/* Select the CCD PHY */
		usb_select_phy(CCD_PHY);
	} else {
		ccprintf("Debug Accessory disconnected\n");
		/* Detect when debug cable is connected */
		GWRITE(RDD, PROG_DEBUG_STATE_MAP, ccd_detect);

		/* Select the AP PHY */
		usb_select_phy(AP_PHY);
	}

	/* Connect to selected phy */
	usb_init();

	/* Clear interrupt */
	GWRITE_FIELD(RDD, INT_STATE, INTR_DEBUG_STATE_DETECTED, 1);
}
DECLARE_IRQ(GC_IRQNUM_RDD0_INTR_DEBUG_STATE_DETECTED_INT, rdd_interrupt, 1);

void rdd_init(void)
{
	/* Enable RDD */
	clock_enable_module(MODULE_RDD, 1);
	GWRITE(RDD, POWER_DOWN_B, 1);

	ccd_detect = GREAD(RDD, PROG_DEBUG_STATE_MAP);
	/* Detect cable disconnect if CCD is enabled */
	if (usb_get_phy() == CCD_PHY)
		GWRITE(RDD, PROG_DEBUG_STATE_MAP, ~ccd_detect);

	/* Enable RDD interrupts */
	task_enable_irq(GC_IRQNUM_RDD0_INTR_DEBUG_STATE_DETECTED_INT);
	GWRITE_FIELD(RDD, INT_ENABLE, INTR_DEBUG_STATE_DETECTED, 1);
}
DECLARE_HOOK(HOOK_INIT, rdd_init, HOOK_PRIO_DEFAULT);

static int command_test_rdd(int argc, char **argv)
{
	GWRITE_FIELD(RDD, INT_TEST, INTR_DEBUG_STATE_DETECTED, 1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(test_rdd, command_test_rdd, "", "", NULL);
