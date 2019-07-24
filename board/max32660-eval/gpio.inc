/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/*
 * Signals which aren't implemented on Board but we'll emulate anyway, to
 * make it more convenient to debug other code.
 */
UNIMPLEMENTED(WP)	  /* Write protect input */
UNIMPLEMENTED(ENTERING_RW) /* EC entering RW code */

ALTERNATE(PIN_MASK(0, 0x0C00), 2, MODULE_UART, 0) /* Alt 2, P0.10 (UART1_TX), P0.11 (UART1_RX) */
ALTERNATE(PIN_MASK(0, 0x000C), 1, MODULE_I2C, 0)  /* Alt 1, P0.2  (I2C1_SCL), P0.3  (I2C1_SDA) */
