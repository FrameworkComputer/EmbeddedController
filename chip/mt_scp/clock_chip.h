/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks, PLL and power settings */

#ifndef __CROS_EC_CLOCK_CHIP_H
#define __CROS_EC_CLOCK_CHIP_H

#include "common.h"
#include "registers.h"

/* Default ULPOSC clock speed in MHz */
#ifndef ULPOSC1_CLOCK_MHZ
#define ULPOSC1_CLOCK_MHZ 240
#endif
#ifndef ULPOSC2_CLOCK_MHZ
#define ULPOSC2_CLOCK_MHZ 330
#endif

void scp_enable_clock(void);

#endif /* __CROS_EC_CLOCK_CHIP_H */
