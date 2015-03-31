/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO(LED1,       PIN(154), GPIO_ODR_LOW)
GPIO(LED2,       PIN(155), GPIO_ODR_HIGH)
GPIO(LED3,       PIN(156), GPIO_ODR_LOW)
GPIO(PCH_SMI_L,  PIN(44),  GPIO_ODR_HIGH) /* SMI output */
GPIO(PCH_WAKE_L, PIN(200), GPIO_ODR_HIGH) /* PCH wake pin */

/* Switch S1 */
GPIO(S1,         PIN(63),  GPIO_INT_FALLING | GPIO_PULL_UP)

/* Shared SPI CS */
GPIO(SHD_CS0,    PIN(150), GPIO_ODR_HIGH)

/*
 * Signals which aren't implemented on MEC1322 eval board but we'll
 * emulate anyway, to make it more convenient to debug other code.
 */
UNIMPLEMENTED(RECOVERY_L)  /* Recovery signal from DOWN button */
UNIMPLEMENTED(WP_L)        /* Write protect input */
UNIMPLEMENTED(ENTERING_RW) /* EC entering RW code */

ALTERNATE(PIN_MASK(16, 0x24),     1, MODULE_UART,    0) /* UART0 */
ALTERNATE(PIN_MASK(3,  (1 << 4)), 3, MODULE_PWM_FAN, 0)
ALTERNATE(PIN_MASK(14, (1 << 0)), 3, MODULE_PWM_FAN, 0)
/*
 * I2C0: External pull-up resistors on EVB are too weak. Let's
 * also enable internal pull-up here.
 */
ALTERNATE(PIN_MASK(1,  0x60), 2, MODULE_I2C,           GPIO_PULL_UP)
ALTERNATE(PIN_MASK(0,  0xfe), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(1,  0x03), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(3,  0x04), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(4,  0x0d), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(10, 0xd8), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(12, 0x60), 2, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(14, 0x14), 3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(2,  0x20), 2, MODULE_LPC,           0)
ALTERNATE(PIN_MASK(12, 0x14), 1, MODULE_SPI,           0)
ALTERNATE(PIN_MASK(6,  0x10), 1, MODULE_SPI,           0)
