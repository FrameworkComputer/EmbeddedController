/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AP state machine
 */
#include "ccd_config.h"
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
	return state == DEVICE_STATE_ON;
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
 * Set AP to the off state. Disable functionality that should only be available
 * when the AP is on.
 */
static void deferred_set_ap_off(void)
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
DECLARE_DEFERRED(deferred_set_ap_off);

/**
 * Move the AP to the ON state
 */
void set_ap_on(void)
{
	hook_call_deferred(&deferred_set_ap_off_data, -1);
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

/*
 * If TPM_RST_L is asserted, the AP is in reset. Disable all AP functionality
 * in 1 second if it remains asserted.
 */
void tpm_rst_asserted(enum gpio_signal unused)
{
	CPRINTS("%s", __func__);

	/*
	 * It's possible the signal is being pulsed. Wait 1 second to disable
	 * functionality, so it's more likely the AP is fully off and not being
	 * reset.
	 */
	hook_call_deferred(&deferred_set_ap_off_data, SECOND);

	set_state(DEVICE_STATE_DEBOUNCING);
}

/**
 * Check the initial AP state.
 */
static void init_ap_detect(void)
{
	/* Enable interrupts for AP state detection */
	gpio_enable_interrupt(GPIO_TPM_RST_L);
	gpio_enable_interrupt(GPIO_DETECT_TPM_RST_L_ASSERTED);
	/*
	 * Enable TPM reset GPIO interrupt.
	 *
	 * If the TPM_RST_L signal is already high when cr50 wakes up or
	 * transitions to high before we are able to configure the gpio then we
	 * will have missed the edge and the tpm reset isr will not get
	 * called. Check that we haven't already missed the rising edge. If we
	 * have alert tpm_rst_isr.
	 */
	if (gpio_get_level(GPIO_TPM_RST_L))
		tpm_rst_deasserted(GPIO_TPM_RST_L);
	else
		tpm_rst_asserted(GPIO_TPM_RST_L);
}
/*
 * TPM_RST_L isn't setup until board_init. Make sure init_ap_detect happens
 * after that.
 */
DECLARE_HOOK(HOOK_INIT, init_ap_detect, HOOK_PRIO_DEFAULT + 1);
