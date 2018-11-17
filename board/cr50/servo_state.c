/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Servo state machine.
 */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "uart_bitbang.h"
#include "uartn.h"
#include "usb_i2c.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

static enum device_state state = DEVICE_STATE_INIT;

void print_servo_state(void)
{
	ccprintf("Servo:   %s\n", device_state_name(state));
}

int servo_is_connected(void)
{
	/*
	 * If we're connected, we definitely know we are.  If we're debouncing,
	 * then we were connected and might still be.  If we haven't
	 * initialized yet, we'd bettter assume we're connected until we prove
	 * otherwise.  In any of these cases, it's not safe to allow ports to
	 * be connected because that would block detecting servo.
	 */
	return (state == DEVICE_STATE_CONNECTED ||
		state == DEVICE_STATE_DEBOUNCING ||
		state == DEVICE_STATE_INIT ||
		state == DEVICE_STATE_INIT_DEBOUNCING);
}

/**
 * Set the servo state.
 *
 * Done as a function to make it easier to debug state transitions.  Note that
 * this ONLY sets the state (and possibly prints debug info), and doesn't do
 * all the additional transition work that servo_disconnect(), etc. do.
 *
 * @param new_state	State to set.
 */
static void set_state(enum device_state new_state)
{
#ifdef CR50_DEBUG_SERVO_STATE
	/* Print all state transitions.  May spam the console. */
	if (state != new_state)
		CPRINTS("Servo %s -> %s",
			device_state_name(state), device_state_name(new_state));
#endif
	state = new_state;
}

/**
 * Check if we can tell servo is connected.
 *
 * @return 1 if we can tell if servo is connected, 0 if we can't tell.
 */
static int servo_detectable(void)
{
	/*
	 * If we are driving the UART transmit line to the EC, then we can't
	 * check to see if servo is also doing so.
	 *
	 * We also need to check if we're bit-banging the EC UART, because in
	 * that case, the UART transmit line is directly controlled as a GPIO
	 * and can be high even if UART TX is disconnected.
	 */
	return !(uart_tx_is_connected(UART_EC) || uart_bitbang_is_enabled());
}

/**
 * Handle servo being disconnected
 */
static void servo_disconnect(void)
{
	if (!servo_is_connected())
		return;

	CPRINTS("Servo disconnect");
	set_state(DEVICE_STATE_DISCONNECTED);
	ccd_update_state();
}

/**
 * Handle servo being connected.
 *
 * This can be called directly by servo_detect(), or as a deferred function.
 * Both are in the HOOK task, so can't preempt each other.
 */
static void servo_connect(void)
{
	/*
	 * If we were debouncing disconnect, go back to connected.  We never
	 * finished disconnecting, so nothing else is necessary.
	 */
	if (state == DEVICE_STATE_DEBOUNCING)
		set_state(DEVICE_STATE_CONNECTED);

	/* If we're already connected, nothing else needs to be done */
	if (state == DEVICE_STATE_CONNECTED)
		return;

	/*
	 * If we're still here, this is a real transition from a disconnected
	 * state, so we need to configure ports.
	 */
	CPRINTS("Servo connect");
	set_state(DEVICE_STATE_CONNECTED);
	ccd_update_state();
}
DECLARE_DEFERRED(servo_connect);

void servo_ignore(int enable)
{
	if (enable) {
		/*
		 * Set servo state to IGNORE, so servo presence wont prevent
		 * cr50 from enabling EC and AP uart.
		 */
		set_state(DEVICE_STATE_IGNORED);
		ccd_update_state();
	} else {
		/*
		 * To be on the safe side 'connect' servo when we stop ignoring
		 * the servo state. If servo is disconnected, then cr50 will
		 * notice within 1 second and reenable ccd.
		 */
		servo_connect();
	}
}

/**
 * Servo state machine
 */
static void servo_detect(void)
{
	/* Disable interrupts if we had them on for debouncing */
	gpio_disable_interrupt(GPIO_DETECT_SERVO);

	if (state == DEVICE_STATE_IGNORED)
		return;

	/* If we're driving EC UART TX, we can't detect servo */
	if (!servo_detectable()) {
		/* We're driving one port; might as well drive them all */
		servo_disconnect();

		set_state(DEVICE_STATE_UNDETECTABLE);
		return;
	}

	/* Handle detecting servo */
	if (gpio_get_level(GPIO_DETECT_SERVO)) {
		servo_connect();
		return;
	}
	/*
	 * If servo has become detectable but wasn't detected above, assume
	 * it's disconnected.
	 *
	 * We know we were driving EC UART TX, so we want to give priority to
	 * our ability to drive it again.  If we went to the debouncing state
	 * here, then we'd need to wait a second before we could drive it.
	 *
	 * This is similar to how if servo was driving EC UART TX, we go to the
	 * debouncing state below, because we want to give priority to servo
	 * being able to drive it again.
	 */
	if (state == DEVICE_STATE_UNDETECTABLE) {
		set_state(DEVICE_STATE_DISCONNECTED);
		return;
	}
	/*
	 * Make sure the interrupt is enabled. We will need to detect the on
	 * transition if we enter the off or debouncing state
	 */
	gpio_enable_interrupt(GPIO_DETECT_SERVO);

	/* Servo wasn't detected.  If we're already disconnected, done. */
	if (state == DEVICE_STATE_DISCONNECTED)
		return;

	/* If we were debouncing, we're now sure we're disconnected */
	if (state == DEVICE_STATE_DEBOUNCING ||
	    state == DEVICE_STATE_INIT_DEBOUNCING) {
		servo_disconnect();
		return;
	}

	/*
	 * Otherwise, we were connected or initializing, and we're not sure if
	 * we're now disconnected or just sending a 0-bit.  So start
	 * debouncing.
	 *
	 * During debouncing, servo_is_connected() will still return true, so
	 * that if both CCD and servo cables are connected, we won't start
	 * driving EC UART TX and become unable to determine the servo connect
	 * state.
	 */
	if (state == DEVICE_STATE_INIT)
		set_state(DEVICE_STATE_INIT_DEBOUNCING);
	else
		set_state(DEVICE_STATE_DEBOUNCING);
}
/*
 * Do this at slightly elevated priority so it runs before rdd_check_pin() and
 * ec_detect().  This increases the odds that we'll detect servo before
 * detecting the EC.  If ec_detect() ran first, it could turn on TX to the EC
 * UART before we had a chance to detect servo.  This is still a little bit of
 * a race condition.
 */
DECLARE_HOOK(HOOK_SECOND, servo_detect, HOOK_PRIO_DEFAULT - 1);

/**
 * Interrupt handler for servo detect asserted
 */
void servo_detect_asserted(enum gpio_signal signal)
{
	gpio_disable_interrupt(GPIO_DETECT_SERVO);

	/*
	 * If this interrupt is because servo is actually detectable (vs. we're
	 * driving the detect pin now), queue a transition back to connected.
	 */
	if (servo_detectable())
		hook_call_deferred(&servo_connect_data, 0);
}
