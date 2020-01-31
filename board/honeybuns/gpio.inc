/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
/* TODO(b/148624000): Change this pin assignment once schematics are final */
GPIO_INT(EC_WP_L,              PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)

/* Power sequencing interrupts */

/* Sensor Interrupts */

/* USB-C interrupts */

/* HDMI interrupts */

/* Volume button interrupts */

/* AP Signals */

/* USB and USBC Signals */

/* Misc Signals */
UNIMPLEMENTED(EC_ENTERING_RW)

/* UART */

