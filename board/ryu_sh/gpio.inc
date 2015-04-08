/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(AP_IN_SUSPEND,        E, 9,  GPIO_INT_BOTH,                   power_signal_interrupt)
/*
 * TODO(gwendal): Follow Rambus work.
 * Combined accelerometer input. This will become an interrupt, once we have
 * support for it.
 */
GPIO(ACC_IRQ,              B, 12, GPIO_INPUT)

/* Outputs */
GPIO(SH_EC_SIGNAL,         A, 7,  GPIO_OUT_LOW)
GPIO(SH_IRQ_L,             A, 11, GPIO_OUT_LOW)

/* Inputs */
GPIO(LID_CLOSED,           A,  2, GPIO_INPUT)
GPIO(BASE_PRESENT,         A,  3, GPIO_INPUT)
GPIO(COMPASS_DRDY,         B, 11, GPIO_INPUT)

#if 0
/* Alternate functions */
GPIO(UART_TX,              A,  9, GPIO_OUT_LOW)
GPIO(UART_RX,              A, 10, GPIO_OUT_LOW)
#endif

/* Needed to bypass flash write protection */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(SLAVE_I2C_SCL,        B,  6, GPIO_INPUT)
GPIO(SLAVE_I2C_SDA,        B,  7, GPIO_INPUT)
GPIO(MASTER_I2C_SCL,       B, 13, GPIO_INPUT)
GPIO(MASTER_I2C_SDA,       B, 14, GPIO_INPUT)

ALTERNATE(A, 0x0600, 1, MODULE_UART,   0) /* USART1: PA9/PA10 */
ALTERNATE(B, 0x00C0, 1, MODULE_I2C,    0) /* I2C SLAVE:PB6/7 */
ALTERNATE(B, 0x6000, 5, MODULE_I2C,    0) /* I2C MASTER:PB13/14 */
