/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about. */

/* Wake Source interrupts */
GPIO_INT(LID_OPEN,		PIN(0, 2), GPIO_INT_BOTH,	switch_interrupt)
GPIO_INT(EC_WP_L,		PIN(A, 1), GPIO_INT_BOTH,	switch_interrupt)

/* Power sequencing interrupts */

/* Sensor Interrupts */

/* USB-C interrupts */

/* HDMI interrupts */

/* Volume button interrupts */

/* AP Signals */
UNIMPLEMENTED(ENTERING_RW) /* GPIO_ENTERING_RW */

/* USB and USBC Signals */

/* Misc Signals */

/* UART */

/* I2C pins - these will be reconfigured for alternate function below */

/* Power Switch Logic (PSL) inputs */
