/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(EC_LID_OPEN,          PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(EC_WP_L,              PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)

/* Power sequencing interrupts */

/* Sensor Interrupts */

/* USB-C interrupts */

/* HDMI interrupts */

/* Volume button interrupts */

/* AP Signals */
GPIO(EC_ENTERING_RW,           PIN(E, 3), GPIO_OUT_LOW)

/* USB and USBC Signals */

/* Misc Signals */

/* UART */
ALTERNATE(PIN_MASK(6, BIT(5) | BIT(4)), 0, MODULE_UART, 0)                           /* UART from EC to Servo */

