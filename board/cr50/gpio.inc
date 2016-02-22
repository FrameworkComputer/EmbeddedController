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

/*
 * If we are included by generic GPIO code that doesn't know about the PINMUX
 * macro we need to provide an empty definition so that the invocations don't
 * interfere with other GPIO processing.
 */
#ifndef PINMUX
#define PINMUX(...)
#endif

/* The serial port is one of the SoC peripheral functions */
PINMUX(FUNC(UART0_TX), A0, DIO_OUTPUT)
PINMUX(FUNC(UART0_RX), A1, DIO_INPUT)

/* Inputs */
PINMUX(GPIO(SW_N), M0, 0)
PINMUX(GPIO(SW_S), M1, 0)
PINMUX(GPIO(SW_W), M2, 0)
PINMUX(GPIO(SW_E), M3, 0)

/* Aliased Inputs, connected to the same pins */
PINMUX(GPIO(SW_N_), M0, 0)
PINMUX(GPIO(SW_S_), M1, 0)
PINMUX(GPIO(SW_W_), M2, 0)
PINMUX(GPIO(SW_E_), M3, 0)

/* Outputs - also mark as inputs so we can read back from the driven pin */
PINMUX(GPIO(LED_2), A9,  DIO_INPUT)
PINMUX(GPIO(LED_4), A11, DIO_INPUT)
PINMUX(GPIO(LED_6), A13, DIO_INPUT)
PINMUX(GPIO(LED_7), A14, DIO_INPUT)

#undef PINMUX
