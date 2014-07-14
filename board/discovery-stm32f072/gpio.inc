/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO(USER_BUTTON, A, 0, GPIO_INT_FALLING, button_event)

/* Outputs */
GPIO(LED_U, C, 6, GPIO_OUT_LOW, NULL)
GPIO(LED_D, C, 7, GPIO_OUT_LOW, NULL)
GPIO(LED_L, C, 8, GPIO_OUT_LOW, NULL)
GPIO(LED_R, C, 9, GPIO_OUT_LOW, NULL)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(A, 0xC000, 1, MODULE_UART, 0) /* USART2: PA14/PA15 */
