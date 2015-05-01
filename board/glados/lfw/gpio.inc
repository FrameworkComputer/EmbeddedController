/* -*- mode:c -*-
 *
 * Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Minimal set of GPIOs needed for LFW loader
 */

/* SPI PVT chip select */
GPIO(PVT_CS0, PORT(14), 6, GPIO_ODR_HIGH)

/* Alternate functions GPIO definition */
/* UART */
ALTERNATE(PORT(16),     0x24,   1,      MODULE_UART,            0)
/* SPI pins */
ALTERNATE(PORT(5),      0x10,   1,      MODULE_SPI,             0)
ALTERNATE(PORT(16),     0x10,   1,      MODULE_SPI,             0)
ALTERNATE(PORT(15),     0x08,   1,      MODULE_SPI,             0)
