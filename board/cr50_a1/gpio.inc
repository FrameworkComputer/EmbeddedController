/* -*- mode:c -*-
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */

/* User Push buttons */
GPIO_INT(SW_N,   0, 0, GPIO_INT_RISING, button_event)
GPIO_INT(SW_S,   0, 1, GPIO_INT_RISING, button_event)
GPIO_INT(SW_W,   0, 2, GPIO_INT_RISING, button_event)
GPIO_INT(SW_E,   0, 3, GPIO_INT_RISING, button_event)

/* We can't trigger on both rising and falling edge, so attach each button
 * to two input GPIOs. */
GPIO_INT(SW_N_,   1, 0, GPIO_INT_FALLING, button_event)
GPIO_INT(SW_S_,   1, 1, GPIO_INT_FALLING, button_event)
GPIO_INT(SW_W_,   1, 2, GPIO_INT_FALLING, button_event)
GPIO_INT(SW_E_,   1, 3, GPIO_INT_FALLING, button_event)

/* User GPIO LEDs */
GPIO(LED_2,  0, 4, GPIO_OUT_LOW)
GPIO(LED_3,  0, 5, GPIO_OUT_LOW)
GPIO(LED_4,  0, 6, GPIO_OUT_LOW)
GPIO(LED_5,  0, 7, GPIO_OUT_LOW)
GPIO(LED_6,  0, 8, GPIO_OUT_LOW)
GPIO(LED_7,  0, 9, GPIO_OUT_LOW)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)

/* The Cr50 ARM core has no alternate functions, so we repurpose that
 * macro to describe the PINMUX setup. The args are
 *
 * 1. The ARM core GPIO or SoC peripheral function to connect
 * 2. The pinmux DIO pad to connect to
 * 3. <ignored>
 * 4. MODULE_GPIO, to prevent being called by gpio_config_module()
 * 5. flags to specify the direction if the GPIO isn't enough
 */

/* The serial port is one of the SoC peripheral functions */
ALTERNATE(FUNC(UART0_TX), DIO(A0), 0, MODULE_GPIO, DIO_OUTPUT)
ALTERNATE(FUNC(UART0_RX), DIO(A1), 0, MODULE_GPIO, DIO_INPUT)

/* Inputs */
ALTERNATE(SW_N,           DIO(M0), 0, MODULE_GPIO, 0)
ALTERNATE(SW_S,           DIO(M1), 0, MODULE_GPIO, 0)
ALTERNATE(SW_W,           DIO(M2), 0, MODULE_GPIO, 0)
ALTERNATE(SW_E,           DIO(M3), 0, MODULE_GPIO, 0)

/* Aliased Inputs, connected to the same pins */
ALTERNATE(SW_N_,           DIO(M0), 0, MODULE_GPIO, 0)
ALTERNATE(SW_S_,           DIO(M1), 0, MODULE_GPIO, 0)
ALTERNATE(SW_W_,           DIO(M2), 0, MODULE_GPIO, 0)
ALTERNATE(SW_E_,           DIO(M3), 0, MODULE_GPIO, 0)

/* Outputs - also mark as inputs so we can read back from the driven pin */
ALTERNATE(LED_2,          DIO(A9),    0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(LED_3,          DIO(A10),   0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(LED_4,          DIO(A11),   0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(LED_5,          DIO(A12),   0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(LED_6,          DIO(A13),   0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(LED_7,          DIO(A14),   0, MODULE_GPIO, DIO_INPUT)
