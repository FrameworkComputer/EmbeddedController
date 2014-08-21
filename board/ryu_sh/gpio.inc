/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
/*
 * TODO(gwendal): Follow Rambus work.
 * Combined accelerometer input. This will become an interrupt, once we have
 * support for it.
 */
GPIO(ACC_IRQ,              B, 12, GPIO_INPUT,  NULL)

/* Outputs */
GPIO(SH_EC_SIGNAL,         A, 7,  GPIO_OUT_LOW,  NULL)
GPIO(SH_IRQ_L,             A, 11, GPIO_OUT_LOW,  NULL)

/* Inputs */
GPIO(COMPASS_DRDY,         B, 11, GPIO_INPUT,  NULL)
GPIO(AP_IN_SUSPEND,        B, 15, GPIO_INPUT,  NULL)

#if 0
/* Alternate functions */
GPIO(UART_TX,              A,  2, GPIO_OUT_LOW,  NULL)
GPIO(UART_RX,              A,  3, GPIO_OUT_LOW,  NULL)
#endif

/* Needed to bypass flash write protection */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(SLAVE_I2C_SCL,        B,  6, GPIO_INPUT,    NULL)
GPIO(SLAVE_I2C_SDA,        B,  7, GPIO_INPUT,    NULL)
GPIO(MASTER_I2C_SCL,       B, 13, GPIO_INPUT,    NULL)
GPIO(MASTER_I2C_SDA,       B, 14, GPIO_INPUT,    NULL)

ALTERNATE(A, 0x000C, 1, MODULE_UART,   0) /* USART2: PA2/PA3 */
ALTERNATE(B, 0x00C0, 1, MODULE_I2C,    0) /* I2C SLAVE:PB6/7 */
ALTERNATE(B, 0x6000, 5, MODULE_I2C,    0) /* I2C MASTER:PB13/14 */
