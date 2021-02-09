/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include "power.h"
#include "switch.h"
#include "throttle_ap.h"

#include "gpio_list.h" /* Must come after other header files. */

/*
 * remove when we enable CONFIG_POWER_BUTTON
 */

void power_button_interrupt(enum gpio_signal signal)
{
}

/*
 * remove when we enable CONFIG_VOLUME_BUTTONS
 */

void button_interrupt(enum gpio_signal signal)
{
}
