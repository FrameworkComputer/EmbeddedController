/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Outputs */
GPIO(LED1,                     PIN(A, 5),  GPIO_OUT_LOW) /* Green */

/* Misc Signals */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(EC_WP_L)

/* LPUART1: A2/A3 */
ALTERNATE(PIN_MASK(A, 0x000C), GPIO_ALT_LPUART,  MODULE_UART, GPIO_PULL_UP)
/* MCO A8 */
ALTERNATE(PIN_MASK(A, 0x0100), 0,  MODULE_CLOCK, 0)
