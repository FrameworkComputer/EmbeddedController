/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dedede family-specific configuration */

#include "common.h"
#include "gpio.h"

/*
 * Dedede does not use hibernate wake pins, but the super low power "Z-state"
 * instead in which the EC is powered off entirely.  Power will be restored to
 * the EC once one of the wake up events occurs.  These events are ACOK, lid
 * open, and a power button press.
 */
const enum gpio_signal hibernate_wake_pins[] = {};
const int hibernate_wake_pins_used;
