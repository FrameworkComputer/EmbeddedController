/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */
GPIO_INT(LID_OPEN,		PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH,	lid_interrupt)
GPIO_INT(WP_L,			PIN(A, 1), GPIO_INT_BOTH,			switch_interrupt)
GPIO_INT(POWER_BUTTON_L,	PIN(0, 1), GPIO_INT_BOTH,			power_button_interrupt)


GPIO(ENTERING_RW,		PIN(8, 0), GPIO_OUT_LOW)			/* EC Entering RW */