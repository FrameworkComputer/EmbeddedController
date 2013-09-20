/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Haswell chipset interface */

#ifndef __CROS_EC_CHIPSET_HASWELL_H
#define __CROS_EC_CHIPSET_HASWELL_H

/**
 * Interrupt handler for Haswell-specific GPIOs.
 */
#ifdef CONFIG_CHIPSET_HASWELL
void haswell_interrupt(enum gpio_signal signal);
#else
#define haswell_interrupt NULL
#endif

#endif
