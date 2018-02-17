/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "physical_presence.h"
#include "rbox.h"
#include "registers.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_RBOX, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_RBOX, format, ## args)

/**
 * Enable/disable power button interrupt.
 *
 * @param enable	Enable (!=0) or disable (==0)
 */
static void power_button_enable_interrupt(int enable)
{
	if (enable) {
		/* Clear any leftover power button interrupts */
		GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);

		/* Enable power button interrupt */
		GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 1);
		task_enable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);
	} else {
		GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 0);
		task_disable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);
	}
}

static void power_button_handler(void)
{
	CPRINTS("power button pressed");

	if (physical_detect_press() != EC_SUCCESS) {
		/* Not consumed by physical detect */
#ifdef CONFIG_U2F
		/* Track last power button press for U2F */
		power_button_record();
#endif
	}

	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);
}
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT, power_button_handler, 1);

#ifdef CONFIG_U2F
static void power_button_init(void)
{
	/*
	 * Enable power button interrupts all the time for U2F.
	 *
	 * Ideally U2F should only enable physical presence after the start of
	 * a U2F request (using atomic operations for the PP enable mask so it
	 * plays nicely with CCD config), but that doesn't happen yet.
	 */
	power_button_enable_interrupt(1);
}
DECLARE_HOOK(HOOK_INIT, power_button_init, HOOK_PRIO_DEFAULT);
#endif  /* CONFIG_U2F */

void board_physical_presence_enable(int enable)
{
#ifndef CONFIG_U2F
	/* Enable/disable power button interrupts */
	power_button_enable_interrupt(enable);
#endif

	/* Stay awake while we're doing this, just in case. */
	if (enable)
		disable_sleep(SLEEP_MASK_PHYSICAL_PRESENCE);
	else
		enable_sleep(SLEEP_MASK_PHYSICAL_PRESENCE);
}

static int command_powerbtn(int argc, char **argv)
{
	ccprintf("powerbtn: %s\n",
		 rbox_powerbtn_is_pressed() ? "pressed\n" : "released\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn, "",
			"get the state of the power button");
