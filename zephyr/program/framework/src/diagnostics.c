/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "battery.h"
#include "board_host_command.h"
#include "charge_state.h"
#include "chipset.h"
#include "diagnostics.h"
#include "driver/temp_sensor/f75303.h"
#include "ec_commands.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "led.h"
#include "power.h"
#include "port80.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/**
 * CROS_EC_HOOK_TICK_INTERVAL = 200 ms
 * TICK_PER_SEC = 1000 ms / 200 ms = 5
 */
#define TICK_PER_SEC	5

uint32_t hw_diagnostics;
uint32_t diagnostic_tick;
uint32_t diagnostics_ctr;
uint32_t bios_code;

uint8_t bios_complete;
uint8_t fan_seen; /* TODO: Unused so far */
uint8_t run_diagnostics;
uint8_t device_complete;

int standalone_mode;

void set_standalone_mode(int enable)
{
	CPRINTS("set standalone = %d", enable);
	standalone_mode = enable;
}

int get_standalone_mode(void)
{
	return standalone_mode;
}


void reset_diagnostics(void)
{
	/* Diagnostic always reset at G3/S5 */
#ifdef CONFIG_BOARD_LOTUS
	hw_diagnostics =
		BIT(DIAGNOSTICS_NO_RIGHT_FAN) | BIT(DIAGNOSTICS_NO_LEFT_FAN) |
		BIT(DIAGNOSTICS_NO_S0) | BIT(DIAGNOSTICS_HW_NO_BATTERY);
#else
	hw_diagnostics = BIT(DIAGNOSTICS_NO_S0) | BIT(DIAGNOSTICS_HW_NO_BATTERY);
#endif

	run_diagnostics = 1;

	diagnostics_ctr = 0;
	bios_complete = 0;
	bios_code = 0;
	diagnostic_tick = 0;
	fan_seen = 0;
	device_complete = 0;
}

void cancel_diagnostics(void)
{
	/**
	 * We need to cancel the diagnostics if the user presses the power
	 * button to power off the system during the diagnostic.
	 */
	run_diagnostics = 0;
}

static void set_diagnostic_leds(int color)
{
	led_set_color(color, EC_LED_ID_BATTERY_LED);
	board_led_apply_color();
}

void set_diagnostic(enum diagnostics_device_idx idx, bool error)
{
	if (error)
		hw_diagnostics |= 1 << idx;
	else
		hw_diagnostics &= ~(1 << idx);
}

void set_bios_diagnostic(uint8_t code)
{
	if (code == CODE_PORT80_COMPLETE) {
		bios_complete = true;
		CPRINTS("BIOS COMPLETE");
	}

	if (code == CODE_DDR_FAIL)
		set_diagnostic(DIAGNOSTICS_NO_DDR, true);
	if (code == CODE_NO_EDP && !get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_NO_EDP, true);
}

void set_device_complete(int done)
{
	device_complete = done;
}

bool diagnostics_tick(void)
{
	/* Don't need to run the diagnostic */
	if (run_diagnostics == 0)
		return false;

	/* Diagnostic complete */
	if (diagnostics_ctr >= DIAGNOSTICS_MAX) {
		run_diagnostics = 0;
		return false;
	}

	/* Wait 15 seconds for checks to complete */
	if (++diagnostic_tick < 60 * TICK_PER_SEC)
		return false;

	/* Everything is ok after minimum 15 seconds of checking */
	if (bios_complete && hw_diagnostics == 0)
		return false;
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_right_side), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_left_side), 1);

	/* If something is wrong, display the diagnostic via the LED */
	if (diagnostic_tick & 0x01)
		set_diagnostic_leds(LED_OFF);
	else {
		if (diagnostics_ctr == DIAGNOSTICS_START) {
			set_diagnostic_leds(LED_WHITE);
			bios_code = port_80_last();
			CPRINTS("Boot issue: HW 0x%08x BIOS: 0x%04x", hw_diagnostics, bios_code);
		} else if (diagnostics_ctr  < DIAGNOSTICS_HW_FINISH) {
			set_diagnostic_leds((hw_diagnostics & (1 << diagnostics_ctr))
				? LED_RED : LED_GREEN);
		} else if (diagnostics_ctr == DIAGNOSTICS_HW_FINISH)
			set_diagnostic_leds(LED_AMBER);
		else if (diagnostics_ctr < DIAGNOSTICS_MAX) {
			set_diagnostic_leds((bios_code &
				(1 << (diagnostics_ctr - DIAGNOSTICS_BIOS_BIT0)))
				? LED_BLUE : LED_GREEN);
		}
		diagnostics_ctr++;
	}

	return true;

}

static void diagnostics_check(void)
{
	if (device_complete)
		return;
	/* Clear the DIAGNOSTIC_NO_S0 flag if chipset is resume */
	set_diagnostic(DIAGNOSTICS_NO_S0, false);

	/* Call deferred hook to check the device */
	project_diagnostics();

}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, diagnostics_check, HOOK_PRIO_DEFAULT);

uint32_t get_hw_diagnostic(void)
{
	return hw_diagnostics;
}

uint8_t is_bios_complete(void)
{
	return bios_complete;
}

uint8_t is_device_complete(void)
{
	return device_complete;
}
