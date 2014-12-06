/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific clock module for Chrome EC */

#ifndef CLOCK_CHIP_H_
#define CLOCK_CHIP_H_

/**
 * Return the current APB1 clock frequency in Hz.
 */
int clock_get_apb1_freq(void);

/**
 * Return the current APB2 clock frequency in Hz.
 */
int clock_get_apb2_freq(void);

#endif /* CLOCK_CHIP_H_ */
