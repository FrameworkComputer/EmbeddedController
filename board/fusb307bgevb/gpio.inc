/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(USB_C0_PD_INT_ODL, PIN(A, 8), GPIO_INT_FALLING | GPIO_PULL_UP, tcpc_alert_event)
GPIO_INT(USER_BUTTON_ENTER, PIN(A, 0), GPIO_INT_FALLING, button_enter_event)
GPIO_INT(USER_BUTTON_REFRESH, PIN(A, 1), GPIO_INT_FALLING, button_refresh_event)
GPIO_INT(USER_BUTTON_DOWN, PIN(A, 2), GPIO_INT_FALLING, button_down_event)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C2_SCL,	PIN(B, 10),  GPIO_INPUT)
GPIO(I2C2_SDA,	PIN(B, 11),  GPIO_INPUT)

ALTERNATE(PIN_MASK(B, 0x0C00), 1, MODULE_I2C, GPIO_ODR_HIGH ) /* I2C MASTER: PB10/11 */

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_USART, 0) /* USART1: PA09/PA10 */
ALTERNATE(PIN_MASK(A, 0xC000), 1, MODULE_UART, 0)  /* USART2: PA14/PA15 */
ALTERNATE(PIN_MASK(C, 0x0C00), 0, MODULE_USART, 0) /* USART4: PC10/PC11 */
