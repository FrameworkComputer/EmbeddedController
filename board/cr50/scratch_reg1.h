/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_SCRATCH_REG1_H
#define __EC_BOARD_CR50_SCRATCH_REG1_H

/*
 * Bit assignments of the LONG_LIFE_SCRATCH1 register. This register survives
 * all kinds of resets, it is cleared only on the Power ON event.
 */
#define BOARD_SLAVE_CONFIG_SPI       (1 << 0)   /* TPM uses SPI interface */
#define BOARD_SLAVE_CONFIG_I2C       (1 << 1)   /* TPM uses I2C interface */
#define BOARD_USB_AP                 (1 << 2)   /* One of the USB PHYs is  */
						/* connected to the AP */
/*
 * This gap is left to enusre backwards compatibility with the earliest cr50
 * code releases. It will be possible to safely reuse this gap if and when the
 * rest of the bits are taken.
 */

/* TODO(crosbug.com/p/56945): Remove when sys_rst_l has an external pullup */
#define BOARD_NEEDS_SYS_RST_PULL_UP  (1 << 5)   /* Add a pullup to sys_rst_l */
#define BOARD_USE_PLT_RESET          (1 << 6)   /* Platform reset exists */

/*
 * Bits to store console and write protect bit states across deep sleep and
 * resets.
 */
#define BOARD_CONSOLE_UNLOCKED       (1 << 7)
#define BOARD_WP_ASSERTED            (1 << 8)

#endif  /* ! __EC_BOARD_CR50_SCRATCH_REG1_H */
