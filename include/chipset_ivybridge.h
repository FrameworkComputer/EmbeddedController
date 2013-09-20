/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Ivy bridge chipset interface */

#ifndef __CROS_EC_CHIPSET_IVYBRIDGE_H
#define __CROS_EC_CHIPSET_IVYBRIDGE_H

/**
 * Interrupt handler for Ivy Bridge-specific GPIOs.
 */
#ifdef CONFIG_CHIPSET_IVYBRIDGE
void ivybridge_interrupt(enum gpio_signal signal);
#else
#define ivybridge_interrupt NULL
#endif

#endif
