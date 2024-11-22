/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_host_command.h"
#include "diagnostics.h"
#include "driver/temp_sensor/f75303.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "led.h"
#include "led_common.h"
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
#define LED_ON 1
#define LED_OFF 0

uint32_t hw_diagnostics;
uint32_t diagnostic_tick;
uint32_t diagnostics_ctr;
uint32_t bios_code;

uint8_t run_diagnostics;

bool bios_diagnostic_is_completed;
bool device_diagnostic_is_completed;

/* Count the 1-second blink for diagnostic start or diagnostic hw finish */
int diagnostic_1sec_target;

void reset_diagnostics(void)
{
	/**
	 * Diagnostic always reset at G3/S5, so set the DIAGNOSTICS_NO_S0 to false.
	 * EC needs to detect the power glitch after PSU_OK is turned on, so set the
	 * DIAGNOSTICS_PSU_POK defaults to false.
	 */
	hw_diagnostics = BIT(DIAGNOSTICS_NO_S0) | BIT(DIAGNOSTICS_PSU_POK);

	run_diagnostics = 1;

	diagnostics_ctr = 0;
	bios_diagnostic_is_completed = 0;
	bios_code = 0;
	diagnostic_tick = 0;
	device_diagnostic_is_completed = 0;

	diagnostic_1sec_target = 0;
}

void cancel_diagnostics(void)
{
	/**
	 * We need to cancel the diagnostics if the user presses the power
	 * button to power off the system during the diagnostic.
	 */
	run_diagnostics = 0;
}

static void set_diagnostic_leds(bool led3_enable, bool led4_enable)
{
	if (led3_enable)
		led_set_color(LED_AMBER, EC_LED_ID_SECOND_POWER_LED);
	else
		led_set_color(LED_OFF, EC_LED_ID_SECOND_POWER_LED);
#if DT_NODE_EXISTS(DT_ALIAS(gpio_diagnostic_led4))
	gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_diagnostic_led4), led4_enable);
#endif
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
		bios_diagnostic_is_completed = true;
		CPRINTS("BIOS COMPLETE");
	}

	if (code == CODE_DDR_FAIL)
		set_diagnostic(DIAGNOSTICS_NO_DDR, true);
}

void set_device_complete(int done)
{
	device_diagnostic_is_completed = done;
}

#define YES_NO_BIT(bit) (((hw_diagnostics & BIT(bit)) == BIT(bit)) ? "Yes" : "No ")
#define YES_NO(b) (b ? "Yes" : "No ")

void print_diag_details(void)
{
	CPRINTS("  Power");
	CPRINTS("    PSU POK:                %s", YES_NO_BIT(DIAGNOSTICS_PSU_POK));
	CPRINTS("    12V OK:                 %s", YES_NO_BIT(DIAGNOSTICS_12V_OK));
	CPRINTS("    SLP S5:                 %s", YES_NO_BIT(DIAGNOSTICS_SLP_S5));
	CPRINTS("    SLP S4:                 %s", YES_NO_BIT(DIAGNOSTICS_SLP_S4));
	CPRINTS("    HW PGOOD VR:            %s", YES_NO_BIT(DIAGNOSTICS_HW_PGOOD_VR));
	CPRINTS("    HW power glitch:        %s", YES_NO_BIT(DIAGNOSTICS_POWER_GLITCH));
	CPRINTS("  Peripheral Device");
	CPRINTS("    No Fan:                 %s", YES_NO_BIT(DIAGNOSTICS_NOFAN));
	CPRINTS("    No Thermal Sensor:      %s", YES_NO_BIT(DIAGNOSTICS_THERMAL_SENSOR));
	CPRINTS("    No DDR:                 %s", YES_NO_BIT(DIAGNOSTICS_NO_DDR));
	CPRINTS("  Miscellaneous");
	CPRINTS("    No S0:                  %s", YES_NO_BIT(DIAGNOSTICS_NO_S0));
	CPRINTS("    Device (Diag) Complete: %s", YES_NO(device_diagnostic_is_completed));
	CPRINTS("    BIOS Complete:          %s", YES_NO(bios_diagnostic_is_completed));
}

static bool diagnostics_tick(void)
{
	/* Don't need to run the diagnostic */
	if (run_diagnostics == 0) {
		return false;
	}

	/* Diagnostic complete */
	if (diagnostics_ctr >= DIAGNOSTICS_MAX) {
		run_diagnostics = 0;
		set_diagnostic_leds(LED_OFF, LED_OFF);
		return false;
	}

	/* Wait 90 seconds for checks to complete
	 * We really want the system to start within 60 seconds,
	 * but a complete init with lots of RAM and peripherals can take longer
	 */
	if (++diagnostic_tick < 90 * TICK_PER_SEC)
		return false;

	/* Everything is ok after minimum 90 seconds of checking */
	if (bios_diagnostic_is_completed && hw_diagnostics == 0)
		return false;

	if (diagnostics_ctr == DIAGNOSTICS_START ||
		diagnostics_ctr == DIAGNOSTICS_HW_FINISH) {
		if (diagnostic_1sec_target == 0) {
			diagnostic_1sec_target = diagnostic_tick + TICK_PER_SEC;

			if (diagnostics_ctr == DIAGNOSTICS_START) {
				/* Print diagnostic info at first time */
				bios_code = port_80_last();
				CPRINTS("Boot issue: HW 0x%08x BIOS: 0x%04x",
				hw_diagnostics, bios_code);
				print_diag_details();
			}
		}
		if (diagnostic_tick < diagnostic_1sec_target) {
			/* Set led on 1 second for long blink */
			set_diagnostic_leds(LED_ON, LED_ON);
		} else {
			diagnostics_ctr++;
			diagnostic_1sec_target = 0;
			set_diagnostic_leds(LED_OFF, LED_OFF);
		}

		return true;
	}
	/* If something is wrong, display the diagnostic via the LED */
	if (diagnostic_tick & 0x01) {
		set_diagnostic_leds(LED_OFF, LED_OFF);
		diagnostics_ctr++;
	} else {
		if (diagnostics_ctr  < DIAGNOSTICS_HW_FINISH) {
			set_diagnostic_leds(LED_ON,
				(hw_diagnostics & (1 << diagnostics_ctr)) ? LED_ON : LED_OFF);
		} else if (diagnostics_ctr < DIAGNOSTICS_MAX) {
			set_diagnostic_leds(LED_ON,
				(bios_code & (1 << (diagnostics_ctr - DIAGNOSTICS_BIOS_BIT0)))
				? LED_ON : LED_OFF);
		}
	}

	return true;

}

static void diagnostics_check(void)
{
	if (device_diagnostic_is_completed)
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
	return bios_diagnostic_is_completed;
}

uint8_t is_device_complete(void)
{
	return device_diagnostic_is_completed;
}

static int cmd_diag(int argc, const char **argv)
{
	if (device_diagnostic_is_completed == 0) {
		CPRINTS("Diagnostics not completed.");
		return EC_SUCCESS;
	}
	if (hw_diagnostics == 0) {
		CPRINTS("No diag issues: 0x%08x BIOS: 0x%04x", hw_diagnostics, bios_code);
	} else {
		CPRINTS("Boot issue:     0x%08x BIOS: 0x%04x", hw_diagnostics, bios_code);
	}

	bios_code = port_80_last();
	print_diag_details();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(diag, cmd_diag,
			"[]",
			"print diagnostics");

static void led_tick(void)
{
	if (diagnostics_tick())
		led_auto_control(EC_LED_ID_SECOND_POWER_LED, 0);
	else
		led_auto_control(EC_LED_ID_SECOND_POWER_LED, 1);
}
/* Need make led set faster than HOOK_TICK in led.c */
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_PRE_DEFAULT);
