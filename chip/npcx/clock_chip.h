/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific clock module for Chrome EC */

#ifndef __CROS_EC_CLOCK_CHIP_H
#define __CROS_EC_CLOCK_CHIP_H

/* Default is 40MHz (target is 15MHz) */
#define OSC_CLK  15000000

/**
 * Return the current APB1 clock frequency in Hz.
 */
int clock_get_apb1_freq(void);

/**
 * Return the current APB2 clock frequency in Hz.
 */
int clock_get_apb2_freq(void);

/**
 * Set the CPU clock to maximum freq for better performance.
 */
void clock_turbo(void);

#endif /* __CROS_EC_CLOCK_CHIP_H */
