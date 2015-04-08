/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(USER_BUTTON, A, 0, GPIO_INT_BOTH, button_event)

/* Outputs */
GPIO(LED_BLUE,    B, 6, GPIO_OUT_LOW)
GPIO(LED_GREEN,   B, 7, GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(A, 0x0600, GPIO_ALT_USART, MODULE_UART, 0)
