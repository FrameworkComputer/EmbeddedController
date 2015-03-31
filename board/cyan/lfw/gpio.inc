/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Minimal set of GPIOs needed for LFW loader
 */

GPIO(PVT_CS0,           PIN(146), GPIO_ODR_HIGH)                 /* SPI PVT Chip select */

/* Alternate functions GPIO definition */
ALTERNATE(PIN_MASK(16, 0x24),   1,      MODULE_UART,            0)                      /* UART0 */
ALTERNATE(PIN_MASK(5,  0x10),   1,      MODULE_SPI,             0)
ALTERNATE(PIN_MASK(16, 0x10),   1,      MODULE_SPI,             0)
ALTERNATE(PIN_MASK(15, 0x08),   1,      MODULE_SPI,             0)                      /* 153: CLK */
