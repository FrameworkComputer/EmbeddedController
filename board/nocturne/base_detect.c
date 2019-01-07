/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Nocturne base detection code.
 *
 * Nocturne has two analog detection pins with which it monitors to determine
 * the base status: the attach, and detach pins.
 *
 * When the voltages cross a certain threshold, after some debouncing, the base
 * is deemed connected.  Nocturne then applies the base power and monitors for
 * power faults from the eFuse as well as base disconnection.  Similarly, once
 * the voltages cross a different threshold, after some debouncing, the base is
 * deemed disconnected.  At this point, Nocturne disables the base power.
 */

#include "adc.h"
#include "base_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

#define DEFAULT_POLL_TIMEOUT_US (250 * MSEC)
#define DEBOUNCE_TIMEOUT_US (20 * MSEC)
#define POWER_FAULT_RETRY_INTERVAL_US (15 * MSEC)

/*
 * Number of times to attempt re-applying power within 1s when a fault occurs.
 */
#define POWER_FAULT_MAX_RETRIES 3

/* Thresholds for attach pin reading when power is not applied. */
#define ATTACH_MIN_MV 300
#define ATTACH_MAX_MV 900

/* Threshold for attach pin reading when power IS applied. */
#define PWREN_ATTACH_MIN_MV 2300

/* Threshold for detach pin reading. */
#define DETACH_MIN_MV 10


enum base_detect_state {
	BASE_DETACHED = 0,
	BASE_ATTACHED_DEBOUNCE,
	BASE_ATTACHED,
	BASE_DETACHED_DEBOUNCE,
	// Default for |forced_state|. Should be set only on |forced_state|.
	BASE_NO_FORCED_STATE,
};

static int debug;
static enum base_detect_state forced_state = BASE_NO_FORCED_STATE;
static enum base_detect_state state;


static void enable_base_interrupts(int enable)
{
	int (*fn)(enum gpio_signal) = enable ? gpio_enable_interrupt :
		gpio_disable_interrupt;

	/* This pin is present on boards newer than rev 0. */
	if (board_get_version() > 0)
		fn(GPIO_BASE_USB_FAULT_ODL);

	fn(GPIO_BASE_PWR_FAULT_ODL);
}

static void base_power_enable(int enable)
{
	/* Nothing to do if the state is the same. */
	if (gpio_get_level(GPIO_BASE_PWR_EN) == enable)
		return;

	if (enable) {
		/* Apply power to the base only if the AP is on or sleeping. */
		if (chipset_in_state(CHIPSET_STATE_ON |
				     CHIPSET_STATE_ANY_SUSPEND)) {
			gpio_set_level(GPIO_BASE_PWR_EN, 1);
			/* Allow time for the fault line to rise. */
			msleep(1);
			/* Monitor for base power faults. */
			enable_base_interrupts(1);
		}
	} else {
		/*
		 * Disable power fault interrupt.  It will read low when base
		 * power is removed.
		 */
		enable_base_interrupts(0);
		/* Now, remove power to the base. */
		gpio_set_level(GPIO_BASE_PWR_EN, 0);
	}

	CPRINTS("BP: %d", enable);
}

static void base_detect_changed(void)
{
	switch (state) {
	case BASE_DETACHED:
		base_set_state(0);
		base_power_enable(0);
		break;

	case BASE_ATTACHED:
		base_set_state(1);
		base_power_enable(1);
		break;

	default:
		return;
	};
}

static int base_seems_attached(int attach_pin_mv, int detach_pin_mv)
{
	/* We can't tell if we don't have good readings. */
	if (attach_pin_mv == ADC_READ_ERROR ||
	    detach_pin_mv == ADC_READ_ERROR)
		return 0;

	if (gpio_get_level(GPIO_BASE_PWR_EN))
		return (attach_pin_mv >= PWREN_ATTACH_MIN_MV) &&
			(detach_pin_mv >= DETACH_MIN_MV);
	else
		return (attach_pin_mv <= ATTACH_MAX_MV) &&
			(attach_pin_mv >= ATTACH_MIN_MV) &&
			(detach_pin_mv <= DETACH_MIN_MV);
}

static int base_seems_detached(int attach_pin_mv, int detach_pin_mv)
{
	/* We can't tell if we don't have good readings. */
	if (attach_pin_mv == ADC_READ_ERROR ||
	    detach_pin_mv == ADC_READ_ERROR)
		return 0;

	return (attach_pin_mv >= PWREN_ATTACH_MIN_MV) &&
		(detach_pin_mv <= DETACH_MIN_MV);
}

static void set_state(enum base_detect_state new_state)
{
	if (new_state != state) {
		CPRINTS("BD: st%d", new_state);
		state = new_state;
	}
}

static void base_detect_deferred(void);
DECLARE_DEFERRED(base_detect_deferred);
static void base_detect_deferred(void)
{
	int attach_reading;
	int detach_reading;
	int timeout = DEFAULT_POLL_TIMEOUT_US;

	if (forced_state != BASE_NO_FORCED_STATE) {
		if (state != forced_state) {
			CPRINTS("BD forced  %s",
				forced_state == BASE_ATTACHED ?
				"attached" : "detached");
			set_state(forced_state);
			base_detect_changed();
		}
		return;
	}

	attach_reading = adc_read_channel(ADC_BASE_ATTACH);
	detach_reading = adc_read_channel(ADC_BASE_DETACH);

	if (debug)
		CPRINTS("BD st%d: att: %dmV det: %dmV", state,
			attach_reading,
			detach_reading);

	switch (state) {
	case BASE_DETACHED:
		/* Check to see if a base may be attached. */
		if (base_seems_attached(attach_reading, detach_reading)) {
			timeout = DEBOUNCE_TIMEOUT_US;
			set_state(BASE_ATTACHED_DEBOUNCE);
		}
		break;

	case BASE_ATTACHED_DEBOUNCE:
		/* Check to see if it's still attached. */
		if (base_seems_attached(attach_reading, detach_reading)) {
			set_state(BASE_ATTACHED);
			base_detect_changed();
		} else if (base_seems_detached(attach_reading,
					       detach_reading)) {
			set_state(BASE_DETACHED);
		}
		break;

	case BASE_ATTACHED:
		/* Check to see if a base may be detached. */
		if (base_seems_detached(attach_reading, detach_reading)) {
			timeout = DEBOUNCE_TIMEOUT_US;
			set_state(BASE_DETACHED_DEBOUNCE);
		}
		break;

	case BASE_DETACHED_DEBOUNCE:
		/* Check to see if a base is still detached. */
		if (base_seems_detached(attach_reading, detach_reading)) {
			set_state(BASE_DETACHED);
			base_detect_changed();
		} else if (base_seems_attached(attach_reading,
					       detach_reading)) {
			set_state(BASE_ATTACHED);
		}
		break;
		/* TODO(b/74239259): do you want to add an interrupt? */

	default:
		break;
	};

	/* Check again in the appropriate time only if the AP is on. */
	if (chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND))
		hook_call_deferred(&base_detect_deferred_data, timeout);
};
DECLARE_HOOK(HOOK_INIT, base_detect_deferred, HOOK_PRIO_INIT_ADC + 1);

static void restart_state_machine(void)
{
	/*
	 * Since we do not poll in anything lower than S3, the base may or may
	 * not be connected, therefore intentionally set the state to detached
	 * such that we can detect and power on the base if necessary.
	 */
	set_state(BASE_DETACHED);
	hook_call_deferred(&base_detect_deferred_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, restart_state_machine, HOOK_PRIO_DEFAULT);

static void power_off_base(void)
{
	base_power_enable(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, power_off_base, HOOK_PRIO_DEFAULT);

static uint8_t base_power_on_attempts;
static void clear_base_power_on_attempts_deferred(void)
{
	base_power_on_attempts = 0;
}
DECLARE_DEFERRED(clear_base_power_on_attempts_deferred);

static void check_and_reapply_base_power_deferred(void)
{
	if (state != BASE_ATTACHED)
		return;

	if (base_power_on_attempts < POWER_FAULT_MAX_RETRIES) {
		CPRINTS("Reapply base pwr");
		base_power_enable(1);
		base_power_on_attempts++;

		hook_call_deferred(&clear_base_power_on_attempts_deferred_data,
				   SECOND);
	}

}
DECLARE_DEFERRED(check_and_reapply_base_power_deferred);

void base_pwr_fault_interrupt(enum gpio_signal s)
{
	/* Inverted because active low. */
	int pwr_fault_detected = !gpio_get_level(GPIO_BASE_PWR_FAULT_ODL);
	int usb_fault_detected = s == GPIO_BASE_USB_FAULT_ODL;

	if (pwr_fault_detected | usb_fault_detected) {
		/* Turn off base power. */
		CPRINTS("Base Pwr Flt! %s%s", pwr_fault_detected ? "p" : "-",
			usb_fault_detected ? "u" : "-");
		base_power_enable(0);

		/*
		 * Try and apply power in a bit if maybe it was just a temporary
		 * condition.
		 */
		hook_call_deferred(&check_and_reapply_base_power_deferred_data,
				   POWER_FAULT_RETRY_INTERVAL_US);
	}
}

static int command_basedetectdebug(int argc, char **argv)
{
	if ((argc > 1) && !parse_bool(argv[1], &debug))
		return EC_ERROR_PARAM1;

	CPRINTS("BD: %sst%d", forced_state != BASE_NO_FORCED_STATE ?
						  "forced " : "", state);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(basedebug, command_basedetectdebug, "[ena|dis]",
			"En/Disable base detection debug");


void base_force_state(int state)
{
	if (state == 1)
		forced_state = BASE_ATTACHED;
	else if (state == 0)
		forced_state = BASE_DETACHED;
	else
		forced_state = BASE_NO_FORCED_STATE;

	hook_call_deferred(&base_detect_deferred_data, 0);
}
