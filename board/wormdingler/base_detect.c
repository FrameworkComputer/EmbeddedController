/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wormdingler base detection code */

#include "adc.h"
#include "base_state.h"
#include "board.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "tablet_mode.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* Make sure POGO VBUS starts later then PP3300_HUB when power on  */
#define BASE_DETECT_EN_LATER_US (600 * MSEC)

/* Base detection and debouncing */
#define BASE_DETECT_EN_DEBOUNCE_US (350 * MSEC)
#define BASE_DETECT_DIS_DEBOUNCE_US (20 * MSEC)

/*
 * If the base status is unclear (i.e. not within expected ranges, read
 * the ADC value again every 500ms.
 */
#define BASE_DETECT_RETRY_US (500 * MSEC)

/*
 * Lid has 604K pull-up, base has 30.1K pull-down, so the
 * ADC value should be around 30.1/(604+30.1)*3300 = 156
 *
 * We add a significant margin on the maximum value, due to noise on the line,
 * especially when PWM is active. See b/64193554 for details.
 */
#define BASE_DETECT_MIN_MV 120
#define BASE_DETECT_MAX_MV 300

/* Minimum ADC value to indicate base is disconnected for sure */
#define BASE_DETECT_DISCONNECT_MIN_MV 1500

/*
 * Base EC pulses detection pin for 500 us to signal out of band USB wake (that
 * can be used to wake system from deep S3).
 */
#define BASE_DETECT_PULSE_MIN_US 400
#define BASE_DETECT_PULSE_MAX_US 650

static uint64_t base_detect_debounce_time;

static void base_detect_deferred(void);
DECLARE_DEFERRED(base_detect_deferred);

enum base_status {
	BASE_UNKNOWN = 0,
	BASE_DISCONNECTED = 1,
	BASE_CONNECTED = 2,
};

static enum base_status current_base_status;

/*
 * This function is called whenever there is a change in the base detect
 * status. Actions taken include:
 * 1. Change in power to base
 * 2. Indicate mode change to host.
 * 3. Indicate tablet mode to host. Current assumption is that if base is
 * disconnected then the system is in tablet mode, else if the base is
 * connected, then the system is not in tablet mode.
 */
static void base_detect_change(enum base_status status)
{
	int connected = (status == BASE_CONNECTED);

	if (current_base_status == status)
		return;

	gpio_set_level(GPIO_EN_BASE, connected);
	base_set_state(connected);
	current_base_status = status;
}

/* Measure detection pin pulse duration (used to wake AP from deep S3). */
static uint64_t pulse_start;
static uint32_t pulse_width;

static void print_base_detect_value(int v, int tmp_pulse_width)
{
	CPRINTS("%s = %d (pulse %d)", adc_channels[ADC_BASE_DET].name,
			v, tmp_pulse_width);
}

static void base_detect_deferred(void)
{
	uint64_t time_now = get_time().val;
	int v;
	uint32_t tmp_pulse_width = pulse_width;

	if (base_detect_debounce_time > time_now) {
		hook_call_deferred(&base_detect_deferred_data,
				   base_detect_debounce_time - time_now);
		return;
	}

	v = adc_read_channel(ADC_BASE_DET);
	if (v == ADC_READ_ERROR)
		return;

	print_base_detect_value(v, tmp_pulse_width);

	if (v >= BASE_DETECT_MIN_MV && v <= BASE_DETECT_MAX_MV) {
		if (current_base_status != BASE_CONNECTED) {
			base_detect_change(BASE_CONNECTED);
		} else if (tmp_pulse_width >= BASE_DETECT_PULSE_MIN_US &&
			   tmp_pulse_width <= BASE_DETECT_PULSE_MAX_US) {
			CPRINTS("Sending event to AP");
			host_set_single_event(EC_HOST_EVENT_KEY_PRESSED);
		}
		return;
	}

	if (v >= BASE_DETECT_DISCONNECT_MIN_MV) {
		base_detect_change(BASE_DISCONNECTED);
		return;
	}

	/* Unclear base status, schedule again in a while. */
	hook_call_deferred(&base_detect_deferred_data, BASE_DETECT_RETRY_US);
}

static inline int detect_pin_connected(enum gpio_signal det_pin)
{
	return gpio_get_level(det_pin) == 0;
}

void base_detect_interrupt(enum gpio_signal signal)
{
	uint64_t time_now = get_time().val;
	int debounce_us;

	if (detect_pin_connected(signal))
		debounce_us = BASE_DETECT_EN_DEBOUNCE_US;
	else
		debounce_us = BASE_DETECT_DIS_DEBOUNCE_US;

	if (base_detect_debounce_time <= time_now) {
		/*
		 * Detect and measure detection pin pulse, when base is
		 * connected. Only a single pulse is measured over a debounce
		 * period. If no pulse, or multiple pulses are detected,
		 * pulse_width is set to 0.
		 */
		if (current_base_status == BASE_CONNECTED &&
		    !detect_pin_connected(signal)) {
			pulse_start = time_now;
		} else {
			pulse_start = 0;
		}
		pulse_width = 0;

		hook_call_deferred(&base_detect_deferred_data, debounce_us);
	} else {
		if (current_base_status == BASE_CONNECTED &&
		    detect_pin_connected(signal) && !pulse_width &&
		    pulse_start) {
			/* First pulse within period. */
			pulse_width = time_now - pulse_start;
		} else {
			pulse_start = 0;
			pulse_width = 0;
		}
	}

	base_detect_debounce_time = time_now + debounce_us;
}

static void base_enable(void)
{
	/* Enable base detection interrupt. */
	base_detect_debounce_time = get_time().val;
	hook_call_deferred(&base_detect_deferred_data,
			BASE_DETECT_EN_LATER_US);
	gpio_enable_interrupt(GPIO_BASE_DET_L);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, base_enable, HOOK_PRIO_DEFAULT);

static void base_disable(void)
{
	/*
	 * Disable base detection interrupt and disable power to base.
	 * Set the state UNKNOWN so the next startup will initialize a
	 * correct state and notify AP.
	 */
	gpio_disable_interrupt(GPIO_BASE_DET_L);
	base_detect_change(BASE_UNKNOWN);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, base_disable, HOOK_PRIO_DEFAULT);

static void base_init(void)
{
	/*
	 * If we jumped to this image and chipset is already in S0, enable
	 * base.
	 */
	if (system_jumped_late() && chipset_in_state(CHIPSET_STATE_ON))
		base_enable();
}
DECLARE_HOOK(HOOK_INIT, base_init, HOOK_PRIO_DEFAULT+1);

void base_force_state(enum ec_set_base_state_cmd state)
{
	if (state == EC_SET_BASE_STATE_ATTACH) {
		gpio_disable_interrupt(GPIO_BASE_DET_L);
		base_detect_change(BASE_CONNECTED);
		CPRINTS("BD forced connected");
	} else if (state == EC_SET_BASE_STATE_DETACH) {
		gpio_disable_interrupt(GPIO_BASE_DET_L);
		base_detect_change(BASE_DISCONNECTED);
		CPRINTS("BD forced disconnected");
	} else {
		base_enable();
		CPRINTS("BD forced reset");
	}
}
