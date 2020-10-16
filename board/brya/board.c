/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "registers.h"

static void fake_gpio_interrupt(enum gpio_signal signal)
{
}

#include "gpio_list.h"
