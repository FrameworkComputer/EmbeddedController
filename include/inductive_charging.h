/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inductive charging control */

#include "gpio.h"

#ifndef __INDUCTIVE_CHARGING_H
#define __INDUCTIVE_CHARGING_H

/*
 * Interrupt handler for inductive charging signal.
 *
 * @param signal  Signal which triggered the interrupt.
 */
void inductive_charging_interrupt(enum gpio_signal);

#endif
