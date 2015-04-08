/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO(LED1,       PORT(15), 4, GPIO_ODR_LOW)
GPIO(LED2,       PORT(15), 5, GPIO_ODR_HIGH)
GPIO(LED3,       PORT(15), 6, GPIO_ODR_LOW)
GPIO(PCH_SMI_L,  PORT(4),  4, GPIO_ODR_HIGH) /* SMI output */
GPIO(PCH_WAKE_L, PORT(20), 0, GPIO_ODR_HIGH) /* PCH wake pin */

/* Switch S1 */
GPIO(S1,         PORT(6),  3, GPIO_INT_FALLING | GPIO_PULL_UP)

/* Shared SPI CS */
GPIO(SHD_CS0,    PORT(15), 0, GPIO_ODR_HIGH)

/*
 * Signals which aren't implemented on MEC1322 eval board but we'll
 * emulate anyway, to make it more convenient to debug other code.
 */
UNIMPLEMENTED(RECOVERY_L)  /* Recovery signal from DOWN button */
UNIMPLEMENTED(WP_L)        /* Write protect input */
UNIMPLEMENTED(ENTERING_RW) /* EC entering RW code */

ALTERNATE(PORT(16), 0x24,     1, MODULE_UART,    0) /* UART0 */
ALTERNATE(PORT(3),  (1 << 4), 3, MODULE_PWM_FAN, 0)
ALTERNATE(PORT(14), (1 << 0), 3, MODULE_PWM_FAN, 0)
/*
 * I2C0: External pull-up resistors on EVB are too weak. Let's
 * also enable internal pull-up here.
 */
ALTERNATE(PORT(1),  0x60, 2, MODULE_I2C,           GPIO_PULL_UP)
ALTERNATE(PORT(0),  0xfe, 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PORT(1),  0x03, 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PORT(3),  0x04, 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PORT(4),  0x0d, 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PORT(10), 0xd8, 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PORT(12), 0x60, 2, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PORT(14), 0x14, 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PORT(2),  0x20, 2, MODULE_LPC,           0)
ALTERNATE(PORT(12), 0x14, 1, MODULE_SPI,           0)
ALTERNATE(PORT(6),  0x10, 1, MODULE_SPI,           0)
