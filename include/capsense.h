/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CAPSENSE_H
#define __CROS_EC_CAPSENSE_H

#include "common.h"
#include "gpio.h"

void capsense_interrupt(enum gpio_signal signal);

#endif  /* __CROS_EC_CAPSENSE_H */
