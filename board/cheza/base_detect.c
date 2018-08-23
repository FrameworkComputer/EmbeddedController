/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lux base without battery detection code */

#include "adc.h"
#include "adc_chip.h"
#include "board.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

/* Base detection and debouncing */
#define BASE_DETECT_DEBOUNCE_US (20 * MSEC)

/*
 * If the base status is unclear (i.e. not within expected ranges, read
 * the ADC value again every 500ms.
 */
#define BASE_DETECT_RETRY_US (500 * MSEC)

/*
 * When base is disconnected, and gets connected:
 * Lid has 1M pull-up, base has 200K pull-down, so the ADC
 * value should be around 200/(200+1000)*3300 = 550.
 *
 * Idle value should be ~3300: lid has 1M pull-up, and nothing else (i.e. ADC
 * maxing out at 2813).
 */
#define BASE_DISCONNECTED_CONNECT_MIN_MV 450
#define BASE_DISCONNECTED_CONNECT_MAX_MV 650

#define BASE_DISCONNECTED_MIN_MV 2800
#define BASE_DISCONNECTED_MAX_MV (ADC_MAX_VOLT+1)

/*
 * When base is connected, then gets disconnected:
 * Lid has 1M pull-up, lid has 10.0K pull-down, so the ADC
 * value should be around 10.0/(10.0+1000)*3300 = 33.
 *
 * Idle level when connected should be:
 * Lid has 10K pull-down, base has 5.1K pull-up, so the ADC value should be
 * around 10.0/(10.0+5.1)*3300 = 2185 (actual value is 2153 as there is still
 * a 1M pull-up on lid, and 200K pull-down on base).
 */
#define BASE_CONNECTED_DISCONNECT_MIN_MV 20
#define BASE_CONNECTED_DISCONNECT_MAX_MV 45

#define BASE_CONNECTED_MIN_MV 2050
#define BASE_CONNECTED_MAX_MV 2300

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
 * 1. Enable/disable pull-down on half-duplex UART line
 * 2. Enable/disable power to base.
 * 3. Indicate mode change to host.
 * 4. Indicate tablet mode to host. Current assumption is that if base is
 * disconnected then the system is in tablet mode, else if the base is
 * connected, then the system is not in tablet mode.
 */
static void base_detect_change(enum base_status status)
{
	int connected = (status == BASE_CONNECTED);

	if (current_base_status == status)
		return;

	current_base_status = status;

	/* Enable pull-down if connected. */
	gpio_set_level(GPIO_EN_CC_LID_BASE_PULLDN, !connected);

	/* We don't enable dual-battery support. Set the base power directly. */
	gpio_set_level(GPIO_EN_PPVAR_VAR_BASE, connected);

	tablet_set_mode(!connected);
}

static void print_base_detect_value(const char *str, int v)
{
	CPRINTS("Base %s. ADC: %d", str, v);
}

static void base_detect_deferred(void)
{
	uint64_t time_now = get_time().val;
	int v;

	if (base_detect_debounce_time > time_now) {
		hook_call_deferred(&base_detect_deferred_data,
				   base_detect_debounce_time - time_now);
		return;
	}

	v = adc_read_channel(ADC_BASE_DET);
	if (v == ADC_READ_ERROR)
		goto retry;

	if (current_base_status == BASE_CONNECTED) {
		if (v >= BASE_CONNECTED_DISCONNECT_MIN_MV &&
		    v <= BASE_CONNECTED_DISCONNECT_MAX_MV) {
			print_base_detect_value("disconnected", v);
			base_detect_change(BASE_DISCONNECTED);
			return;
		} else if (v >= BASE_CONNECTED_MIN_MV &&
			   v <= BASE_CONNECTED_MAX_MV) {
			/* Still connected. */
			return;
		}
	} else { /* Disconnected or unknown. */
		if (v >= BASE_DISCONNECTED_CONNECT_MIN_MV &&
		    v <= BASE_DISCONNECTED_CONNECT_MAX_MV) {
			print_base_detect_value("connected", v);
			base_detect_change(BASE_CONNECTED);
			return;
		} else if (v >= BASE_DISCONNECTED_MIN_MV &&
			   v <= BASE_DISCONNECTED_MAX_MV) {
			if (current_base_status == BASE_UNKNOWN) {
				print_base_detect_value("disconnected", v);
				base_detect_change(BASE_DISCONNECTED);
			}
			/* Still disconnected. */
			return;
		}
	}

retry:
	print_base_detect_value("status unclear", v);
	/* Unclear base status, schedule again in a while. */
	hook_call_deferred(&base_detect_deferred_data,
				   BASE_DETECT_RETRY_US);
}

void base_detect_interrupt(enum gpio_signal signal)
{
	uint64_t time_now = get_time().val;

	if (base_detect_debounce_time <= time_now)
		hook_call_deferred(&base_detect_deferred_data,
				   BASE_DETECT_DEBOUNCE_US);

	base_detect_debounce_time = time_now + BASE_DETECT_DEBOUNCE_US;
}

static void base_detect_enable(void)
{
	/* Enable base detection interrupt. */
	base_detect_debounce_time = get_time().val;
	hook_call_deferred(&base_detect_deferred_data, 0);
	gpio_enable_interrupt(GPIO_CC_LID_BASE_ADC);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, base_detect_enable, HOOK_PRIO_DEFAULT);

static void base_detect_disable(void)
{
	/* Disable base detection interrupt and disable power to base. */
	gpio_disable_interrupt(GPIO_CC_LID_BASE_ADC);
	base_detect_change(BASE_DISCONNECTED);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, base_detect_disable, HOOK_PRIO_DEFAULT);

static void base_init(void)
{
	/*
	 * Make sure base power and pull-down are off. This will reset the base
	 * if it is already connected.
	 */
	gpio_set_level(GPIO_EN_PPVAR_VAR_BASE, 0);
	gpio_set_level(GPIO_EN_CC_LID_BASE_PULLDN, 1);
}
DECLARE_HOOK(HOOK_INIT, base_init, HOOK_PRIO_DEFAULT+1);

void base_force_state(int state)
{
	if (state == 1) {
		gpio_disable_interrupt(GPIO_CC_LID_BASE_ADC);
		base_detect_change(BASE_CONNECTED);
		CPRINTS("BD forced connected");
	} else if (state == 0) {
		gpio_disable_interrupt(GPIO_CC_LID_BASE_ADC);
		base_detect_change(BASE_DISCONNECTED);
		CPRINTS("BD forced disconnected");
	} else {
		hook_call_deferred(&base_detect_deferred_data, 0);
		gpio_enable_interrupt(GPIO_CC_LID_BASE_ADC);
		CPRINTS("BD forced reset");
	}
}
