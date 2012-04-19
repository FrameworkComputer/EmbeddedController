/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button module for Chrome EC */

#ifndef __CROS_EC_POWER_BUTTON_H
#define __CROS_EC_POWER_BUTTON_H

#include "common.h"
#include "gpio.h"

/* Interrupt handler for the power button and lid switch.  Passed the signal
 * which triggered the interrupt. */
void power_button_interrupt(enum gpio_signal signal);

/* Power button task */
void power_button_task(void);

#endif  /* __CROS_EC_POWER_BUTTON_H */
