/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the board GPIOs that we care about. */

#include "base_gpio.inc"

GPIO_INT(6AXIS_INT_L,		PIN(A, 0), GPIO_INT_FALLING | GPIO_PULL_UP, motion_interrupt)		/* 6 Axis IMU */
GPIO_INT(TABLET_MODE,		PIN(C, 1), GPIO_INT_BOTH, gmr_tablet_switch_isr)			/* 360 Tablet Mode */
