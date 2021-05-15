/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button API for Chrome EC */

#ifndef __CROS_EC_POWER_BUTTON_H
#define __CROS_EC_POWER_BUTTON_H

#include "common.h"

/**
 * Return non-zero if power button is pressed.
 *
 * Uses the debounced button state, not the raw signal from the GPIO.
 */
int power_button_is_pressed(void);

/**
 * Wait for the power button to be released
 *
 * @param timeout_us Timeout in microseconds, or -1 to wait forever
 * @return EC_SUCCESS if ok, or
 *         EC_ERROR_TIMEOUT if power button failed to release
 */
int power_button_wait_for_release(int timeout_us);

/**
 * Return non-zero if power button signal asserted at hardware input.
 *
 */
int power_button_signal_asserted(void);

/**
 * if true mean PB want to battery cutoff
 *
 */
int power_button_batt_cutoff(void);

/**
 * Interrupt handler for power button.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void power_button_interrupt(enum gpio_signal signal);

/**
 * For x86 systems, force-assert the power button signal to the PCH.
 */
void power_button_pch_press(void);

/**
 * For x86 systems, force-deassert the power button signal to the PCH.
 */
void power_button_pch_release(void);

/**
 * For x86 systems, force a pulse of the power button signal to the PCH.
 */
__override_proto void power_button_pch_pulse(void);

/**
 * For x86 systems, simulate a power button press.
 */
void power_button_simulate_press(void);

/**
 * Returns the time when DSW_PWROK was asserted. It should be customized
 * by each board. See CONFIG_DELAY_DSW_PWROK_TO_PWRBTN for details.
 *
 * @return time in usec when DSW_PWROK was asserted.
 */
int64_t get_time_dsw_pwrok(void);

/**
 * This must be defined when CONFIG_POWER_BUTTON_TO_PCH_CUSTOM is defined. This
 * allows a board to override the default behavior of
 * gpio_set_level(GPIO_PCH_PWRBTN_L, level).
 */
void board_pwrbtn_to_pch(int level);

/**
 * This allows an external function to set/get the power button level
 */
void power_button_set_simulated_state(int level);

#endif  /* __CROS_EC_POWER_BUTTON_H */
