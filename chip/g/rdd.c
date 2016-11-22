/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "console.h"
#include "hooks.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_api.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

/*
 * The default PROG_DEBUG_STATE_MAP value. Used to tell the to controller send
 * an interrupt when CC1/2 are detected to be in the defined voltage range of a
 * debug accessory.
 */
#define DETECT_DEBUG 0x420
#define DETECT_DISCONNECT (~DETECT_DEBUG & 0xffff)

int debug_cable_is_attached(void)
{
	uint8_t cc1 = GREAD_FIELD(RDD, INPUT_PIN_VALUES, CC1);
	uint8_t cc2 = GREAD_FIELD(RDD, INPUT_PIN_VALUES, CC2);

	return (cc1 == cc2 && (cc1 == 3 || cc1 == 1));
}

void rdd_interrupt(void)
{
	int is_debug;

	delay_sleep_by(1 * SECOND);

	is_debug = debug_cable_is_attached();

	if (is_debug) {
		CPRINTS("Debug Accessory connected");

		/* Detect when debug cable is disconnected */
		GWRITE(RDD, PROG_DEBUG_STATE_MAP, DETECT_DISCONNECT);

		rdd_attached();
	} else if (!is_debug) {
		CPRINTS("Debug Accessory disconnected");

		/* Detect when debug cable is connected */
		GWRITE(RDD, PROG_DEBUG_STATE_MAP, DETECT_DEBUG);

		rdd_detached();

		cflush();
	}

	/* Clear interrupt */
	GWRITE_FIELD(RDD, INT_STATE, INTR_DEBUG_STATE_DETECTED, 1);
}
DECLARE_IRQ(GC_IRQNUM_RDD0_INTR_DEBUG_STATE_DETECTED_INT, rdd_interrupt, 1);

void rdd_init(void)
{
	/* Enable RDD */
	clock_enable_module(MODULE_RDD, 1);
	GWRITE(RDD, POWER_DOWN_B, 1);

	GWRITE(RDD, PROG_DEBUG_STATE_MAP, DETECT_DEBUG);

	/* Initialize the debug state based on the current cc values */
	rdd_interrupt();

	/* Enable RDD interrupts */
	task_enable_irq(GC_IRQNUM_RDD0_INTR_DEBUG_STATE_DETECTED_INT);
	GWRITE_FIELD(RDD, INT_ENABLE, INTR_DEBUG_STATE_DETECTED, 1);
}
DECLARE_HOOK(HOOK_INIT, rdd_init, HOOK_PRIO_DEFAULT);
