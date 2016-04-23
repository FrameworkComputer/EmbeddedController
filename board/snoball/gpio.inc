/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* TCPC alert / interrupt inputs */
GPIO_INT(TCPC1_INT,	PIN(A, 0),	GPIO_INT_FALLING | GPIO_PULL_UP, tcpc_alert_event)
GPIO_INT(TCPC2_INT,	PIN(F, 7),	GPIO_INT_FALLING | GPIO_PULL_UP, tcpc_alert_event)

GPIO(OPTO_TX_1,		PIN(A, 2),	GPIO_OUT_LOW)
GPIO(OPTO_TX_2,		PIN(A, 3),	GPIO_OUT_LOW)

/* ADCs */
GPIO(VBIAS,		PIN(A, 4),	GPIO_ANALOG)
GPIO(VOUT_1,		PIN(A, 5),	GPIO_ANALOG)
GPIO(VOUT_2,		PIN(A, 6),	GPIO_ANALOG)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C1_SCL,		PIN(B, 6),	GPIO_INPUT | GPIO_PULL_UP)
GPIO(I2C1_SDA,		PIN(B, 7),	GPIO_INPUT | GPIO_PULL_UP)
GPIO(I2C2_SCL,		PIN(A, 11),	GPIO_INPUT | GPIO_PULL_UP) /* bitbang */
GPIO(I2C2_SDA,		PIN(A, 12),	GPIO_INPUT | GPIO_PULL_UP) /* bitbang */

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

/* Alternate functions */
#if 0
/* UART pins */
GPIO(EC_UART_TX,	PIN(A, 9),	GPIO_OUT_LOW)
GPIO(EC_UART_RX,	PIN(A, 10),	GPIO_INPUT)
#endif

/* PB6 / PB7 I2C1_SCL / I2C1_SDA */
ALTERNATE(PIN_MASK(B, 0x00C0), 1, MODULE_I2C,      GPIO_PULL_UP)

/* PA2 / PA9 / PA10: USART1 */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,     0)
