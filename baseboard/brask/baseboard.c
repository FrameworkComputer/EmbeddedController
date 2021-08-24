/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"

#include "gpio_signal.h"

/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);
