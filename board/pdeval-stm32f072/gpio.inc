/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(USER_BUTTON, PIN(A, 0), GPIO_INT_FALLING, button_event)
GPIO_INT(PD_MCU_INT,  PIN(A, 1), GPIO_INT_FALLING, alert_event)

/* Outputs */
GPIO(LED_U, PIN(C, 6), GPIO_OUT_LOW)
GPIO(LED_D, PIN(C, 7), GPIO_OUT_LOW)
GPIO(LED_L, PIN(C, 8), GPIO_OUT_LOW)
GPIO(LED_R, PIN(C, 9), GPIO_OUT_LOW)
GPIO(USB_C0_DVDDIO, PIN(C, 14), GPIO_OUT_HIGH)
GPIO(USB_C0_AVDD33, PIN(C, 15), GPIO_OUT_HIGH)
GPIO(VBUS_PMIC_CTRL, PIN(A, 4), GPIO_OUT_LOW)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C0_SCL, PIN(B, 6),  GPIO_INPUT)
GPIO(I2C0_SDA, PIN(B, 7),  GPIO_INPUT)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_USART, 0) /* USART1: PA09/PA10 */
ALTERNATE(PIN_MASK(A, 0xC000), 1, MODULE_UART,  0) /* USART2: PA14/PA15 */
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,   0) /* I2C MASTER:PB6/7 */
