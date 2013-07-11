/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Backlight API for Chrome EC */

#ifndef __CROS_EC_BACKLIGHT_H
#define __CROS_EC_BACKLIGHT_H

#include "common.h"

/**
 * Interrupt handler for backlight.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void backlight_interrupt(enum gpio_signal signal);

#endif  /* __CROS_EC_BACKLIGHT_H */
