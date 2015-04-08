/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(POWER_BUTTON_L, E, 4, GPIO_INT_BOTH | GPIO_PULL_UP,   power_button_interrupt)
GPIO_INT(LID_OPEN,       E, 2, GPIO_INT_BOTH | GPIO_PULL_DOWN, lid_interrupt)

GPIO(H_LED0,         A, 0, GPIO_ODR_HIGH)
GPIO(H_LED1,         A, 1, GPIO_ODR_HIGH)
GPIO(H_LED2,         A, 2, GPIO_ODR_HIGH)
GPIO(H_LED3,         A, 3, GPIO_ODR_HIGH)
GPIO(H_LED4,         A, 4, GPIO_ODR_HIGH)
GPIO(H_LED5,         A, 5, GPIO_ODR_HIGH)
GPIO(H_LED6,         A, 6, GPIO_ODR_HIGH)
GPIO(L_LED0,         I, 0, GPIO_ODR_HIGH)
GPIO(L_LED1,         I, 1, GPIO_ODR_HIGH)
GPIO(L_LED2,         I, 2, GPIO_ODR_HIGH)
GPIO(L_LED3,         I, 3, GPIO_ODR_HIGH)
GPIO(L_LED4,         I, 4, GPIO_ODR_HIGH)
GPIO(L_LED5,         I, 5, GPIO_ODR_HIGH)
GPIO(L_LED6,         I, 6, GPIO_ODR_HIGH)
GPIO(BUSY_LED,       J, 0, GPIO_OUT_LOW)
GPIO(GOOD_LED,       J, 1, GPIO_OUT_HIGH)
GPIO(FAIL_LED,       J, 2, GPIO_OUT_LOW)
GPIO(PCH_PLTRST_L,   E, 3, GPIO_INPUT)
GPIO(PCH_SMI_L,      D, 3, GPIO_OUT_HIGH)
GPIO(PCH_SCI_L,      D, 4, GPIO_OUT_HIGH)
GPIO(GATE_A20_H,     B, 5, GPIO_OUT_HIGH)
GPIO(PCH_RCIN_L,     B, 6, GPIO_OUT_HIGH)
GPIO(LPC_CLKRUN_L,   H, 0, GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)

ALTERNATE(B, 0x03, 1, MODULE_UART, GPIO_PULL_UP) /* UART0 */
