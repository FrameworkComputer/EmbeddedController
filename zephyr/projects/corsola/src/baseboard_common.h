/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola baseboard-specific onfiguration common to ECOS and Zephyr */

#ifndef __CROS_EC_BASEBOARD_COMMON_H
#define __CROS_EC_BASEBOARD_COMMON_H

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

/**
 * board_get_version() - Get the board version
 *
 * Read the ADC to obtain the board version
 *
 * @return board version in the range 0 to 14 inclusive
 */
int board_get_version(void);

void ppc_interrupt(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
void x_ec_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BASEBOARD_COMMON_H */
