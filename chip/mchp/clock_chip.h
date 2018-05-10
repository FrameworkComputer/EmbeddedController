/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Microchip MEC1701 specific module for Chrome EC */

#ifndef __CROS_EC_CLOCK_CHIP_H
#define __CROS_EC_CLOCK_CHIP_H

#include <stdint.h>

void htimer_init(void);
void system_set_htimer_alarm(uint32_t seconds,
		uint32_t microseconds);

#endif /* __CROS_EC_I2C_CLOCK_H */
