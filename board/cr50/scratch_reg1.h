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

/*
 * The gaps are left to enusre backwards compatibility with the earliest cr50
 * code releases. It will be possible to safely reuse these gaps if and when the
 * rest of the bits are taken.
 */

/* TODO(crosbug.com/p/56945): Remove when sys_rst_l has an external pullup */
#define BOARD_NEEDS_SYS_RST_PULL_UP  (1 << 5)   /* Add a pullup to sys_rst_l */
#define BOARD_USE_PLT_RESET          (1 << 6)   /* Use plt_rst_l instead of */
						/* sys_rst_l to monitor the */
						/* system resets */

/* Bits to store write protect bit state across deep sleep and resets. */
#define BOARD_WP_ASSERTED            (1 << 8)
#define BOARD_FORCING_WP             (1 << 9)

/*
 * Bit to signal to compatible RO to suppress its uart output.
 * Helps to reduce time to resume from deep sleep.
 */
#define BOARD_NO_RO_UART             (1 << 10)

/*
 * Bits to store current case-closed debug state across deep sleep.
 *
 * DO NOT examine these bits to determine the current CCD state.  Call methods
 * from case_closed_debug.h instead.
 */
#define BOARD_CCD_SHIFT              11
#define BOARD_CCD_STATE              (3 << BOARD_CCD_SHIFT)

/* Prevent Cr50 from entering deep sleep when the AP is off */
#define BOARD_DEEP_SLEEP_DISABLED    (1 << 13)
/* Use Cr50_RX_AP_TX to determine if the AP is off or on */
#define BOARD_DETECT_AP_WITH_UART    (1 << 14)
/*
 * Macro to capture all properties related to board strapping pins. This must be
 * updated if additional strap related properties are added.
 */
#define BOARD_ALL_PROPERTIES (BOARD_SLAVE_CONFIG_SPI | BOARD_SLAVE_CONFIG_I2C \
	| BOARD_NEEDS_SYS_RST_PULL_UP | BOARD_USE_PLT_RESET | \
	BOARD_DEEP_SLEEP_DISABLED | BOARD_DETECT_AP_WITH_UART)

#endif  /* ! __EC_BOARD_CR50_SCRATCH_REG1_H */
