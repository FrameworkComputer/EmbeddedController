/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "console.h"
#include "gpio.h"
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

/*
 * The interrupt only triggers when the debug state is detected.  If we want to
 * trigger an interrupt when the debug state is *not* detected, we need to
 * program the bit-inverse.
 */
#define DETECT_DISCONNECT (~DETECT_DEBUG & 0xffff)

/* State of RDD CC detection */
static enum device_state state = DEVICE_STATE_DISCONNECTED;

/* Force detecting a debug accessory (ignore RDD CC detect hardware) */
static int force_detected;

/*
 * The Rdd state. This is saved in the rdd_interrupt to make sure the state is
 * stable.
 */
static uint8_t rdd_is_detected_shadow;

/**
 * Get instantaneous cable detect state
 *
 * @return 1 if debug accessory is detected, 0 if not detected
 */
uint8_t rdd_is_detected(void)
{
	return rdd_is_detected_shadow;
}

void print_rdd_state(void)
{
	ccprintf("Rdd:     %s\n",
		 force_detected ? "keepalive" : device_state_name(state));
}

/**
 * Handle debug accessory disconnecting
 */
static void rdd_disconnect(void)
{
	CPRINTS("Rdd disconnect");
	state = DEVICE_STATE_DISCONNECTED;

	/*
	 * Stop pulling CCD_MODE_L low.  The internal pullup configured in the
	 * pinmux will pull the signal back high, unless the EC is also pulling
	 * it low.
	 *
	 * This disables the SBUx muxes, if we were the only one driving
	 * CCD_MODE_L.
	 */
	gpio_set_level(GPIO_CCD_MODE_L, 1);
}
DECLARE_DEFERRED(rdd_disconnect);

/**
 * Handle debug accessory connecting
 *
 * This can be deferred from both rdd_detect() and the interrupt handler, so
 * it needs to check the current state to determine whether we're already
 * connected.
 */
static void rdd_connect(void)
{
	/* If we were debouncing, we're done, and still connected */
	if (state == DEVICE_STATE_DEBOUNCING)
		state = DEVICE_STATE_CONNECTED;

	/* If we're already connected, done */
	if (state == DEVICE_STATE_CONNECTED)
		return;

	/* We were previously disconnected, so connect */
	CPRINTS("Rdd connect");
	state = DEVICE_STATE_CONNECTED;

	/* Assert CCD_MODE_L to enable the SBUx muxes */
	gpio_set_level(GPIO_CCD_MODE_L, 0);
}
DECLARE_DEFERRED(rdd_connect);

/**
 * Debug accessory detect interrupt
 */
static void rdd_interrupt(void)
{
	uint8_t cc1 = GREAD_FIELD(RDD, INPUT_PIN_VALUES, CC1);
	uint8_t cc2 = GREAD_FIELD(RDD, INPUT_PIN_VALUES, CC2);

	/* Save the rdd state while the cc lines are stable. */
	rdd_is_detected_shadow = (cc1 == cc2 && (cc1 == 3 || cc1 == 1));

	/*
	 * The Rdd detector is level-sensitive with debounce.  That is, it
	 * samples the RDCCx pin states.  If they're different, it resets the
	 * wait counter.  If they're the same, it decrements the wait counter.
	 * Then if the counter is zero, and the state we're looking for matches
	 * the map, it fires the interrupt.
	 *
	 * Note that the counter *remains* zero until the pin states change.
	 *
	 * If we want to be able to wake on Rdd change, then interrupts need to
	 * remain enabled.  Each time we get an interrupt, we'll toggle the map
	 * we're looking for to the opposite state.  That stops the interrupt
	 * from continuing to fire on the current state.  When the pins settle
	 * into a new state, we'll fire the interrupt again.
	 *
	 * Even with that, we can still get a double interrupt now and then,
	 * because the Rdd module runs on a different clock than we do.  So the
	 * write we do to change the state map may not be picked up until the
	 * next clock, when the Rdd module has already generated its next
	 * interrupt based on the old map.  This is harmless, because we're
	 * unlikely to actually trigger the deferred function twice, and it
	 * doesn't care if we do anyway because on the second call it'll
	 * already be in the connected state.
	 */
	if (rdd_is_detected()) {
		/* Accessory detected; toggle to looking for disconnect */
		GWRITE(RDD, PROG_DEBUG_STATE_MAP, DETECT_DISCONNECT);

		/* Cancel any pending disconnects */
		hook_call_deferred(&rdd_disconnect_data, -1);
		/*
		 * Trigger the deferred handler so that we move back into the
		 * connected state before our debounce interval expires.
		 */
		hook_call_deferred(&rdd_connect_data, 0);
	} else {
		/*
		 * Skip disconnecting Rdd, if rdd is force detected. If Rdd is
		 * already disconnected, no need to do it again.
		 */
		if (!force_detected && state != DEVICE_STATE_DISCONNECTED) {
			/* Debounce disconnect for 1 second */
			state = DEVICE_STATE_DEBOUNCING;
			hook_call_deferred(&rdd_disconnect_data, SECOND);
		}
		/* Not detected; toggle to looking for connect. */
		GWRITE(RDD, PROG_DEBUG_STATE_MAP, DETECT_DEBUG);
	}

	/* Make sure we stay awake long enough to advance the state machine */
	delay_sleep_by(1 * SECOND);

	/* Clear the interrupt */
	GWRITE_FIELD(RDD, INT_STATE, INTR_DEBUG_STATE_DETECTED, 1);
}
DECLARE_IRQ(GC_IRQNUM_RDD0_INTR_DEBUG_STATE_DETECTED_INT, rdd_interrupt, 1);

void init_rdd_state(void)
{
	/* Enable RDD hardware */
	clock_enable_module(MODULE_RDD, 1);
	GWRITE(RDD, POWER_DOWN_B, 1);

	/*
	 * Note that there is currently (ha, see what I did there) a leakage
	 * path out of Cr50 into the CC lines.  On some systems, this can cause
	 * false Rdd detection when the TCPCs are turned off.  This may require
	 * a software workaround where RDD hardware must be powered down
	 * whenever the TCPCs are off, and can only be powered up for brief
	 * periods to do a quick check.  See b/38019839 and b/64582597.
	 */

	/* Configure to detect accessory connected */
	GWRITE(RDD, PROG_DEBUG_STATE_MAP, DETECT_DEBUG);

	/*
	 * Set the 0.4V comparator reference to 0.3V instead.  The voltage is
	 * marginal near 0.4V for example with VBUS at 4.75V and a SuzyQable See
	 * b/64847312.
	 */
	GWRITE_FIELD(RDD, REF_ADJ, LVL0P4V, 0x2);

	/*
	 * Enable interrupt for detecting CC.  This minimizes the time before
	 * we transition to cable-detected at boot, and will cause us to wake
	 * from deep sleep if a cable is plugged in.
	 */
	task_enable_irq(GC_IRQNUM_RDD0_INTR_DEBUG_STATE_DETECTED_INT);
	GWRITE_FIELD(RDD, INT_STATE, INTR_DEBUG_STATE_DETECTED, 1);
	GWRITE_FIELD(RDD, INT_ENABLE, INTR_DEBUG_STATE_DETECTED, 1);
}

static int command_rdd_keepalive(int argc, char **argv)
{
	if (argc == 1) {
		print_rdd_state();
		return EC_SUCCESS;
	}

	if (!parse_bool(argv[1], &force_detected))
		return EC_ERROR_PARAM1;

	if (force_detected) {
		/* Force Rdd detect */
		ccprintf("Forcing Rdd detect keepalive\n");
		hook_call_deferred(&rdd_connect_data, 0);
	} else {
		/* Go back to actual hardware state */
		ccprintf("Using actual Rdd state\n");
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rddkeepalive, command_rdd_keepalive,
			"[BOOLEAN]",
			"Get Rdd state or force keepalive");
