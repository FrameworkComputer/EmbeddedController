/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AP state machine
 */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

static enum device_state state = DEVICE_STATE_INIT;

void print_ap_state(void)
{
	ccprintf("AP:      %s\n", device_state_name(state));
}

int ap_is_on(void)
{
	/* Debouncing and on are both still on */
	return (state == DEVICE_STATE_DEBOUNCING || state == DEVICE_STATE_ON);
}

/**
 * Set the AP state.
 *
 * Done as a function to make it easier to debug state transitions.  Note that
 * this ONLY sets the state (and possibly prints debug info), and doesn't do
 * all the additional transition work that set_ap_on(), etc. do.
 *
 * @param new_state	State to set.
 */
static void set_state(enum device_state new_state)
{
#ifdef CR50_DEBUG_AP_STATE
	/* Print all state transitions.  May spam the console. */
	if (state != new_state)
		CPRINTS("AP %s -> %s",
			device_state_name(state), device_state_name(new_state));
#endif
	state = new_state;
}

/**
 * Set AP to the off state
 */
static void set_ap_off(void)
{
	CPRINTS("AP off");
	set_state(DEVICE_STATE_OFF);

	/*
	 * If TPM is configured then the INT_AP_L signal is used as a low pulse
	 * trigger to sync transactions with the host. By default Cr50 is
	 * driving this line high, but when the AP powers off, the 1.8V rail
	 * that it's pulled up to will be off and cause excessive power to be
	 * consumed by the Cr50. Set INT_AP_L as an input while the AP is
	 * powered off.
	 */
	gpio_set_flags(GPIO_INT_AP_L, GPIO_INPUT);

	ccd_update_state();

	/*
	 * We don't enable deep sleep on ARM devices yet, as its processing
	 * there will require more support on the AP side than is available
	 * now.
	 *
	 * Note: Presence of platform reset is a poor indicator of deep sleep
	 * support.  It happens to be correlated with ARM vs x86 at present.
	 */
	if (board_deep_sleep_allowed())
		enable_deep_sleep();
}

/**
 * Move the AP to the ON state
 */
static void set_ap_on(void)
{
	CPRINTS("AP on");
	set_state(DEVICE_STATE_ON);

	/*
	 * AP is powering up, set the host sync signal to output and set it
	 * high which is the default level.
	 */
	gpio_set_flags(GPIO_INT_AP_L, GPIO_OUT_HIGH);
	gpio_set_level(GPIO_INT_AP_L, 1);

	ccd_update_state();

	if (board_deep_sleep_allowed())
		disable_deep_sleep();
}

/**
 * Handle moving the AP to the OFF state from a deferred interrupt handler.
 *
 * Needs to make additional state checks to avoid double-on in case ap_detect()
 * has run in the meantime.
 */
void set_ap_on_deferred(void)
{
	/* If we were debouncing ON->OFF, cancel it because we're still on */
	if (state == DEVICE_STATE_DEBOUNCING)
		set_state(DEVICE_STATE_ON);

	/* If AP isn't already on, make it so */
	if (state != DEVICE_STATE_ON)
		set_ap_on();
}
DECLARE_DEFERRED(set_ap_on_deferred);

/**
 * Interrupt handler for AP detect asserted
 */
void ap_detect_asserted(enum gpio_signal signal)
{
	gpio_disable_interrupt(GPIO_DETECT_AP);
	hook_call_deferred(&set_ap_on_deferred_data, 0);
}

/**
 * Detect state machine
 */
static void ap_detect(void)
{
	int detect;

	if (board_detect_ap_with_tpm_rst()) {
		/* AP is detected if platform reset is deasserted */
		detect = gpio_get_level(GPIO_TPM_RST_L);
	} else {
		/* Disable interrupts if we had them on for debouncing */
		gpio_disable_interrupt(GPIO_DETECT_AP);

		/* AP is detected if it's driving its UART TX signal */
		detect = gpio_get_level(GPIO_DETECT_AP);
	}

	/* Handle detecting device */
	if (detect) {
		/*
		 * If we were debouncing ON->OFF, cancel debouncing and go back
		 * to the ON state.
		 */
		if (state == DEVICE_STATE_DEBOUNCING)
			set_state(DEVICE_STATE_ON);

		/* If we're already ON, done */
		if (state == DEVICE_STATE_ON)
			return;

		if (board_detect_ap_with_tpm_rst()) {
			/*
			 * The platform reset handler has not run yet;
			 * otherwise, it would have already turned the AP on
			 * and we wouldn't get here.
			 *
			 * This can happen if the hook task calls ap_detect()
			 * before deferred_tpm_rst_isr().  In this case, the
			 * deferred handler is already pending so calling the
			 * ISR has no effect.
			 *
			 * But we may actually have missed the edge.  In that
			 * case, calling the ISR makes sure we don't miss the
			 * reset.  It will call set_ap_on_deferred() to move
			 * the AP to the ON state.
			 */
			CPRINTS("AP detect calling tpm_rst_deasserted()");
			tpm_rst_deasserted(GPIO_TPM_RST_L);
		} else {
			/* We're responsible for setting the AP state to ON */
			set_ap_on();
		}

		return;
	}

	/* AP wasn't detected.  If we're already off, done. */
	if (state == DEVICE_STATE_OFF)
		return;

	/* If we were debouncing, we're now sure we're off */
	if (state == DEVICE_STATE_DEBOUNCING ||
	    state == DEVICE_STATE_INIT_DEBOUNCING) {
		set_ap_off();
		return;
	}

	/*
	 * Otherwise, we were on before and haven't detected the AP.  But we
	 * don't know if that's because the AP is actually off, or because the
	 * AP UART is sending a 0-bit or temporarily asserting platform reset.
	 * So start debouncing.
	 */
	if (state == DEVICE_STATE_INIT)
		set_state(DEVICE_STATE_INIT_DEBOUNCING);
	else
		set_state(DEVICE_STATE_DEBOUNCING);

	/* If we're using AP UART RX for detect, enable its interrupt */
	if (!board_detect_ap_with_tpm_rst())
		gpio_enable_interrupt(GPIO_DETECT_AP);
}
DECLARE_HOOK(HOOK_SECOND, ap_detect, HOOK_PRIO_DEFAULT);
