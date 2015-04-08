/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Recovery signal from DOWN button */
GPIO(RECOVERY_L, D, 1, GPIO_PULL_UP)
GPIO(DEBUG_LED,  A, 7, GPIO_OUT_LOW)

/*
 * Signals which aren't implemented on BDS but we'll emulate anyway, to
 * make it more convenient to debug other code.
 */
UNIMPLEMENTED(WP)          /* Write protect input */
UNIMPLEMENTED(ENTERING_RW) /* EC entering RW code */

ALTERNATE(A, 0x03, 1, MODULE_UART,     0) /* UART0 */
ALTERNATE(G, 0x40, 3, MODULE_I2C,      0) /* I2C5 SCL */
ALTERNATE(G, 0x80, 3, GPIO_OPEN_DRAIN, 0) /* I2C5 SDA */
ALTERNATE(B, 0x03, 1, MODULE_UART,     0) /* UART1 */
