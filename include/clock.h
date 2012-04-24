/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*  Clocks and power management settings */

#ifndef __CROS_EC_CLOCK_H
#define __CROS_EC_CLOCK_H

#include "common.h"

/* Set the CPU clocks and PLLs. */
int clock_init(void);

/* Return the current clock frequency in Hz. */
int clock_get_freq(void);

/* Enable or disable the PLL. */
int clock_enable_pll(int enable);

/* Wait <cycles> system clock cycles.  Simple busy waiting for before
 * clocks/timers are initialized. */
void clock_wait_cycles(uint32_t cycles);

#endif  /* __CROS_EC_CLOCK_H */
