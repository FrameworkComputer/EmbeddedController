/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "extension.h"
#include "gpio.h"
#include "hooks.h"
#include "physical_presence.h"
#include "rbox.h"
#include "registers.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "timer.h"
#include "u2f_impl.h"

#define CPRINTS(format, args...) cprints(CC_RBOX, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_RBOX, format, ## args)

DECLARE_DEFERRED(deassert_ec_rst);

void power_button_release_enable_interrupt(int enable)
{
	/* Clear any leftover power button rising edge detection interrupts */
	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_RED, 1);

	if (enable) {
		/* Enable power button rising edge detection interrupt */
		GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_RED, 1);
		task_enable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_RED_INT);
	} else {
		GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_RED, 0);
		task_disable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_RED_INT);
	}
}

/**
 * Enable/disable power button interrupt.
 *
 * @param enable	Enable (!=0) or disable (==0)
 */
static void power_button_press_enable_interrupt(int enable)
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

static void power_button_release_handler(void)
{
#ifdef CR50_DEV
	CPRINTS("power button released");
#endif

	/*
	 * Let deassert_ec_rst be called deferred rather than
	 * by interrupt handler.
	 */
	hook_call_deferred(&deassert_ec_rst_data, 0);

	/* Note that this is for one-time use through the current power on. */
	power_button_release_enable_interrupt(0);
}
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_PWRB_IN_RED_INT, power_button_release_handler,
	1);

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
	power_button_press_enable_interrupt(1);
}
DECLARE_HOOK(HOOK_INIT, power_button_init, HOOK_PRIO_DEFAULT);
#endif  /* CONFIG_U2F */

void board_physical_presence_enable(int enable)
{
#ifndef CONFIG_U2F
	/* Enable/disable power button interrupts */
	power_button_press_enable_interrupt(enable);
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
		 rbox_powerbtn_is_pressed() ? "pressed" : "released");

#ifdef CR50_DEV
	pop_check_presence(1);
#endif
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn, "",
			"get the state of the power button");

/*
 * Perform a user presence check using the power button.
 */
static enum vendor_cmd_rc vc_get_pwr_btn(enum vendor_cmd_cc code,
					 void *buf,
					 size_t input_size,
					 size_t *response_size)
{
	/*
	 * The AP uses VENDOR_CC_GET_PWR_BTN to poll both for the press and
	 * release of the power button.
	 *
	 * pop_check_presence(1) returns true if a new power button press was
	 * recorded in the last 10 seconds.
	 *
	 * Indicate button release if no new presses have been recorded and the
	 * current button state is not pressed.
	 */
	if (pop_check_presence(1) == POP_TOUCH_YES ||
		rbox_powerbtn_is_pressed())
		*(uint8_t *)buf = 1;
	else
		*(uint8_t *)buf = 0;
	*response_size = 1;

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_GET_PWR_BTN, vc_get_pwr_btn);

