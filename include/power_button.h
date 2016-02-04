/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
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
void power_button_pch_pulse(void);

#endif  /* __CROS_EC_POWER_BUTTON_H */
