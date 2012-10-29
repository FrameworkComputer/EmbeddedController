/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button module for Chrome EC */

#ifndef __CROS_EC_POWER_BUTTON_H
#define __CROS_EC_POWER_BUTTON_H

#include "common.h"
#include "gpio.h"

/**
 * Interrupt handler for the power button and lid switch.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void power_button_interrupt(enum gpio_signal signal);

/**
 * Power button task.
 */
void power_button_task(void);

/**
 * Return non-zero if AC power is present.
 */
int power_ac_present(void);

/**
 * Return non-zero if lid is open.
 *
 * Uses the debounced lid state, not the raw signal from the GPIO.
 */
int power_lid_open_debounced(void);

/**
 * Return non-zero if write protect signal is asserted.
 */
int write_protect_asserted(void);

#endif  /* __CROS_EC_POWER_BUTTON_H */
