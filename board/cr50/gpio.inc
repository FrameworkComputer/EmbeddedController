/* -*- mode:c -*-
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */

/* User Push buttons */
GPIO_INT(SW_N,   PIN(0, 0), GPIO_INT_RISING, button_event)
GPIO_INT(SW_S,   PIN(0, 1), GPIO_INT_RISING, button_event)
GPIO_INT(SW_W,   PIN(0, 2), GPIO_INT_RISING, button_event)
GPIO_INT(SW_E,   PIN(0, 3), GPIO_INT_RISING, button_event)

/* We can't trigger on both rising and falling edge, so attach each button
 * to two input GPIOs. */
GPIO_INT(SW_N_,   PIN(1, 0), GPIO_INT_FALLING, button_event)
GPIO_INT(SW_S_,   PIN(1, 1), GPIO_INT_FALLING, button_event)
GPIO_INT(SW_W_,   PIN(1, 2), GPIO_INT_FALLING, button_event)
GPIO_INT(SW_E_,   PIN(1, 3), GPIO_INT_FALLING, button_event)

/* User GPIO LEDs */
GPIO(LED_2,  PIN(0, 4), GPIO_OUT_LOW)
GPIO(LED_3,  PIN(0, 5), GPIO_OUT_LOW)
GPIO(LED_4,  PIN(0, 6), GPIO_OUT_LOW)
GPIO(LED_5,  PIN(0, 7), GPIO_OUT_LOW)
GPIO(LED_6,  PIN(0, 8), GPIO_OUT_LOW)
GPIO(LED_7,  PIN(0, 9), GPIO_OUT_LOW)

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
ALTERNATE(PIN_MASK(FUNC(UART0_TX), DIO(A0)), 0, MODULE_GPIO, DIO_OUTPUT)
ALTERNATE(PIN_MASK(FUNC(UART0_RX), DIO(A1)), 0, MODULE_GPIO, DIO_INPUT)

/* Inputs */
ALTERNATE(PIN_MASK(SW_N, DIO(M0)), 0, MODULE_GPIO, 0)
ALTERNATE(PIN_MASK(SW_S, DIO(M1)), 0, MODULE_GPIO, 0)
ALTERNATE(PIN_MASK(SW_W, DIO(M2)), 0, MODULE_GPIO, 0)
ALTERNATE(PIN_MASK(SW_E, DIO(M3)), 0, MODULE_GPIO, 0)

/* Aliased Inputs, connected to the same pins */
ALTERNATE(PIN_MASK(SW_N_, DIO(M0)), 0, MODULE_GPIO, 0)
ALTERNATE(PIN_MASK(SW_S_, DIO(M1)), 0, MODULE_GPIO, 0)
ALTERNATE(PIN_MASK(SW_W_, DIO(M2)), 0, MODULE_GPIO, 0)
ALTERNATE(PIN_MASK(SW_E_, DIO(M3)), 0, MODULE_GPIO, 0)

/* Outputs - also mark as inputs so we can read back from the driven pin */
ALTERNATE(PIN_MASK(LED_2, DIO(A9)),    0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(PIN_MASK(LED_3, DIO(A10)),   0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(PIN_MASK(LED_4, DIO(A11)),   0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(PIN_MASK(LED_5, DIO(A12)),   0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(PIN_MASK(LED_6, DIO(A13)),   0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(PIN_MASK(LED_7, DIO(A14)),   0, MODULE_GPIO, DIO_INPUT)

/* SPS IOs This is not needed for the FPGA, it is hardwired
 * TODO_FPGA (might have to be modified for a new version)
ALTERNATE(PIN_MASK(FUNC(SPS0_SPICLK),  DIO(A2)), 0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(PIN_MASK(FUNC(SPS0_SPIMOSI), DIO(A3)), 0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(PIN_MASK(FUNC(SPS0_SPICSB),  DIO(A4)), 0, MODULE_GPIO, DIO_INPUT)
ALTERNATE(PIN_MASK(FUNC(SPS0_SPIMISO), DIO(A5)), 0, MODULE_GPIO, DIO_OUTPUT)
*/
